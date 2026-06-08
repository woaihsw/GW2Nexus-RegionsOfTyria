#ifndef RENDERER_SERVICE_H
#define RENDERER_SERVICE_H

#include "../Globals.h"
#include "CurrentMapService.h"
#include <thread>
#include <mutex>

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

	void changeCurrentCharacter(std::string currentCharacter);
	void unload();

private:
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
	ImFont* getLoadedFont(const std::string& name);
	ImFont* getRenderableFontForText(ImFont* preferred, ImFont* fallback, const char* text);
	ImFont* getRenderableFontForCharacter(ImFont* preferred, ImFont* fallback, ImWchar character);
	float calculateRenderedTextWidth(ImFont* preferred, ImFont* fallback, const char* text);

};

#endif
