#include "MapGlyphAtlas.h"
#include "MapFontPreparation.h"
#include "MapCache.h"
#include "FontFallback.h"
#include "service/MapInventory.h"

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

int main(int argc, char** argv) {
	if (argc != 4) { std::cerr << "Usage: map_font_units CJK.ttf seed.txt Latin.ttf\n"; return 2; }
	std::ifstream input(argv[1], std::ios::binary);
	auto source = std::make_shared<std::vector<unsigned char>>(
		std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	if (source->empty()) { std::cerr << "CJK test font is missing\n"; return 2; }
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
		atlas.setSources({source, source, latin});
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
		check(atlas.prepare(profiles, upload), "known map names prepare successfully");
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
				[&] { return atlas.prepare(profiles, upload); }, publish);
		};
		failUpload = true;
		check(!advance(), "texture failure keeps map pending");
		check(!inventory.getMapInfo("zh", 9999) && !inventory.isLocaleLoaded("zh"),
			"neither map nor locale is visible before glyph upload");
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
		for (int i = 0; i < 100; ++i) atlas.prepare(profiles, upload);
		check(uploads == uploadsAfterMap, "ordinary frames do not rebuild pages");
		// A less common character absent from the original seed follows the same path.
		atlas.addText("龘");
		failAfterUploads = uploads + 1;
		check(!atlas.prepare(profiles, upload) && atlas.find(28, false, 0x9F98)
			&& !atlas.find(72, false, 0x9F98), "partial upload keeps completed profiles without claiming others");
		const int partialUploads = uploads;
		failAfterUploads = -1;
		check(atlas.prepare(profiles, upload) && uploads == partialUploads + 2
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
		check(restarted.prepare({{72, false}}, upload) && restarted.find(72, false, 0x5146),
			"restart preloads zhao from cached map names");
		atlas.prepare({{28, false}}, upload);
		check(!atlas.find(72, false, 0x5146), "unused sizes are retired at frame boundary");
		atlas.clear();
		restarted.clear();
		check(liveTextures == 0, "unload releases all private textures");
		atlas.setSources({source});
		atlas.addText("\xCD\xB8"); // U+0378 is unassigned, with no real font glyph.
		check(!atlas.prepare({{72, false}}, upload) && !atlas.find(72, false, 0x0378)
			&& atlas.error().find("0378") != std::string::npos,
			"unavailable glyph reports its codepoint instead of accepting a fallback question mark");
		atlas.clear();
	}
	{
		std::ifstream seedFile(argv[2], std::ios::binary);
		std::string seed(std::istreambuf_iterator<char>(seedFile), {});
		MapGlyphAtlas atlas;
		atlas.setSources({source, source, latin});
		atlas.addText(seed);
		const auto start = std::chrono::steady_clock::now();
		check(atlas.prepare({{20, false}, {28, false}, {72, false}, {28, true}, {72, true}},
			[](const unsigned char*, int, int) -> MapGlyphAtlas::Texture { return {new int(1), [](void* p) { delete static_cast<int*>(p); }}; }),
			"entire bundled name seed rasterizes at default map sizes");
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		std::cout << "MEASURE bundled private pages=" << atlas.pageCount()
			<< " RGBA_bytes=" << atlas.textureBytes() << " CPU_ms=" << ms << '\n';
		if (!atlas.error().empty()) std::cout << "ATLAS_ERROR " << atlas.error() << '\n';
	}
	ImGui::DestroyContext();
	std::cout << "Map font tests: " << passes << " passed, " << failures << " failed\n";
	return failures ? 1 : 0;
}
