#include "FontReload.h"
#include "PopupAnimation.h"
#include "SectorGeometry.h"
#include "Settings.h"
#include "SettingsBackup.h"
#include "WideUtf8.h"
#include "service/MapInventory.h"

#include <cmath>
#include <iostream>
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
	std::cout << g_passes << " passed, " << g_failures << " failed\n";
	return g_failures == 0 ? 0 : 1;
}
