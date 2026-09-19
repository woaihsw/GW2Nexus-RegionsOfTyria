#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <set>
#include <string>

// Scripts used in CJK names need native-size glyphs, even when the host has a
// smaller copy. Other characters may use the same host fallbacks as the renderer.
inline bool needsNativeMapGlyph(ImWchar c) {
	return (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7AF)
		|| (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE10 && c <= 0xFE1F)
		|| (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFFEF);
}

inline std::set<ImWchar> mapTextCharacters(const std::string& text) {
	std::set<ImWchar> result;
	for (const char* p = text.c_str(); *p;) {
		unsigned int c = 0;
		const int length = ImTextCharFromUtf8(&c, p, nullptr);
		if (length <= 0) break;
		p += length;
		if (c >= 0x80 && c <= IM_UNICODE_CODEPOINT_MAX) result.insert(static_cast<ImWchar>(c));
	}
	return result;
}
