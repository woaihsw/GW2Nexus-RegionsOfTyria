#include "AddonRenderer.h"
#include <fstream>

#include "AddonInitalize.h"

using json = nlohmann::json;

std::string replacePlaceholderTexts(std::string text, bool useSampleText);

static void TextColoredUnformatted(const ImVec4& color, const char* text) {
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();
}

std::optional<std::chrono::steady_clock::time_point> popupAnimationStart;
float opacity = 0.0f;

CurrentMapService currentMapService = CurrentMapService();
int currentSectorId = -1;
int currentMapId = -1;
std::string currentCharacter = "";
Mumble::ERace currentRace;
RacialFontSettings* fontSettings = nullptr;
bool fontsPicked;

bool fontsLoaded = false;
int expectedFontCount = 30;

static ImWchar decodeUtf8Character(const char* text, int& charLength) {
	charLength = 1;
	unsigned char c = static_cast<unsigned char>(*text);

	if (c < 0x80) {
		return c;
	}

	auto isContinuation = [](const char* p) {
		return *p != '\0' && (static_cast<unsigned char>(*p) & 0xC0) == 0x80;
	};

	if ((c & 0xE0) == 0xC0 && isContinuation(text + 1)) {
		charLength = 2;
		ImWchar character = static_cast<ImWchar>((c & 0x1F) << 6);
		character |= static_cast<unsigned char>(text[1]) & 0x3F;
		return character;
	}

	if ((c & 0xF0) == 0xE0 && isContinuation(text + 1) && isContinuation(text + 2)) {
		charLength = 3;
		ImWchar character = static_cast<ImWchar>((c & 0x0F) << 12);
		character |= (static_cast<unsigned char>(text[1]) & 0x3F) << 6;
		character |= static_cast<unsigned char>(text[2]) & 0x3F;
		return character;
	}

	if ((c & 0xF8) == 0xF0 && isContinuation(text + 1) && isContinuation(text + 2) && isContinuation(text + 3)) {
		charLength = 4;
		ImWchar character = static_cast<ImWchar>((c & 0x07) << 18);
		character |= (static_cast<unsigned char>(text[1]) & 0x3F) << 12;
		character |= (static_cast<unsigned char>(text[2]) & 0x3F) << 6;
		character |= static_cast<unsigned char>(text[3]) & 0x3F;
		return character;
	}

	return c;
}

static bool fontCanRenderText(ImFont* font, const char* text) {
	if (font == nullptr || !font->IsLoaded()) return false;

	for (const char* p = text; *p;) {
		int charLength = 1;
		ImWchar character = decodeUtf8Character(p, charLength);
		if (font->FindGlyphNoFallback(character) == nullptr) {
			return false;
		}
		p += charLength;
	}

	return true;
}

static bool isCoreFont(const std::string& name) {
	return name != fontNameCjkSmall &&
		name != fontNameCjkLarge &&
		name != fontNameCjkWidget &&
		name != fontNameCjkAnimSmall &&
		name != fontNameCjkAnimLarge;
}

Renderer::Renderer() {}
Renderer::~Renderer() {}


void Renderer::changeCurrentCharacter(std::string c) {
	if (currentCharacter != c) {
		currentCharacter = c;
		currentSectorId = -1;
		currentMapId = -1;
		fontsPicked = false;
		popupAnimationStart.reset();
		opacity = 0.0f;
	}
}

void Renderer::unload() {
	popupAnimationStart.reset();
	opacity = 0.0f;
}

bool Renderer::isCleared() {
	return fonts.size() == 0;
}

void Renderer::clearFonts() {
	// set flag that *hopefully* stops rendering the fonts
	fontsLoaded = false;
	fontsPicked = false;

	// reset the font selection so on the next loop it will default to Nexus fonts fallback
	fontLarge = nullptr;
	fontSmall = nullptr;
	fontAnimLarge = nullptr;
	fontAnimSmall = nullptr;

	// clear the fonts map
	fonts.clear();
}

void Renderer::registerFont(std::string name, ImFont* font) {
#ifndef NDEBUG
	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Registering font: " + name).c_str());
	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, font->GetDebugName());
#endif
	if (fonts.contains(name)) {
		fonts[name] = font;
		// we receive a font update so update font settings
		updateFontSettings();
	}
	else {
		fonts.emplace(name, font);
	}

	int coreFontCount = 0;
	for (const auto& font : fonts) {
		if (isCoreFont(font.first)) {
			coreFontCount++;
		}
	}

	bool allCoreFontsLoaded = coreFontCount >= expectedFontCount;
	if (allCoreFontsLoaded && !fontsLoaded) {
		APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "All fonts loaded and registered with the renderer.");
	}
	fontsLoaded = allCoreFontsLoaded;
}

ImFont* Renderer::getLoadedFont(const std::string& name) {
	if (!fonts.contains(name)) return nullptr;
	ImFont* font = fonts[name];
	if (font == nullptr || !font->IsLoaded()) return nullptr;
	return font;
}

ImFont* Renderer::getRenderableFontForText(ImFont* preferred, ImFont* fallback, const char* text) {
	if (fontCanRenderText(preferred, text)) return preferred;
	if (fontCanRenderText(fallback, text)) return fallback;
	return preferred != nullptr ? preferred : fallback;
}

ImFont* Renderer::getRenderableFontForCharacter(ImFont* preferred, ImFont* fallback, ImWchar character) {
	if (preferred != nullptr && preferred->IsLoaded() && preferred->FindGlyphNoFallback(character) != nullptr) {
		return preferred;
	}
	if (fallback != nullptr && fallback->IsLoaded() && fallback->FindGlyphNoFallback(character) != nullptr) {
		return fallback;
	}
	return preferred != nullptr ? preferred : fallback;
}

float Renderer::calculateRenderedTextWidth(ImFont* preferred, ImFont* fallback, const char* text) {
	if (text == nullptr) return 0.0f;

	float width = 0.0f;
	float scaling = NexusLink != nullptr && NexusLink->Scaling > 0.0f ? NexusLink->Scaling : 1.0f;
	for (const char* p = text; *p;) {
		int charLength = 1;
		ImWchar character = decodeUtf8Character(p, charLength);
		ImFont* renderFont = getRenderableFontForCharacter(preferred, fallback, character);

		if (renderFont != nullptr) {
			ImGui::PushFont(renderFont);
			width += ImGui::CalcTextSize(p, p + charLength).x * scaling;
			ImGui::PopFont();
		}

		p += charLength;
	}

	return width;
}

void Renderer::updateFontSettings() {
	// update wrapper so the options dialog does not need knowledge about currentRace
	setRacialFont(currentRace);
}

void Renderer::setGenericFont() {
	fontSettings = &settings.fontSettings[0];
	fontLarge = fonts[fontNameGenericLarge];
	fontSmall = fonts[fontNameGenericSmall];
	fontAnimLarge = fonts[fontNameGenericAnimLarge];
	fontAnimSmall = fonts[fontNameGenericAnimSmall];
	fontWidget = fonts[fontNameGenericWidget];
}

void Renderer::setRacialFont(Mumble::ERace race) {
	// set default values
	currentRace = race;
	// if we haven't been supplied with fonts leave here to avoid nullpointers
	if (!fontsLoaded) { return; }

	if (settings.fontMode == 1) {
		setGenericFont();
	}
	// if fontmode == 0 => pick font according to race; else fontmode tells us which race the user wants, starting with 2 = asura to 6 = sylvari
	else if ((settings.fontMode == 0 && race == Mumble::ERace::Asura) || settings.fontMode == 2) {
		fontLarge = this->fonts[fontNameAsuraLarge];
		fontSmall = this->fonts[fontNameAsuraSmall];

		fontAnimLarge = this->fonts[fontNameAsuraAnimLarge];
		fontAnimSmall = this->fonts[fontNameAsuraAnimSmall];

		fontSettings = &settings.fontSettings[1];
	}
	else if ((settings.fontMode == 0 && race == Mumble::ERace::Charr) || settings.fontMode == 3) {
		fontLarge = this->fonts[fontNameCharrLarge];
		fontSmall = this->fonts[fontNameCharrSmall];
		
		fontAnimLarge = this->fonts[fontNameCharrAnimLarge];
		fontAnimSmall = this->fonts[fontNameCharrAnimSmall];

		fontSettings = &settings.fontSettings[2];
	}
	else if ((settings.fontMode == 0 && race == Mumble::ERace::Human) || settings.fontMode == 4) {
		fontLarge = this->fonts[fontNameHumanLarge];
		fontSmall = this->fonts[fontNameHumanSmall];
		
		fontAnimLarge = this->fonts[fontNameHumanAnimLarge];
		fontAnimSmall = this->fonts[fontNameHumanAnimSmall];
		
		fontSettings = &settings.fontSettings[3];
	}
	else if ((settings.fontMode == 0 && race == Mumble::ERace::Norn) || settings.fontMode == 5) {
		fontLarge = this->fonts[fontNameNornLarge];
		fontSmall = this->fonts[fontNameNornSmall];
		
		fontAnimLarge = this->fonts[fontNameNornAnimLarge];
		fontAnimSmall = this->fonts[fontNameNornAnimSmall];

		fontSettings = &settings.fontSettings[4];
	}
	else if ((settings.fontMode == 0 && race == Mumble::ERace::Sylvari) || settings.fontMode == 6) {
		fontLarge = this->fonts[fontNameSylvariLarge];
		fontSmall = this->fonts[fontNameSylvariSmall];
		
		fontAnimLarge = this->fonts[fontNameSylvariAnimLarge];
		fontAnimSmall = this->fonts[fontNameSylvariAnimSmall];

		fontSettings = &settings.fontSettings[5];
	}

	// Widget Font Mode
	if (settings.widgetFontMode == 1) {
		fontWidget = this->fonts[fontNameGenericWidget];
	}
	// if fontmode == 0 => pick font according to race; else fontmode tells us which race the user wants, starting with 2 = asura to 6 = sylvari
	else if ((settings.widgetFontMode == 0 && race == Mumble::ERace::Asura) || settings.widgetFontMode == 2) {
		fontWidget = this->fonts[fontNameAsuraWidget];

	}
	else if ((settings.widgetFontMode == 0 && race == Mumble::ERace::Charr) || settings.widgetFontMode == 3) {
		fontWidget = this->fonts[fontNameCharrWidget];
	}
	else if ((settings.widgetFontMode == 0 && race == Mumble::ERace::Human) || settings.widgetFontMode == 4) {
		fontWidget = this->fonts[fontNameHumanWidget];
	}
	else if ((settings.widgetFontMode == 0 && race == Mumble::ERace::Norn) || settings.widgetFontMode == 5) {
		fontWidget = this->fonts[fontNameNornWidget];
	}
	else if ((settings.widgetFontMode == 0 && race == Mumble::ERace::Sylvari) || settings.widgetFontMode == 6) {
		fontWidget = this->fonts[fontNameSylvariWidget];
	}

#ifndef NDEBUG
	if(fontLarge != nullptr) 
		APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("FontLarge: " + std::string(fontLarge->GetDebugName())).c_str());
	if(fontSmall != nullptr)
		APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("FontSmall: " + std::string(fontSmall->GetDebugName())).c_str());
	if (fontWidget != nullptr)
		APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("FontWidget: " + std::string(fontWidget->GetDebugName())).c_str());
	if(fontAnimLarge != nullptr)
		APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("FontAnimLarge: " + std::string(fontAnimLarge->GetDebugName())).c_str());
	if(fontAnimSmall != nullptr) 
		APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("FontAnimSmall: " + std::string(fontAnimSmall->GetDebugName())).c_str());
#endif

	fontsPicked = true;
}

void Renderer::preRender(ImGuiIO& io) {
	// NO OP
}

void Renderer::postRender(ImGuiIO& io) {
	// NO OP
}

void Renderer::render() {
	if (unloading.load()) return;
	try {
		renderSampleInfo();
		renderSectorInfo();
		renderMinimapWidget();
		renderDebugInfo();
	}
	catch (const std::exception& e) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Exception in render.");
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, e.what());
	}
	catch (...) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Unknown exception in render.");
	}
}

void Renderer::renderSampleInfo() {

	bool renderSample = false;
	int raceIndex = 0;
	for (int i = 0; i < 6; i++) {
		if (showTemplate[i]) {
			raceIndex = i;
			renderSample = true;
			break;
		}
	}
	
	if (!renderSample) return;

	// Store current race
	Mumble::ERace originalPick = currentRace;

	// switch to template race set by options dialog
	switch (raceIndex) {
		case 0: setGenericFont(); break;
		case 1: setRacialFont(Mumble::ERace::Asura); break;
		case 2: setRacialFont(Mumble::ERace::Charr); break;
		case 3: setRacialFont(Mumble::ERace::Human); break;
		case 4: setRacialFont(Mumble::ERace::Norn); break;
		case 5: setRacialFont(Mumble::ERace::Sylvari); break;
		default: setGenericFont();
	}

	// sanity check
	if (fontSettings == nullptr) {
#ifndef NDEBUG
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Could not find font settings, possibly still initializing.");
#endif
		setRacialFont(originalPick);
		return;
	}

	// render info with generic texts
	renderInfo(1.0f, true);

	// reset to the originalPick
	setRacialFont(originalPick);
}

void Renderer::renderMinimapWidget() {
	if (!settings.widgetEnabled) return;
	if (!NexusLink->IsGameplay) return;
	if (MumbleLink->Context.IsMapOpen) return;
	
	MapData* currentMap = currentMapService.getCurrentMap();
	if (currentMap == nullptr) return;
	if (fontSettings == nullptr && !fontsPicked) {
		setRacialFont(currentRace);
	} else if (!fontsPicked) {
		setRacialFont(currentRace);
	}
	if (fontSettings == nullptr) {
#ifndef NDEBUG
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Could not find font settings, possibly still initializing.");
#endif
		return;
	}

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar;

	if (fontWidget == nullptr) {
		fontWidget = (ImFont*)NexusLink->FontBig;
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontWidget null, fallback to Nexus default font.");
	}
	else if (!fontWidget->IsLoaded()) {
		fontWidget = (ImFont*)NexusLink->FontBig;
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontWidget not loaded, fallback to Nexus default font.");
	}

	// still not loaded - might occurr during font reload so skip now
	if (fontWidget == nullptr || !fontWidget->IsLoaded()) { fontsPicked = false; return; }

	std::string output = fontSettings->widgetDisplayFormat;
	output = replacePlaceholderTexts(output, false);
	ImFont* renderFontWidget = getRenderableFontForText(fontWidget, getLoadedFont(fontNameCjkWidget), output.c_str());
	if (renderFontWidget == nullptr) {
		renderFontWidget = fontWidget;
	}

	// calculate text size
	ImGui::PushFont(renderFontWidget);
	ImVec2 textSize = ImGui::CalcTextSize(output.c_str());
	ImGui::PopFont();
	ImVec4 textColor = ImVec4(fontSettings->widgetFontColor[0], fontSettings->widgetFontColor[1], fontSettings->widgetFontColor[2], 1.0f);
	ImVec4 shadowColor = ImVec4(fontSettings->fontBorderColor[0], fontSettings->fontBorderColor[1], fontSettings->fontBorderColor[2], 1);

	ImVec2 widgetPos = ImVec2(settings.widgetPositionX, settings.widgetPositionY);
	ImVec2 widgetSize = ImVec2(settings.widgetWidth, textSize.y);
	ImGui::SetNextWindowPos(widgetPos);
	ImGui::SetNextWindowSize(widgetSize);
	ImGui::SetNextWindowBgAlpha(settings.widgetBackgroundOpacity);

	if (ImGui::Begin("MiniSectorWidget", (bool*)0, flags)) {
		ImGui::PushFont(renderFontWidget);
		// alignment left - center - right
		float textX;
		switch (settings.widgetTextAlign) {
			case 0: textX = (settings.widgetWidth - textSize.x) / 2.0f; break; 
			case 1: textX = 1; break; // extra padding to the left
			case 2: textX = settings.widgetWidth - textSize.x - 1; break; // -1 = extra padding to the right
			default: textX = (settings.widgetWidth - textSize.x) / 2.0f;
		}

		switch (fontSettings->fontBorderMode) {
		case 0: // no border
			break; 
		case 1: // shadow
			ImGui::SetCursorPosX(textX + fontSettings->fontBorderOffset);
			ImGui::SetCursorPosY(1.0f);
			TextColoredUnformatted(shadowColor, output.c_str());
			break;
		case 2: // full border
			ImGui::SetCursorPosX(textX - fontSettings->fontBorderOffset);
			ImGui::SetCursorPosY(0.0f - fontSettings->fontBorderOffset);

			ImVec2 currentPos = ImGui::GetCursorPos();
			for (int x = 0; x <= fontSettings->fontBorderOffset * 2; x++)
			{
				for (int y = 0; y <= fontSettings->fontBorderOffset * 2; y++)
				{
					ImGui::SetCursorPos({ currentPos.x + static_cast<float>(x), currentPos.y + static_cast<float>(y) });
					TextColoredUnformatted(shadowColor, output.c_str());
				}
			}	
			break;
		}
		
		ImGui::SetCursorPosX(textX);
		ImGui::SetCursorPosY(0.0f);
		TextColoredUnformatted(textColor, output.c_str());
		ImGui::PopFont();
	}
	ImGui::End();
}

void Renderer::renderSectorInfo() {
	if (!settings.enablePopup) return; 
	if (settings.hidePopupInCombat && MumbleLink->Context.IsInCombat) return;
	if (settings.hidePopupInCompetitive && MumbleLink->Context.IsCompetitive) return;

	MapData* currentMap = currentMapService.getCurrentMap();
	if (currentMap == nullptr) return;
	if (fontSettings == nullptr && !fontsPicked) {
		setRacialFont(currentRace);
	}
	else if (!fontsPicked) {
		setRacialFont(currentRace);
	}
	if (fontSettings == nullptr) {
#ifndef NDEBUG
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Could not find font settings, possibly still initializing.");
#endif
		return;
	}

	if (currentSectorId != currentMap->currentSector.id // sector change
		|| currentMapId != currentMap->id) { // map change, we could technically end up in a sector with same id since I parse unknown sectors as -1
		
		currentMapId = currentMap->id;
		currentSectorId = currentMap->currentSector.id;

		APIDefs->Events.Raise("EV_TYRIAN_REGIONS_SECTOR_CHANGED", currentMap);
		popupAnimationStart = std::chrono::steady_clock::now();
	}

	if (!popupAnimationStart.has_value()) {
		return;
	}

	const float elapsed = std::chrono::duration<float>(
		std::chrono::steady_clock::now() - *popupAnimationStart).count();
	const PopupAnimationParams animation = PopupAnimationParams::fromSettings(
		settings.popupAnimationSpeed,
		settings.popupAnimationDuration);
	if (!PopupAnimationActive(elapsed, animation)) {
		popupAnimationStart.reset();
		opacity = 0.0f;
		return;
	}

	opacity = PopupOpacityAt(elapsed, animation);
	renderInfo(opacity, false);
}

void Renderer::renderInfo(float opacityOverride, bool useSampleText) {
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowSize = io.DisplaySize;
	// Make the next Window go over the entire screen for easier calculations
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
	// And make sure to disable all interaction with it properly *cough*
	if (ImGui::Begin("MapdetailsFrame", (bool*)0,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoMouseInputs))
	{

		std::string smallText = std::string(fontSettings->displayFormatSmall);
		std::string largeText = std::string(fontSettings->displayFormatLarge);
		if (largeText.empty()) largeText = "@s"; // default if empty

		smallText = replacePlaceholderTexts(smallText, useSampleText);
		largeText = replacePlaceholderTexts(largeText, useSampleText);

		if (!fontsPicked) {
			setRacialFont(currentRace);
		}
		// whether we picked fonts successfully or not, make sure they're loaded properly and fall back to nexus fonts in case they're not
		// Font fallback
		if (fontLarge == nullptr) {
			fontLarge = (ImFont*)NexusLink->FontBig;
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontLarge null, fallback to Nexus default font.");
		}
		else if (!fontLarge->IsLoaded()) {
			fontLarge = (ImFont*)NexusLink->FontBig;
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontLarge not loaded, fallback to Nexus default font.");
		}
		if (fontSmall == nullptr) {
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontSmall null, fallback to Nexus default font.");
			fontSmall = (ImFont*)NexusLink->Font;
		}
		else if (!fontSmall->IsLoaded()) {
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontSmall not loaded, fallback to Nexus default font.");
			fontSmall = (ImFont*)NexusLink->Font;
		}
		// Animation Font flalback
		if (fontAnimLarge == nullptr) {
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontAnimLarge null, fallback to Nexus default font.");
			fontAnimLarge = (ImFont*)NexusLink->FontBig;
		}
		else if (!fontAnimLarge->IsLoaded()) {
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontAnimLarge not loaded, fallback to Nexus default font.");
			fontAnimLarge = (ImFont*)NexusLink->FontBig;
		}
		if (fontAnimSmall == nullptr) {
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontAnimSmall null, fallback to Nexus default font.");
			fontAnimSmall = (ImFont*)NexusLink->Font;
		}
		else if (!fontAnimSmall->IsLoaded()) {
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "FontAnimSmall not loaded, fallback to Nexus default font.");
			fontAnimSmall = (ImFont*)NexusLink->Font;
		}

		// still not loaded - might occurr during font reload so skip now
		if (fontLarge == nullptr || !fontLarge->IsLoaded()
			|| fontSmall == nullptr || !fontSmall->IsLoaded()
			|| fontAnimLarge == nullptr || !fontAnimLarge->IsLoaded()
			|| fontAnimSmall == nullptr || !fontAnimSmall->IsLoaded()) {
			fontsPicked = false;
		}
		else {
			centerText(largeText, fontSettings->verticalPosition, opacityOverride);
			centerTextSmall(smallText, fontSettings->verticalPosition - fontSettings->spacing, opacityOverride);
		}
	}
	ImGui::End();
}

void Renderer::renderDebugInfo() {
	if (!showDebug) return;
	ImGuiIO& io = ImGui::GetIO();

	if(ImGui::Begin("TyrianRegionsDebug"))
	{
		ImGui::PushFont((ImFont*)NexusLink->Font);

		ImGui::Text("Player information");
		ImGui::TextUnformatted(("GlobalX: " + std::to_string(MumbleLink->Context.Compass.PlayerPosition.X)).c_str());
		ImGui::TextUnformatted(("GlobalY: " + std::to_string(MumbleLink->Context.Compass.PlayerPosition.Y)).c_str());

		gw2::coordinate calcPos = currentMapService.calculatePos();
		ImGui::TextUnformatted(("CalculatedX: " + std::to_string(calcPos.x)).c_str());
		ImGui::TextUnformatted(("CalculatedY: " + std::to_string(calcPos.y)).c_str());

		ImGui::Separator();
		ImGui::Text("Mumble Information");
		ImGui::TextUnformatted(("Map Id: " + std::to_string(MumbleLink->Context.MapID)).c_str());
		ImGui::TextUnformatted(("Competitive: " + std::to_string(MumbleLink->Context.IsCompetitive)).c_str());

		ImGui::Separator();
		ImGui::Text("Current Map Data");
		MapData* currentMap = currentMapService.getCurrentMap();
		if (currentMap == nullptr) {
			ImGui::TextColored(ImVec4(255, 0, 0, 1), "No map found in inventory!");
		}
		else {
			ImGui::TextUnformatted(("Map Id: " + std::to_string(currentMap->id)).c_str());
			ImGui::TextUnformatted(("Map Name: " + currentMap->name).c_str());
			ImGui::TextUnformatted(("Region Id: " + std::to_string(currentMap->regionId)).c_str());
			ImGui::TextUnformatted(("Region Name: " + currentMap->regionName).c_str());
			ImGui::TextUnformatted(("Continent Id: " + std::to_string(currentMap->continentId)).c_str());
			ImGui::TextUnformatted(("Continent Name: " + currentMap->continentName).c_str());
			ImGui::TextUnformatted(("Current Sector Id: " + std::to_string(currentMap->currentSector.id)).c_str());
			ImGui::TextUnformatted(("Current Sector Name: " + currentMap->currentSector.name).c_str());
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Map Inventory Data", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (currentMap == nullptr) {
				ImGui::TextColored(ImVec4(255, 0, 0, 1), "No map found in inventory!");
			}
			else {
				std::string locale = GetLocaleAsString(settings.locale);
				gw2::map* inventoryMap = mapInventory->getMapInfo(locale, currentMap->id);
				if (inventoryMap == nullptr) {
					ImGui::Text("Map not loaded");
				}
				else {
					ImGui::TextUnformatted(("Map Id: " + std::to_string(inventoryMap->id)).c_str());
					ImGui::TextUnformatted(("Map Name: " + inventoryMap->name).c_str());
					ImGui::TextUnformatted(("Region Id: " + std::to_string(inventoryMap->regionId)).c_str());
					ImGui::TextUnformatted(("Region Name: " + inventoryMap->regionName).c_str());
					ImGui::TextUnformatted(("Continent Id: " + std::to_string(inventoryMap->continentId)).c_str());
					ImGui::TextUnformatted(("Continent Name: " + inventoryMap->continentName).c_str());
					ImGui::TextUnformatted(("MinLevel: " + std::to_string(inventoryMap->minLevel)).c_str());
					ImGui::TextUnformatted(("MaxLevel: " + std::to_string(inventoryMap->maxLevel)).c_str());
					if (ImGui::CollapsingHeader("Sectors")) {
						for (auto sector : inventoryMap->sectors) {
							if (ImGui::CollapsingHeader((std::to_string(sector.second.id) + ": " + sector.second.name).c_str())) {
								json j = sector.second;
								ImGui::TextUnformatted(j.dump(4).c_str());
							}
						}
					}
				}
			}
		}
		ImGui::Separator();
		if (ImGui::CollapsingHeader("WvW Match Data", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (match == nullptr) {
				ImGui::TextColored(ImVec4(255, 0, 0, 1), "No match data loaded!");
			}
			else {
				ImGui::TextUnformatted(("Id: " + match->id).c_str());
				gw2api::worlds::world* red = worldInventory->getWorld(GetLocaleAsString(settings.locale), match->worlds.red);
				gw2api::worlds::world* blue = worldInventory->getWorld(GetLocaleAsString(settings.locale), match->worlds.blue);
				gw2api::worlds::world* green = worldInventory->getWorld(GetLocaleAsString(settings.locale), match->worlds.green);

				ImGui::TextUnformatted(("Red world: " + std::to_string(match->worlds.red)).c_str());
				if (red == nullptr) {
					ImGui::TextColored(ImVec4(255, 0, 0, 1), "Red Team unknown!");
				}
				else {
					ImGui::TextUnformatted(red->name.c_str());
				}

				ImGui::TextUnformatted(("Blue world: " + std::to_string(match->worlds.blue)).c_str());
				if (blue == nullptr) {
					ImGui::TextColored(ImVec4(255, 0, 0, 1), "Blue Team unknown!");
				}
				else {
					ImGui::TextUnformatted(blue->name.c_str());
				}

				ImGui::TextUnformatted(("Green world: " + std::to_string(match->worlds.green)).c_str());
				if (green == nullptr) {
					ImGui::TextColored(ImVec4(255, 0, 0, 1), "Green Team unknown!");
				}
				else {
					ImGui::TextUnformatted(green->name.c_str());
				}
			}
		}

		ImGui::PopFont();
	
		ImGui::Separator();
		std::string title = "Loaded fonts: " + std::to_string(fonts.size());
		if (ImGui::CollapsingHeader(title.c_str())) {
			
			for (auto font : fonts) {
				
				if (font.second == nullptr) {
					ImGui::PushFont((ImFont*)NexusLink->Font);
					const char* debugname = font.first.c_str();
					std::string message = "Error: Font is nullptr: " + std::string(debugname);
					ImGui::TextUnformatted(message.c_str());
					ImGui::PopFont();
				}
				else if (font.second->IsLoaded()) {

					std::string fontName = "Font name: " + std::string(font.second->ConfigData->Name) + ", Size : " + std::to_string(font.second->FontSize);
					ImGui::TextUnformatted(fontName.c_str());
					ImGui::PushFont(font.second);
					ImGui::Text("ABCDEFGHIJKLMNOPQRSTUVWXYZ_abdefghijklmnopqrstuvwxyz");
					ImGui::PopFont();
				}
				else {
					ImGui::PushFont((ImFont*)NexusLink->Font);
					const char* debugname = font.second->GetDebugName();
					std::string message = "Error: Font not loaded: " + std::string(debugname);
					ImGui::TextUnformatted(message.c_str());
					ImGui::PopFont();
				}
				ImGui::Separator();
			}
		}
	}
	ImGui::End();
}

void Renderer::renderTextAnimation(const char* text, float opacityOverride, bool large, bool isShadow) {
	ImFont* main = large ? fontLarge : fontSmall;
	ImFont* secondary = large ? fontAnimLarge : fontAnimSmall;

	if (settings.disableAnimations) {
		secondary = main; // :(
	}

	ImVec4 color = ImVec4(fontSettings->fontColor[0], fontSettings->fontColor[1], fontSettings->fontColor[2], opacityOverride);
	ImVec4 shadow = ImVec4(fontSettings->fontBorderColor[0], fontSettings->fontBorderColor[1], fontSettings->fontBorderColor[2], opacityOverride);

	ImVec2 originalCursorPos = ImGui::GetCursorPos();

	float font1Size = main->FontSize;
	float font2Size = secondary->FontSize;

	// Calculate the offset to align secondary to the center of main
	float yOffset = (font1Size - font2Size) * 0.5f;
	float currentX = originalCursorPos.x;

	// Calculate the scaling factor for font2
	float scalingFactor = font1Size / font2Size;
	if (scalingFactor == 0.0f) scalingFactor = 1.0f;

	float originalSecondaryScaling = secondary->Scale;
	secondary->Scale = scalingFactor;

	const char* p = text;
	mbstate_t state = mbstate_t(); // Initialize the conversion state

	for (const char* p = text; *p;) {
		int char_len = 1;
		ImWchar character = decodeUtf8Character(p, char_len);

		// Pick font based on opacity; lower opacity more favorably to secondary.
		bool useSecondary = !settings.disableAnimations && opacityOverride < 1.0f && ((float)rand() / RAND_MAX) > opacityOverride;
		ImFont* selectedFont = useSecondary ? secondary : main;
		ImFont* fallbackFont = getLoadedFont(large ? fontNameCjkLarge : fontNameCjkSmall);
		ImFont* fallbackAnimFont = getLoadedFont(large ? fontNameCjkAnimLarge : fontNameCjkAnimSmall);
		if (fallbackAnimFont == nullptr) {
			fallbackAnimFont = fallbackFont;
		}
		ImFont* selectedFallbackFont = useSecondary ? fallbackAnimFont : fallbackFont;
		if (selectedFallbackFont == nullptr) {
			selectedFallbackFont = large ? (ImFont*)NexusLink->FontBig : (ImFont*)NexusLink->Font;
		}
		selectedFont = getRenderableFontForCharacter(selectedFont, selectedFallbackFont, character);
		
		// Align height to center with main font
		ImGui::PushFont(selectedFont);
		if (selectedFont == secondary) {
			ImVec2 charPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(charPos.x, charPos.y + yOffset));
		}

		ImGui::PushStyleColor(ImGuiCol_Text, isShadow? shadow : color);
		ImGui::TextUnformatted(p, p + char_len);
		
		// draw outline
		if (isShadow && fontSettings->fontBorderMode == 2) {
			ImVec2 currentPos = ImGui::GetCursorPos();
			for (int x = 0; x <= fontSettings->fontBorderOffset * 2; x++)
			{
				for (int y = 0; y <= fontSettings->fontBorderOffset * 2; y++)
				{
					ImGui::SetCursorPos({ currentX + static_cast<float>(x), originalCursorPos.y + static_cast<float>(y) });
					ImGui::TextUnformatted(p, p + char_len);
				}
			}

			ImGui::SetCursorPos(currentPos);
		}
		
		ImGui::PopStyleColor();

		// reset position
		if (selectedFont == secondary) {
			ImVec2 charPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(charPos.x, charPos.y - yOffset));

		}

		ImGui::PopFont();
		
		if (p[char_len]) {	
			if (fallbackFont == nullptr) {
				fallbackFont = large ? (ImFont*)NexusLink->FontBig : (ImFont*)NexusLink->Font;
			}
			ImFont* advanceFont = getRenderableFontForCharacter(main, fallbackFont, character);
			ImGui::PushFont(advanceFont);
			currentX += ImGui::CalcTextSize(p, p + char_len).x * NexusLink->Scaling;

			ImGui::PopFont();
			ImGui::SetCursorPos(ImVec2(currentX, originalCursorPos.y));
		}

		p += char_len;
	}
	secondary->Scale = originalSecondaryScaling;
}

void Renderer::centerText(std::string text, float textY, float opacityOverride) {
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowSize = io.DisplaySize;

	if (text.empty() && currentCharacter == "I Facetank Bosses") {
		text = "Jesus fucking christ Delta, where are you now?";
	}
	else if (text.empty() && currentCharacter != "I Facetank Bosses") {
		text = "The Unknown";
	}

	ImFont* fallbackFont = getLoadedFont(fontNameCjkLarge);
	if (fallbackFont == nullptr) {
		fallbackFont = (ImFont*)NexusLink->FontBig;
	}
	float textX = (windowSize.x - calculateRenderedTextWidth(fontLarge, fallbackFont, text.c_str())) / 2.0f;

	int offset = fontSettings->fontBorderOffset;
	if (fontSettings->fontBorderMode == 0) {
		//No shadow
	}
	else if (fontSettings->fontBorderMode == 1) {
		ImGui::SetCursorScreenPos(ImVec2(textX + offset, textY + offset));
		renderTextAnimation(text.c_str(), opacityOverride, true, true);
	}
	else if (fontSettings->fontBorderMode == 2) {
		ImGui::SetCursorScreenPos(ImVec2(textX - offset, textY - offset));
		renderTextAnimation(text.c_str(), opacityOverride, true, true);
	}
	ImGui::SetCursorPos(ImVec2(textX, textY));
	renderTextAnimation(text.c_str(), opacityOverride, true, false);
}
void Renderer::centerTextSmall(std::string text, float textY, float opacityOverride) {
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowSize = io.DisplaySize;

	ImFont* fallbackFont = getLoadedFont(fontNameCjkSmall);
	if (fallbackFont == nullptr) {
		fallbackFont = (ImFont*)NexusLink->Font;
	}
	float textX = (windowSize.x - calculateRenderedTextWidth(fontSmall, fallbackFont, text.c_str())) / 2.0f;

	int offset = fontSettings->fontBorderOffset;
	if (fontSettings->fontBorderMode == 0) {
		//No shadow
	}
	else if (fontSettings->fontBorderMode == 1) {
		ImGui::SetCursorScreenPos(ImVec2(textX + offset, textY + offset));
		renderTextAnimation(text.c_str(), opacityOverride, false, true);
	}
	else if (fontSettings->fontBorderMode == 2) {
		ImGui::SetCursorScreenPos(ImVec2(textX - offset, textY - offset));
		renderTextAnimation(text.c_str(), opacityOverride, false, true);
	}
	ImGui::SetCursorPos(ImVec2(textX, textY));
	renderTextAnimation(text.c_str(), opacityOverride, false, false);
}

std::string replacePlaceholderTexts(std::string text, bool useSampleText) {

	std::string continent, region, map, sector;
	MapData* currentMap = currentMapService.getCurrentMap();

	if (useSampleText || currentMap == nullptr) {
		if (settings.locale == Locale::Zh) {
			continent = "泰瑞亚";
			region = "科瑞塔";
			map = "女王谷";
			sector = "狮子拱门";
		}
		else {
			continent = "Continent";
			region = "Region";
			map = "Map";
			sector = "Sector";
		}
	}
	else {
		continent = currentMap->continentName;
		region = currentMap->regionName;
		map = currentMap->name;
		sector = currentMap->currentSector.name;
	}

	replaceAll(text, "@c", continent);
	replaceAll(text, "@C", continent);
	replaceAll(text, "@r", region);
	replaceAll(text, "@R", region);
	replaceAll(text, "@m", map);
	replaceAll(text, "@M", map);
	replaceAll(text, "@s", sector);
	replaceAll(text, "@S", sector);

	// Replace WvW Team placeholders
	// guess what? still to lazy to do it properly. what is maintenance, right?
	std::string redTeamText, blueTeamText, greenTeamText;
	if (settings.locale == Locale::Zh) {
		redTeamText = "红方";
		blueTeamText = "蓝方";
		greenTeamText = "绿方";
	}
	else {
		redTeamText = "Red";
		blueTeamText = "Blue";
		greenTeamText = "Green";
	}

	if (match != nullptr) {
		gw2api::worlds::world* redWorld = worldInventory->getWorld(GetLocaleAsString(settings.locale), match->worlds.red);
		gw2api::worlds::alliance* redAlliance = worldInventory->getAlliance(GetLocaleAsString(settings.locale), match->worlds.red);
		
		if (redAlliance != nullptr) {
			redTeamText = redAlliance->name;
		}
		// Fallback to worlds just in case
		else if (redWorld != nullptr) {
			redTeamText = redWorld->name;
		}
	
		gw2api::worlds::world* blueWorld = worldInventory->getWorld(GetLocaleAsString(settings.locale), match->worlds.blue);
		gw2api::worlds::alliance* blueAlliance = worldInventory->getAlliance(GetLocaleAsString(settings.locale), match->worlds.blue);

		if (blueAlliance != nullptr) {
			blueTeamText = blueAlliance->name;
		}
		// Fallback to worlds just in case
		else if (blueWorld != nullptr) {
			blueTeamText = blueWorld->name;
		}

		gw2api::worlds::world* greenWorld = worldInventory->getWorld(GetLocaleAsString(settings.locale), match->worlds.green);
		gw2api::worlds::alliance* greenAlliance = worldInventory->getAlliance(GetLocaleAsString(settings.locale), match->worlds.green);

		if (greenAlliance != nullptr) {
			greenTeamText = greenAlliance->name;
		}
		// Fallback to worlds just in case
		else if (greenWorld != nullptr) {
			greenTeamText = greenWorld->name;
		}
	}
	replaceAll(text, redTeam, redTeamText);
	replaceAll(text, blueTeam, blueTeamText);
	replaceAll(text, greenTeam, greenTeamText);
	
	return text;
}
