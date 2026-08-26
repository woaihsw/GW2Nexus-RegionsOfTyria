#include "FontReload.h"
#include "MapRetry.h"
#include "MapSectorMerge.h"
#include "PopupAnimation.h"
#include "SectorGeometry.h"
#include "Settings.h"
#include "SettingsBackup.h"
#include "WideUtf8.h"
#include "service/MapInventory.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_passes = 0;

static void check(bool condition, const char* name) {
	if (condition) {
		std::cout << "PASS " << name << "\n";
		g_passes++;
	}
	else {
		std::cerr << "FAIL " << name << "\n";
		g_failures++;
	}
}

static void testSettingsJson() {
	SettingsLoadStatus status = SettingsLoadStatus::Recovered;

	Settings valid = ParseSettingsJson(
		R"({"locale":"zh","enablePopup":false,"popupAnimationDuration":9})",
		status);
	check(status == SettingsLoadStatus::Ok, "settings valid json status");
	check(valid.locale == Locale::Zh, "settings valid locale zh");
	check(valid.enablePopup == false, "settings valid enablePopup");
	check(valid.popupAnimationDuration == 9, "settings valid duration");
	check(valid.popupAnimationSpeed == 35, "settings missing field keeps default speed");

	Settings missing = ParseSettingsJson(R"({"locale":"de"})", status);
	check(status == SettingsLoadStatus::Ok, "settings missing fields status");
	check(missing.locale == Locale::De, "settings missing fields locale");
	check(missing.enablePopup == true, "settings missing fields enablePopup default");
	check(missing.fontSettings[0].largeFontSize == 72.0f, "settings missing fields font size default");

	Settings defaults = MakeDefaultSettings();
	Settings corrupt = ParseSettingsJson("{this is not json", status);
	check(status == SettingsLoadStatus::Recovered, "settings corrupt status");
	check(corrupt.locale == defaults.locale, "settings corrupt locale default");
	check(corrupt.enablePopup == defaults.enablePopup, "settings corrupt enablePopup default");
	check(corrupt.popupAnimationSpeed == defaults.popupAnimationSpeed, "settings corrupt speed default");

	Settings invalidLocale = ParseSettingsJson(R"({"locale":"nope"})", status);
	check(status == SettingsLoadStatus::Recovered, "settings invalid locale recovered");
	check(invalidLocale.locale == defaults.locale, "settings invalid locale default");
}

static void testPopupOpacity() {
	PopupAnimationParams params = PopupAnimationParams::fromSettings(35, 3);
	const float fade = params.fadeSeconds();
	check(fade > 0.0f, "popup fade duration positive");
	check(params.holdSeconds == 3.0f, "popup hold from settings");

	const float midIn = PopupOpacityAt(fade * 0.5f, params);
	check(midIn > 0.4f && midIn < 0.6f, "popup fade-in midpoint");
	check(PopupAnimationActive(fade * 0.5f, params), "popup active during fade-in");

	const float holdSample = fade + params.holdSeconds * 0.5f;
	check(PopupOpacityAt(holdSample, params) == 1.0f, "popup hold full opacity");
	check(PopupAnimationActive(holdSample, params), "popup active during hold");

	const float midOut = PopupOpacityAt(fade + params.holdSeconds + fade * 0.5f, params);
	check(midOut > 0.4f && midOut < 0.6f, "popup fade-out midpoint");

	const float after = params.totalSeconds() + 0.05f;
	check(PopupOpacityAt(after, params) == 0.0f, "popup finished opacity");
	check(!PopupAnimationActive(after, params), "popup inactive after timeline");
}

static void testInventoryReplace() {
	MapInventory inventory;
	gw2api::continents::map first{};
	first.id = 15;
	first.name = "first-name";
	inventory.addMap("zh", first);

	gw2api::continents::map second{};
	second.id = 15;
	second.name = "replacement-name";
	inventory.addMap("zh", second);

	gw2api::continents::map* found = inventory.getMapInfo("zh", 15);
	check(found != nullptr, "inventory replace lookup exists");
	check(found->name == "replacement-name", "inventory replace returns new object");
	check(found->name != "first-name", "inventory replace discards old object");
	check(inventory.getMapInfo("en", 15) == nullptr, "inventory other locale missing");
}

static void testUtf8DestCapacity() {
	check(utf8DestCapacityForWideCharSize(0) == 0, "utf8 dest capacity zero");
	check(utf8DestCapacityForWideCharSize(-1) == 0, "utf8 dest capacity negative");
	check(utf8DestCapacityForWideCharSize(1) == 1, "utf8 dest capacity empty-plus-nul");
	check(utf8DestCapacityForWideCharSize(12) == 12, "utf8 dest capacity includes nul");
	check(utf8DestCapacityForWideCharSize(12) > 12 - 1, "utf8 dest capacity is not size-minus-one");
	check(utf8PayloadLengthFromWideCharWritten(12) == 11, "utf8 payload drops trailing nul");
	check(utf8PayloadLengthFromWideCharWritten(1) == 0, "utf8 payload empty string");
	check(utf8PayloadLengthFromWideCharWritten(0) == 0, "utf8 payload zero written");
}

static void testSettingsBackupPath() {
	check(settingsBackupCandidate("/addons/settings.json", 0) == "/addons/settings.json.bad",
		"settings backup preferred name");
	check(settingsBackupCandidate("/addons/settings.json", 1) == "/addons/settings.json.bad.1",
		"settings backup first suffix");

	std::set<std::string> existing;
	auto exists = [&](const std::string& path) {
		return existing.contains(path);
	};
	check(nextSettingsBackupPath("/addons/settings.json", exists) == "/addons/settings.json.bad",
		"settings backup unused preferred");

	existing.insert("/addons/settings.json.bad");
	check(nextSettingsBackupPath("/addons/settings.json", exists) == "/addons/settings.json.bad.1",
		"settings backup when .bad exists");

	existing.insert("/addons/settings.json.bad.1");
	existing.insert("/addons/settings.json.bad.2");
	check(nextSettingsBackupPath("/addons/settings.json", exists) == "/addons/settings.json.bad.3",
		"settings backup skips occupied suffixes");
}

static void testFontReloadSchedule() {
	FontReloadSchedule reload;
	check(reload.state == FontReloadState::Idle, "font reload starts idle");
	check(!reload.shouldLoadFonts(true, 1.0f), "font reload idle does not load");

	reload.request();
	check(reload.state == FontReloadState::WaitingForRelease, "font reload waits after request");
	check(!reload.shouldLoadFonts(false, 1.0f), "font reload waits until fonts cleared");
	check(!reload.shouldLoadFonts(true, 0.0f), "font reload waits for grace period");
	check(!reload.shouldLoadFonts(true, FontReloadSchedule::kGraceSeconds - 0.001f),
		"font reload still waiting just before grace");
	check(reload.shouldLoadFonts(true, FontReloadSchedule::kGraceSeconds),
		"font reload loads after clear and grace");

	reload.cancel();
	check(reload.state == FontReloadState::Idle, "font reload cancel returns idle");
	check(!reload.shouldLoadFonts(true, 1.0f), "font reload cancelled does not load");
}

static void testMapRetryDelay() {
	check(mapRetryDelay(0).count() == 0, "retry delay zero failures");
	check(mapRetryDelay(-1).count() == 0, "retry delay negative failures");
	check(mapRetryDelay(1) == kMapRetryInitialDelay, "retry delay first failure is 30s");
	check(mapRetryDelay(1).count() == 30, "retry delay first failure seconds");
	check(mapRetryDelay(2).count() == 60, "retry delay second failure is 60s");
	check(mapRetryDelay(3).count() == 120, "retry delay third failure is 120s");
	check(mapRetryDelay(4).count() == 240, "retry delay fourth failure is 240s");
	check(mapRetryDelay(5) == kMapRetryMaxDelay, "retry delay fifth failure caps at 5 minutes");
	check(mapRetryDelay(5).count() == 300, "retry delay cap is 300 seconds");
	check(mapRetryDelay(6) == kMapRetryMaxDelay, "retry delay sixth failure remains capped");
	check(mapRetryDelay(32) == kMapRetryMaxDelay, "retry delay large failure count remains capped");

	const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1000));
	MapLoadRetryState waiting;
	waiting.failureCount = 1;
	waiting.nextRetryAt = now + mapRetryDelay(waiting.failureCount);
	check(!mapRetryIsDue(now, waiting), "retry not due before backoff elapses");
	check(mapRetryIsDue(waiting.nextRetryAt, waiting), "retry due exactly at backoff");
	check(mapRetryIsDue(waiting.nextRetryAt + std::chrono::seconds(1), waiting), "retry due after backoff");
}

static void testSectorMergeAndDefault() {
	gw2api::continents::map floor1{};
	floor1.id = 15;
	floor1.name = "Queensdale";
	gw2api::continents::sector sectorOne{};
	sectorOne.id = 1;
	sectorOne.name = "Shaemoor";
	floor1.sectors.emplace("1", sectorOne);

	gw2api::continents::map floor2{};
	floor2.id = 15;
	floor2.name = "Queensdale-floor2";
	gw2api::continents::sector sectorTwo{};
	sectorTwo.id = 2;
	sectorTwo.name = "Township";
	floor2.sectors.emplace("2", sectorTwo);
	gw2api::continents::sector sectorOneDup{};
	sectorOneDup.id = 1;
	sectorOneDup.name = "Shaemoor-dup";
	floor2.sectors.emplace("1", sectorOneDup);

	std::map<std::string, gw2api::continents::map> mapInfos;
	{
		const bool inserted = mapInfos.count("15") == 0;
		gw2api::continents::map& stored = storedMapForMerge(mapInfos, "15", floor1);
		check(inserted, "sector merge first occurrence inserts");
		if (inserted) {
			stored.name = floor1.name;
		}
		mergeMapSectors(stored, floor1);
	}
	{
		const bool inserted = mapInfos.count("15") == 0;
		gw2api::continents::map& stored = storedMapForMerge(mapInfos, "15", floor2);
		check(!inserted, "sector merge later occurrence reuses stored map");
		if (inserted) {
			stored.name = floor2.name;
		}
		mergeMapSectors(stored, floor2);
	}

	check(mapInfos["15"].sectors.size() == 2, "sector merge keeps sectors from every floor");
	check(mapInfos["15"].name == "Queensdale", "sector merge does not replace stored map metadata");
	check(mapInfos["15"].sectors["1"].name == "Shaemoor", "sector merge keeps first sector on duplicate id");
	check(mapInfos["15"].sectors["2"].name == "Township", "sector merge adds new floor sector");

	addDefaultSector(mapInfos["15"]);
	check(mapInfos["15"].sectors.size() == 2, "default sector skipped when sectors exist");

	std::map<std::string, gw2api::continents::map> emptyInfos;
	gw2api::continents::map emptyMap{};
	emptyMap.id = 99;
	emptyMap.name = "Empty Map";
	emptyInfos.emplace("99", emptyMap);
	addDefaultSector(emptyInfos["99"]);
	check(emptyInfos["99"].sectors.size() == 1, "default sector persists on stored empty map");
	check(emptyInfos["99"].sectors.contains("-1"), "default sector uses -1 key");
	check(emptyInfos["99"].sectors["-1"].id == -1, "default sector id is -1");
	check(emptyInfos["99"].sectors["-1"].name == "Empty Map", "default sector uses map name");
}

static void testPointInPolygon() {
	struct Point {
		float x;
		float y;
	};
	const std::vector<Point> square = {
		{0.0f, 0.0f},
		{10.0f, 0.0f},
		{10.0f, 10.0f},
		{0.0f, 10.0f}
	};
	check(pointInPolygon(5.0f, 5.0f, square), "polygon inside");
	check(!pointInPolygon(15.0f, 5.0f, square), "polygon outside");
	check(!pointInPolygon(-1.0f, -1.0f, square), "polygon outside negative");
}

int main() {
	testSettingsJson();
	testPopupOpacity();
	testInventoryReplace();
	testPointInPolygon();
	testUtf8DestCapacity();
	testSettingsBackupPath();
	testFontReloadSchedule();
	testMapRetryDelay();
	testSectorMergeAndDefault();
	std::cout << g_passes << " passed, " << g_failures << " failed\n";
	return g_failures == 0 ? 0 : 1;
}
