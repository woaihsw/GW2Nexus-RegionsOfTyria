#ifndef RENDERER_SERVICE_H
#define RENDERER_SERVICE_H

#include "../Globals.h"
#include "CurrentMapService.h"
#include "../PopupAnimation.h"
#include <chrono>
#include <optional>

class Renderer {
public:
	Renderer();
	~Renderer();
	void preRender(ImGuiIO& io);
	void render();
	void postRender(ImGuiIO& io);

	void clearFonts();
	bool isCleared();
	void registerFont(std::string name, ImFont* font);
	void setRacialFont(Mumble::ERace race);
	void setGenericFont();
	void updateFontSettings();
	bool hostGlyphAvailable(float size, bool animation, ImWchar character) const;
	uint64_t fontRevision() const { return fontsRevision; }

	void changeCurrentCharacter(std::string currentCharacter);
	void unload();

private:
	uint64_t fontsRevision = 0;
	std::map<std::string, ImFont*> fonts;
	ImFont* fontLarge = nullptr;
	ImFont* fontSmall = nullptr;
	ImFont* fontAnimLarge = nullptr;
	ImFont* fontAnimSmall = nullptr;
	ImFont* fontWidget = nullptr;

	/* Render subfunctions */
	void renderSampleInfo();
	void renderSectorInfo();
	void renderMinimapWidget();
	void renderInfo(float opacityOverride, bool useSampleText);
	void renderDebugInfo();

	void renderTextAnimation(const char* text, float opacityOverride, bool large, bool isShadow);
	void centerText(std::string text, float textY, float opacityOverride);
	void centerTextSmall(std::string text, float textY, float opacityOverride);

};

extern Renderer renderer;

#endif
