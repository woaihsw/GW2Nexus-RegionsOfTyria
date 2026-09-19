// ImGui's stb implementation has internal linkage. Use the same parser for
// cheap cmap checks, without building a bitmap to discover an absent glyph.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "MapGlyphAtlas.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#undef STB_TRUETYPE_IMPLEMENTATION
#undef STBTT_STATIC
#include "MapGlyphPolicy.h"
#include "imgui/imgui_internal.h"

#include <cmath>

void MapGlyphAtlas::setSources(std::vector<FontData> data) {
	clear();
	for (auto& bytes : data) {
		if (!bytes || bytes->size() < 12) continue;
		Source source{std::move(bytes)};
		const int offset = stbtt_GetFontOffsetForIndex(source.data->data(), 0);
		if (offset < 0 || static_cast<size_t>(offset) >= source.data->size()) continue;
		if (stbtt_InitFont(&source.info, source.data->data(), offset)) sources.push_back(std::move(source));
	}
}

bool MapGlyphAtlas::supports(ImWchar character) {
	const auto known = coverage.find(character);
	if (known != coverage.end()) return known->second;
	bool available = false;
	for (const auto& source : sources) {
		if (stbtt_FindGlyphIndex(&source.info, character)) { available = true; break; }
	}
	coverage.emplace(character, available);
	return available;
}

bool MapGlyphAtlas::stage(ImWchar character) {
	if (!supports(character)) return false;
	if (!characters.contains(character)) dirty = staged.insert(character).second || dirty;
	return true;
}

bool MapGlyphAtlas::addText(const std::string& text) {
	bool available = true;
	for (ImWchar c : mapTextCharacters(text)) available = stage(c) && available;
	return available;
}

MapGlyphAtlas::Preparation MapGlyphAtlas::prepare(const std::set<Profile>& profiles, const Upload& upload, size_t pageBudget) {
	if (!dirty && profiles == preparedProfiles) return Preparation::Ready;
	dirty = true;
	lastError.clear();
	std::set<ImWchar> requested = characters;
	requested.insert(staged.begin(), staged.end());
	for (const auto& profile : profiles) {
		if (!std::isfinite(profile.size) || profile.size <= 0 || profile.size > 512) {
			lastError = "Invalid map font size";
			return Preparation::Failed;
		}
		auto& target = fonts[profile];
		std::vector<ImWchar> missing;
		for (ImWchar c : requested)
			if (!target.glyphs.contains(c)) missing.push_back(c);
		if (missing.empty()) continue;
		if (pageBudget == 0) return Preparation::Pending;
		if (sources.empty()) {
			lastError = "No system CJK font is available";
			return Preparation::Failed;
		}

		ImFontGlyphRangesBuilder builder;
		// ImGui needs a fallback even when a source lacks every requested glyph.
		builder.AddChar(' ');
		builder.AddChar('?');
		for (ImWchar c : missing) builder.AddChar(c);
		ImVector<ImWchar> ranges;
		builder.BuildRanges(&ranges);
		Page page;
		page.atlas = std::make_unique<ImFontAtlas>();
		page.atlas->Flags = ImFontAtlasFlags_NoMouseCursors | ImFontAtlasFlags_NoBakedLines
			| ImFontAtlasFlags_NoPowerOfTwoHeight;
		// ImGui's default minimum width is 512px, wasteful for a one-glyph page.
		int width = 128;
		const float estimatedWidth = std::sqrt(static_cast<float>(missing.size() + 2)) * profile.size;
		while (width < estimatedWidth && width < 4096) width *= 2;
		page.atlas->TexDesiredWidth = width;
		ImFont* font = nullptr;
		for (size_t i = 0; i < sources.size(); ++i) {
			// The second system face supplies the existing animation variation.
			size_t index = i;
			if (profile.animation && sources.size() > 1 && i < 2) index = 1 - i;
			const auto& data = sources[index].data;
			ImFontConfig config;
			// In ImGui 1.80, passing false copies the entire font file. Borrow
			// our stable source buffer instead, then revoke atlas ownership.
			config.FontDataOwnedByAtlas = true;
			config.OversampleH = config.OversampleV = 1;
			config.MergeMode = font != nullptr;
			ImFont* added = page.atlas->AddFontFromMemoryTTF(data->data(),
				static_cast<int>(data->size()), profile.size, &config, ranges.Data);
			page.atlas->ConfigData.back().FontDataOwnedByAtlas = false;
			if (!font) font = added;
		}
		++builds;
		if (!font || !page.atlas->Build()) {
			lastError = "Could not build map glyph page";
			return Preparation::Failed;
		}
		for (ImWchar c : missing) {
			if (!font->FindGlyphNoFallback(c)) {
				lastError = "System fonts lack map character U+";
				char value[16];
				ImFormatString(value, sizeof(value), "%04X", static_cast<unsigned>(c));
				lastError += value;
				return Preparation::Failed;
			}
		}
		unsigned char* pixels = nullptr;
		int textureWidth = 0, height = 0;
		page.atlas->GetTexDataAsRGBA32(&pixels, &textureWidth, &height);
		page.texture = upload(pixels, textureWidth, height);
		if (!page.texture) {
			lastError = "Could not upload map glyph page";
			return Preparation::Failed;
		}
		page.atlas->SetTexID(page.texture.get());
		page.bytes = static_cast<size_t>(textureWidth) * height * 4;
		page.atlas->ClearTexData();
		page.atlas->ClearInputData();
		for (ImWchar c : missing) target.glyphs.emplace(c, font);
		target.pages.push_back(std::move(page));
		--pageBudget;
	}
	// Only retire old profiles after every replacement page is usable. Failed or
	// budget-limited attempts keep old glyphs, texture IDs and UVs alive.
	for (auto it = fonts.begin(); it != fonts.end();) {
		if (!profiles.contains(it->first)) it = fonts.erase(it);
		else ++it;
	}
	characters = std::move(requested);
	staged.clear();
	preparedProfiles = profiles;
	dirty = false;
	return Preparation::Ready;
}

ImFont* MapGlyphAtlas::find(float size, bool animation, ImWchar character) const {
	const auto profile = fonts.find({ size, animation });
	if (profile == fonts.end()) return nullptr;
	const auto glyph = profile->second.glyphs.find(character);
	return glyph == profile->second.glyphs.end() ? nullptr : glyph->second;
}

ImFont* MapGlyphAtlas::findPrevious(float size, bool animation, ImWchar character) const {
	ImFont* nearest = nullptr;
	float distance = 0;
	for (const auto& profile : preparedProfiles) {
		if (profile.animation != animation) continue;
		ImFont* candidate = find(profile.size, animation, character);
		if (candidate && (!nearest || std::abs(profile.size - size) < distance)) {
			nearest = candidate;
			distance = std::abs(profile.size - size);
		}
	}
	return nearest;
}

void MapGlyphAtlas::clear() {
	fonts.clear();
	characters.clear();
	staged.clear();
	coverage.clear();
	sources.clear();
	lastError.clear();
	dirty = true;
	builds = 0;
	preparedProfiles.clear();
}

size_t MapGlyphAtlas::pageCount() const {
	size_t result = 0;
	for (const auto& [profile, entry] : fonts) result += entry.pages.size();
	return result;
}

size_t MapGlyphAtlas::textureBytes() const {
	size_t result = 0;
	for (const auto& [profile, entry] : fonts)
		for (const auto& page : entry.pages) result += page.bytes;
	return result;
}
