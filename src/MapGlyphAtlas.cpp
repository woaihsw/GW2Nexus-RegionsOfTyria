#include "MapGlyphAtlas.h"
#include "imgui/imgui_internal.h"

#include <cmath>

void MapGlyphAtlas::setSources(std::vector<FontData> data) {
	clear();
	sources = std::move(data);
}

void MapGlyphAtlas::addText(const std::string& text) {
	for (const char* p = text.c_str(); *p;) {
		unsigned int codepoint = 0;
		const int length = ImTextCharFromUtf8(&codepoint, p, nullptr);
		if (length <= 0) break;
		p += length;
		if (codepoint >= 0x80 && codepoint <= IM_UNICODE_CODEPOINT_MAX)
			dirty = characters.insert(static_cast<ImWchar>(codepoint)).second || dirty;
	}
}

bool MapGlyphAtlas::prepare(const std::set<Profile>& profiles, const Upload& upload) {
	if (!dirty && profiles == preparedProfiles) return true;
	dirty = true;
	lastError.clear();
	// Called before NewFrame, when no draw commands refer to retired sizes.
	for (auto it = fonts.begin(); it != fonts.end();) {
		if (!profiles.contains(it->first)) it = fonts.erase(it);
		else ++it;
	}
	for (const auto& profile : profiles) {
		if (!std::isfinite(profile.size) || profile.size <= 0 || profile.size > 512) {
			lastError = "Invalid map font size";
			return false;
		}
		auto& target = fonts[profile];
		std::vector<ImWchar> missing;
		for (ImWchar c : characters)
			if (!target.glyphs.contains(c)) missing.push_back(c);
		if (missing.empty()) continue;
		if (sources.empty()) {
			lastError = "No system CJK font is available";
			return false;
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
			const auto& data = sources[index];
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
		if (!font || !page.atlas->Build()) {
			lastError = "Could not build map glyph page";
			return false;
		}
		for (ImWchar c : missing) {
			if (!font->FindGlyphNoFallback(c)) {
				lastError = "System fonts lack map character U+";
				char value[16];
				ImFormatString(value, sizeof(value), "%04X", static_cast<unsigned>(c));
				lastError += value;
				return false;
			}
		}
		unsigned char* pixels = nullptr;
		int textureWidth = 0, height = 0;
		page.atlas->GetTexDataAsRGBA32(&pixels, &textureWidth, &height);
		page.texture = upload(pixels, textureWidth, height);
		if (!page.texture) {
			lastError = "Could not upload map glyph page";
			return false;
		}
		page.atlas->SetTexID(page.texture.get());
		page.bytes = static_cast<size_t>(textureWidth) * height * 4;
		page.atlas->ClearTexData();
		page.atlas->ClearInputData();
		for (ImWchar c : missing) target.glyphs.emplace(c, font);
		target.pages.push_back(std::move(page));
	}
	preparedProfiles = profiles;
	dirty = false;
	return true;
}

ImFont* MapGlyphAtlas::find(float size, bool animation, ImWchar character) const {
	const auto profile = fonts.find({ size, animation });
	if (profile == fonts.end()) return nullptr;
	const auto glyph = profile->second.glyphs.find(character);
	return glyph == profile->second.glyphs.end() ? nullptr : glyph->second;
}

void MapGlyphAtlas::clear() {
	fonts.clear();
	characters.clear();
	sources.clear();
	lastError.clear();
	dirty = true;
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
