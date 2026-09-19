#pragma once

#include "../MapGlyphAtlas.h"
#include <chrono>

class MapFontService {
public:
	void initialize(const std::string& folder);
	void addText(const std::string& text) { atlas.addText(text); }
	bool prepare();
	ImFont* find(float size, bool animation, ImWchar character) const;
	bool ready() const { return prepared; }
	void clear();
private:
	MapGlyphAtlas atlas;
	bool hasSource = false;
	bool hasAnimationFace = false;
	bool prepared = false;
	std::chrono::steady_clock::time_point retryAt{};
};

extern MapFontService mapFonts;
