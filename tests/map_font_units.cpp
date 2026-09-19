#include "MapGlyphAtlas.h"
#include "MapFontPreparation.h"
#include "MapCache.h"
#include "FontFallback.h"
#include "service/MapInventory.h"
#include "service/MapFontService.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

static int failures = 0;
static int passes = 0;
static MapGlyphAtlas* drawAtlas = nullptr;
static void check(bool condition, const char* name) {
	std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
	condition ? ++passes : ++failures;
}

static bool prepareAtlas(MapGlyphAtlas& atlas, const std::set<MapGlyphAtlas::Profile>& profiles,
	const MapGlyphAtlas::Upload& upload) {
	return atlas.prepare(profiles, upload) == MapGlyphAtlas::Preparation::Ready;
}

static void testFailureIsolation(const MapGlyphAtlas::FontData& source, const MapGlyphAtlas::FontData& secondary, const MapGlyphAtlas::FontData& latin) {
	ImFontAtlas hostAtlas;
	ImFontConfig config;
	config.FontDataOwnedByAtlas = false;
	const ImWchar ranges[]{0x20, 0x2E7F, 0};
	ImFont* host = hostAtlas.AddFontFromMemoryTTF(latin->data(), static_cast<int>(latin->size()), 20, &config, ranges);
	hostAtlas.Build();
	// A custom UI font may also expose symbols in the private-use area.
	host->AddGlyph(nullptr, 0xE000, 0, 0, 10, 10, 0, 0, 1, 1, 10);
	host->BuildLookupTable();
	bool hostAvailable = true, failUpload = false;
	int attempts = 0, uploads = 0, liveTextures = 0;
	uint64_t hostRevision = 1;
	auto now = MapFontService::Clock::time_point(std::chrono::seconds(100));
	const std::set<MapGlyphAtlas::Profile> profiles{{28, false}, {72, false}, {72, true}};
	auto hostCoverage = [&](const auto&, ImWchar c) { return hostAvailable && host->FindGlyphNoFallback(c); };
	auto upload = [&](const unsigned char*, int, int) -> MapGlyphAtlas::Texture {
		++attempts;
		if (failUpload) return {};
		++uploads;
		++liveTextures;
		return {new int(uploads), [&](void* p) { delete static_cast<int*>(p); --liveTextures; }};
	};
	MapFontService service;
	service.setSources({source, secondary});
	auto prepare = [&](size_t budget = SIZE_MAX, const std::string& literals = "") {
		return service.prepare(profiles, literals, hostCoverage, hostRevision, upload, now, budget);
	};
	service.addText("预内室");
	check(prepare() && service.ready() && service.textReady("预内室"), "service starts with usable committed map fonts");
	ImFont* oldFont = service.find(72, false, 0x9884);
	void* oldTexture = oldFont->ContainerAtlas->TexID;
	const float oldU = oldFont->FindGlyphNoFallback(0x9884)->U0;

	MapFontPreparation queue;
	MapInventory inventory;
	auto makeMap = [](int id, const std::string& name) {
		gw2api::continents::map map{};
		map.id = id;
		map.name = name;
		return map;
	};
	inventory.addMap("zh", makeMap(1, "预内室"));
	const std::string badName = "预\xCD\xB8内室";
	queue.submit({"zh", {makeMap(2, badName), makeMap(3, "预兆内室")}, true});
	auto advance = [&] {
		return queue.advance([&](const std::string& text) { service.addText(text); },
			[&] { prepare(); }, [&](const std::string& text) { return service.textReady(text); },
			[&](MapFontPreparation::Batch batch) {
				for (auto& map : batch.maps) inventory.addMap(batch.locale, std::move(map));
				if (batch.completesLocale) inventory.markLocaleLoaded(batch.locale);
			});
	};
	failUpload = true;
	check(!advance() && service.ready() && inventory.getMapInfo("zh", 1)
		&& service.textReady("预内室"), "failed incremental upload preserves service readiness and existing maps");
	check(!inventory.getMapInfo("zh", 2) && !inventory.getMapInfo("zh", 3), "uncovered maps stay hidden after incremental failure");
	const int failedAttempts = attempts;
	queue.submit({"zh", {makeMap(4, "内室")}, false});
	now += std::chrono::seconds(1);
	check(!advance() && inventory.getMapInfo("zh", 4) && attempts == failedAttempts,
		"already-covered later map publishes during another map's upload backoff");
	failUpload = false;
	now += std::chrono::seconds(5);
	check(!advance() && inventory.getMapInfo("zh", 3) && !inventory.getMapInfo("zh", 2)
		&& inventory.isLocaleLoaded("zh"), "good map in poisoned batch publishes and locale enumeration completes");
	check(service.find(72, false, 0x9884) == oldFont && oldFont->ContainerAtlas->TexID == oldTexture
		&& oldFont->FindGlyphNoFallback(0x9884)->U0 == oldU,
		"failed and recovered increment preserves old font pointer texture and UV");
	check(service.missingCharacters().contains(0x0378) && !service.find(72, false, 0x0378),
		"missing source glyph is isolated without placeholder coverage");
	const int afterRecovery = attempts;
	const size_t buildsAfterRecovery = service.buildCount();
	for (int i = 0; i < 100; ++i) { now += std::chrono::seconds(5); advance(); }
	check(attempts == afterRecovery && service.buildCount() == buildsAfterRecovery,
		"permanent missing glyph never causes repeated page builds or uploads");
	queue.submit({"zh", {makeMap(5, "龘")}, false});
	check(!advance() && inventory.getMapInfo("zh", 5), "new supported glyph publishes behind a permanently pending map");

	service.addText("é ü Ω \xEE\x80\x80");
	const int beforeHostText = uploads;
	check(prepare() && service.textReady("é ü Ω \xEE\x80\x80") && uploads == beforeHostText,
		"actual host glyphs including private-use symbols need no private pages");
	FallbackTextFont hostText{nullptr, nullptr, nullptr, host, 72};
	check(hostText.forGlyph(0xE000) == host && hostText.forGlyph(0x03A9) == host,
		"renderer uses the same host coverage policy for symbols and Greek");
	check(prepare(SIZE_MAX, badName) && service.ready() && service.textReady("预内室"),
		"unsupported template literal cannot invalidate committed maps");
	check(prepare(SIZE_MAX, "预内室") && service.textReady("预内室"),
		"removing unsupported template literal recovers without restarting");
	check(!hostText.covers(badName.c_str()), "unsupported literal is rejected for its text rather than drawn as a missing glyph");

	const std::set<MapGlyphAtlas::Profile> larger{{30, false}, {80, false}, {80, true}};
	failUpload = true;
	check(!service.prepare(larger, "", hostCoverage, hostRevision, upload, now, SIZE_MAX)
		&& service.ready() && service.find(80, false, 0x9884) == oldFont,
		"failed size replacement retains usable previous font profiles");
	check(!service.textReady("预内室"), "previous-size fallback does not claim new native-size publication coverage");
	failUpload = false;
	now += std::chrono::seconds(6);
	const int beforeReplacement = liveTextures;
	check(service.prepare(larger, "", hostCoverage, hostRevision, upload, now, SIZE_MAX)
		&& service.textReady("预内室") && service.find(80, false, 0x9884)->FontSize == 80
		&& liveTextures < beforeReplacement + 3, "successful size replacement retires old profiles only after upload");

	MapFontService cold;
	cold.setSources({source, secondary});
	cold.addText("预兆内室");
	const int beforeCold = uploads;
	check(!cold.prepare(profiles, "", hostCoverage, 0, upload, now)
		&& uploads == beforeCold + 1 && !cold.textReady("预兆内室"), "first frame builds at most one profile page");
	check(!cold.prepare(profiles, "", hostCoverage, 0, upload, now)
		&& uploads == beforeCold + 2 && !cold.textReady("预兆内室"), "second frame continues without upload retry delay");
	check(cold.prepare(profiles, "", hostCoverage, 0, upload, now)
		&& uploads == beforeCold + 3 && cold.textReady("预兆内室"), "first map becomes publishable only after all native-size pages finish");

	const auto path = std::filesystem::temp_directory_path()
		/ ("rot-isolated-cache-" + std::to_string(MapFontService::Clock::now().time_since_epoch().count()) + ".json");
	writeMapCacheTemporary(path, {{2, makeMap(2, badName)}, {3, makeMap(3, "预兆内室")}});
	const auto cache = readMapCache(path);
	std::filesystem::remove(path);
	MapFontService restarted;
	restarted.setSources({source, secondary});
	MapFontPreparation restartedQueue;
	MapInventory restartedInventory;
	std::vector<gw2api::continents::map> maps;
	appendMissingCachedMaps(maps, cache);
	restartedQueue.submit({"zh", std::move(maps), true});
	check(!restartedQueue.advance([&](const std::string& text) { restarted.addText(text); },
		[&] { restarted.prepare(profiles, "", hostCoverage, 0, upload, now, SIZE_MAX); },
		[&](const std::string& text) { return restarted.textReady(text); },
		[&](MapFontPreparation::Batch batch) {
			for (auto& map : batch.maps) restartedInventory.addMap(batch.locale, std::move(map));
			if (batch.completesLocale) restartedInventory.markLocaleLoaded(batch.locale);
		}) && restarted.ready() && restartedInventory.getMapInfo("zh", 3)
		&& !restartedInventory.getMapInfo("zh", 2), "cached unsupported map cannot poison other maps across restart");
	maps = {makeMap(2, "内室")};
	appendMissingCachedMaps(maps, cache);
	MapFontService upgraded;
	upgraded.setSources({source, secondary});
	for (const auto& map : maps) upgraded.addText(mapNameText(map));
	check(upgraded.prepare(profiles, "", hostCoverage, 0, upload, now, SIZE_MAX)
		&& upgraded.missingCharacters().empty() && maps.size() == 2 && maps.front().name == "内室",
		"superseded cache name affects neither effective maps nor glyph requirements");

	MapFontService latinOnly;
	latinOnly.setSources({});
	latinOnly.addText("é Ω");
	check(latinOnly.prepare(profiles, "", hostCoverage, hostRevision, upload, now, SIZE_MAX)
		&& latinOnly.textReady("é Ω") && latinOnly.pageCount() == 0, "host-covered locale works without installed CJK fonts");
	hostAvailable = false;
	check(latinOnly.prepare(profiles, "", hostCoverage, ++hostRevision, upload, now, SIZE_MAX)
		&& !latinOnly.textReady("é Ω"), "host font invalidation rechecks publication coverage");
	hostAvailable = true;
	check(latinOnly.prepare(profiles, "", hostCoverage, ++hostRevision, upload, now, SIZE_MAX)
		&& latinOnly.textReady("é Ω"), "host font recovery restores coverage without clearing map state");
	service.clear(); cold.clear(); restarted.clear(); upgraded.clear(); latinOnly.clear();
	check(liveTextures == 0, "all retained replacement and supplementary textures release on unload");
}

int main(int argc, char** argv) {
	if (argc != 4 && argc != 5) { std::cerr << "Usage: map_font_units CJK.ttf seed.txt Latin.ttf [SecondCJK.ttf]\n"; return 2; }
	std::ifstream input(argv[1], std::ios::binary);
	auto source = std::make_shared<std::vector<unsigned char>>(
		std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	if (source->empty()) { std::cerr << "CJK test font is missing\n"; return 2; }
	auto secondary = source;
	if (argc == 5) {
		std::ifstream secondFile(argv[4], std::ios::binary);
		secondary = std::make_shared<std::vector<unsigned char>>(std::istreambuf_iterator<char>(secondFile), std::istreambuf_iterator<char>());
		if (secondary->empty()) { std::cerr << "Second CJK test font is missing\n"; return 2; }
	}

	std::ifstream latinFile(argv[3], std::ios::binary);
	auto latin = std::make_shared<std::vector<unsigned char>>(
		std::istreambuf_iterator<char>(latinFile), std::istreambuf_iterator<char>());
	if (latin->empty()) { std::cerr << "Latin fallback test font is missing\n"; return 2; }
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = {800, 600};
	ImFont* hostFont = io.Fonts->AddFontDefault();
	io.Fonts->Build();
	int hostTexture = 0;
	io.Fonts->SetTexID(&hostTexture);
	const int hostWidth = io.Fonts->TexWidth, hostHeight = io.Fonts->TexHeight;
	const int hostGlyphs = hostFont->Glyphs.Size;
	{
		MapGlyphAtlas atlas;
		atlas.setSources({source, secondary});
		atlas.addText("预内室 泰瑞亚 卡斯特拉 魔泉空洞");
		int uploads = 0, liveTextures = 0;
		bool failUpload = false;
		int failAfterUploads = -1;
		auto upload = [&](const unsigned char* pixels, int width, int height) -> MapGlyphAtlas::Texture {
			check(pixels && width > 0 && height > 0, "upload receives rasterized pixels");
			if (failUpload || uploads == failAfterUploads) return {};
			++uploads;
			++liveTextures;
			return {new int(uploads), [&](void* p) { delete static_cast<int*>(p); --liveTextures; }};
		};
		const std::set<MapGlyphAtlas::Profile> profiles{{28, false}, {72, false}, {72, false}, {72, true}};
		check(prepareAtlas(atlas, profiles, upload), "known map names prepare successfully");
		check(uploads == 3, "identical face and size rasterized once");
		check(atlas.find(72, false, 0x5146) == nullptr, "unseen zhao is not preloaded");
		ImFont* oldFont = atlas.find(72, false, 0x9884);
		const ImFontGlyph* oldGlyph = oldFont->FindGlyphNoFallback(0x9884);
		const float oldU = oldGlyph->U0;
		void* oldTexture = oldFont->ContainerAtlas->TexID;

		gw2api::continents::map apiMap{};
		apiMap.id = 9999;
		apiMap.name = "魔泉空洞";
		apiMap.regionName = "卡斯特拉";
		apiMap.continentName = "泰瑞亚";
		gw2api::continents::sector sector{};
		sector.id = 123;
		sector.name = "预兆内室";
		sector.bounds = {{1, 2}, {3, 4}, {5, 6}};
		apiMap.sectors.emplace("123", sector);
		MapFontPreparation queue;
		MapInventory inventory;
		queue.submit({"zh", {apiMap}, true});
		auto publish = [&](MapFontPreparation::Batch batch) {
			for (auto& map : batch.maps) inventory.addMap(batch.locale, std::move(map));
			if (batch.completesLocale) inventory.markLocaleLoaded(batch.locale);
		};
		auto advance = [&] {
			return queue.advance([&](const std::string& text) { atlas.addText(text); },
				[&] { return prepareAtlas(atlas, profiles, upload); },
				[&](const std::string& text) {
					for (ImWchar c : mapTextCharacters(text))
						for (const auto& profile : profiles)
							if (!atlas.find(profile.size, profile.animation, c)) return false;
					return true;
				}, publish);
		};
		failUpload = true;
		check(!advance(), "texture failure keeps map pending");
		check(!inventory.getMapInfo("zh", 9999) && inventory.isLocaleLoaded("zh"),
			"locale enumeration finishes while unavailable map stays invisible");
		check(!atlas.find(72, false, 0x5146), "failed upload is not registered as coverage");
		failUpload = false;
		check(advance(), "retry publishes map after upload succeeds");
		check(inventory.getMapInfo("zh", 9999) && inventory.isLocaleLoaded("zh"), "map and locale published together");
		ImFont* zhao = atlas.find(72, false, 0x5146);
		check(zhao && zhao->FontSize == 72 && zhao->FindGlyphNoFallback(0x5146), "first API zhao has native 72px glyph");
		check(atlas.find(72, true, 0x5146), "animation face also covers new character");
		check(oldFont == atlas.find(72, false, 0x9884) && oldFont->ContainerAtlas->TexID == oldTexture
			&& oldFont->FindGlyphNoFallback(0x9884)->U0 == oldU, "supplement preserves existing font pointers textures and UVs");
		check(zhao->FindGlyphNoFallback(0x9884) == nullptr, "supplement contains new glyphs only");
		const int uploadsAfterMap = uploads;
		queue.submit({"zh", {apiMap}, false});
		check(advance() && uploads == uploadsAfterMap, "same map does not upload again");
		for (int i = 0; i < 100; ++i) prepareAtlas(atlas, profiles, upload);
		check(uploads == uploadsAfterMap, "ordinary frames do not rebuild pages");
		// A less common character absent from the original seed follows the same path.
		atlas.addText("龘");
		failAfterUploads = uploads + 1;
		check(!prepareAtlas(atlas, profiles, upload) && atlas.find(28, false, 0x9F98)
			&& !atlas.find(72, false, 0x9F98), "partial upload keeps completed profiles without claiming others");
		const int partialUploads = uploads;
		failAfterUploads = -1;
		check(prepareAtlas(atlas, profiles, upload) && uploads == partialUploads + 2
			&& atlas.find(72, false, 0x9F98), "uncommon character retry uploads only unfinished profiles");

		drawAtlas = &atlas;
		FallbackTextFont text{hostFont, nullptr, hostFont, hostFont, 72,
			[](float size, bool animation, ImWchar c) { return drawAtlas->find(size, animation, c); }};
		const float expectedWidth = atlas.find(72, false, 0x9884)->GetCharAdvance(0x9884)
			+ zhao->GetCharAdvance(0x5146);
		check(text.measure("预兆").x == expectedWidth, "centering uses actual glyph advances across pages");
		ImGui::NewFrame();
		ImGui::SetNextWindowSize({780, 200});
		ImGui::Begin("test");
		ImDrawList* list = ImGui::GetWindowDrawList();
		text.draw(list, ImGui::GetCursorScreenPos(), IM_COL32_WHITE, "A预兆内室");
		bool sawBase = false, sawSupplement = false, sawHost = false;
		for (const auto& cmd : list->CmdBuffer) {
			if (!cmd.ElemCount) continue;
			sawBase |= cmd.TextureId == oldTexture;
			sawSupplement |= cmd.TextureId == zhao->ContainerAtlas->TexID;
			sawHost |= cmd.TextureId == &hostTexture;
		}
		check(sawBase && sawSupplement && sawHost, "one text emits valid commands for host base and supplemental textures");
		ImGui::End();
		ImGui::Render();
		check(io.Fonts->TexWidth == hostWidth && io.Fonts->TexHeight == hostHeight
			&& hostFont->Glyphs.Size == hostGlyphs && io.Fonts->TexID == &hostTexture,
			"host shared atlas dimensions glyphs and texture remain unchanged");

		const auto temporary = std::filesystem::temp_directory_path()
			/ ("rot-map-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
		writeMapCacheTemporary(temporary, {{apiMap.id, apiMap}});
		const auto cached = readMapCache(temporary);
		{ std::ofstream bad(temporary); bad << "{broken"; }
		bool invalidCacheRejected = false;
		try { readMapCache(temporary); } catch (const json::exception&) { invalidCacheRejected = true; }
		check(invalidCacheRejected, "corrupt cache is rejected for caller recovery");
		std::filesystem::remove(temporary);
		check(cached.size() == 1 && cached.at(9999).sectors.at("123").name == "预兆内室"
			&& cached.at(9999).sectors.at("123").bounds[1].y == 4, "cache roundtrip preserves Unicode names and map geometry");
		MapGlyphAtlas restarted;
		restarted.setSources({source});
		for (const auto& [id, map] : cached) restarted.addText(mapNameText(map));
		check(prepareAtlas(restarted, {{72, false}}, upload) && restarted.find(72, false, 0x5146),
			"restart preloads zhao from cached map names");
		prepareAtlas(atlas, {{28, false}}, upload);
		check(!atlas.find(72, false, 0x5146), "unused sizes are retired at frame boundary");
		atlas.clear();
		restarted.clear();
		check(liveTextures == 0, "unload releases all private textures");
		atlas.setSources({source});
		check(!atlas.addText("\xCD\xB8") && !atlas.supports(0x0378),
			"unassigned character is rejected before bitmap preparation");
		check(prepareAtlas(atlas, {{72, false}}, upload) && !atlas.find(72, false, 0x0378),
			"unavailable character cannot poison the staged or committed glyph set");
		atlas.clear();
	}
	{
		ImFontAtlas latinAtlas;
		ImFontConfig config;
		config.FontDataOwnedByAtlas = false;
		const ImWchar latinRanges[]{0x20, 0x2E7F, 0};
		ImFont* hostLatin = latinAtlas.AddFontFromMemoryTTF(latin->data(), static_cast<int>(latin->size()), 20, &config, latinRanges);
		latinAtlas.Build();
		std::ifstream seedFile(argv[2], std::ios::binary);
		std::string seed(std::istreambuf_iterator<char>(seedFile), {});
		MapFontService service;
		service.setSources({source, secondary});
		service.addText(seed);
		const auto start = std::chrono::steady_clock::now();
		check(service.prepare({{20, false}, {28, false}, {72, false}, {28, true}, {72, true}}, "",
			[&](const auto&, ImWchar c) { return hostLatin->FindGlyphNoFallback(c) != nullptr; }, 0,
			[](const unsigned char*, int, int) -> MapGlyphAtlas::Texture { return {new int(1), [](void* p) { delete static_cast<int*>(p); }}; },
			start, SIZE_MAX) && service.textReady(seed),
			"bundled seed is covered using CJK-only private sources and actual host fallback");
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		std::cout << "MEASURE bundled private pages=" << service.pageCount()
			<< " RGBA_bytes=" << service.textureBytes() << " CPU_ms=" << ms << '\n';
	}

	testFailureIsolation(source, secondary, latin);
	ImGui::DestroyContext();
	std::cout << "Map font tests: " << passes << " passed, " << failures << " failed\n";
	return failures ? 1 : 0;
}
