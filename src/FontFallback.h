#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "MapGlyphPolicy.h"

#include <initializer_list>

using MapGlyphLookup = ImFont* (*)(float, bool, ImWchar);

// Borrow fonts from either atlas; never resize them or mutate their metrics.
struct FallbackTextFont {
	ImFont* preferred;
	ImFont* alternate;
	ImFont* nexus;
	ImFont* nexusDefault;
	float size;
	MapGlyphLookup mapGlyph = nullptr;
	bool animation = false;
	ImFont* sharedFallback = nullptr;

	ImFont* hostGlyph(ImWchar character) const {
		for (ImFont* font : { preferred, alternate, nexus, nexusDefault, sharedFallback })
			if (font && font->IsLoaded() && font->FindGlyphNoFallback(character)) return font;
		return nullptr;
	}

	ImFont* forGlyph(ImWchar character) const {
		// Native-size non-Latin glyphs take precedence over scaled host fallbacks.
		if (needsNativeMapGlyph(character) && mapGlyph) {
			if (ImFont* font = mapGlyph(size, animation, character)) return font;
		}
		if (ImFont* font = hostGlyph(character)) return font;
		if (mapGlyph) {
			if (ImFont* font = mapGlyph(size, animation, character)) return font;
		}
		return nullptr;
	}

	bool covers(const char* text) const {
		for (const char* p = text; *p;) {
			int length = 0;
			const ImWchar c = decode(p, length);
			if (length <= 0) return false;
			p += length;
			if (c >= 0x80 && !forGlyph(c)) return false;
		}
		return true;
	}

	static ImWchar decode(const char* text, int& length) {
		unsigned int character = 0;
		length = ImTextCharFromUtf8(&character, text, nullptr);
		return static_cast<ImWchar>(character);
	}

	float advance(ImWchar character) const {
		ImFont* font = forGlyph(character);
		return font != nullptr ? font->GetCharAdvance(character) * size / font->FontSize : 0.0f;
	}

	ImVec2 measure(const char* text) const {
		ImVec2 result(0.0f, size);
		float x = 0.0f;
		for (const char* p = text; *p;) {
			int length = 0;
			const ImWchar character = decode(p, length);
			p += length;
			if (character == '\r') continue;
			if (character == '\n') {
				result.x = ImMax(result.x, x);
				x = 0.0f;
				result.y += size;
			}
			else {
				x += advance(character);
			}
		}
		result.x = ImMax(result.x, x);
		return result;
	}

	void drawGlyph(ImDrawList* drawList, ImVec2 position, ImU32 color,
		ImWchar character, const char* begin, const char* end) const {
		ImFont* font = forGlyph(character);
		if (font == nullptr) return;
		drawList->PushTextureID(font->ContainerAtlas->TexID);
		drawList->AddText(font, size, position, color, begin, end);
		drawList->PopTextureID();
	}

	void draw(ImDrawList* drawList, ImVec2 position, ImU32 color, const char* text) const {
		const float startX = position.x;
		for (const char* p = text; *p;) {
			int length = 0;
			const ImWchar character = decode(p, length);
			if (character == '\n') {
				position.x = startX;
				position.y += size;
			}
			else if (character != '\r') {
				drawGlyph(drawList, position, color, character, p, p + length);
				position.x += advance(character);
			}
			p += length;
		}
	}
};
