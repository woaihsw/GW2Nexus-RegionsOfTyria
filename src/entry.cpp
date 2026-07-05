///----------------------------------------------------------------------------------------------------
/// Copyright (c) Raidcore.GG - All rights reserved.
///
/// This code is licensed under the MIT license.
/// You should have received a copy of the license along with this source file.
/// You may obtain a copy of the license at: https://opensource.org/license/MIT
/// 
/// Name         :  entry.cpp
/// Description  :  Simple example of a Nexus addon implementation.
///----------------------------------------------------------------------------------------------------

#include "service/MapLoaderService.h"
#include "service/AddonRenderer.h"
#include "service/AddonInitalize.h"

#include "Globals.h"

#include <cstring> // For strcpy_s
#include <unordered_set>

/* proto */

// Addon loading
void AddonLoad(AddonAPI* aApi);
void AddonUnload();
// Rendering
void AddonRender();
void AddonOptions();
void AddonShortcut();
void PreRender();
void PostRender();
// Fonts
void ReceiveFont(const char* aIdentifier, void* aFont);
void loadFonts();
void loadFontsThreaded();
void releaseFonts();
bool loadCjkFonts(const std::string& addonFolder);
void registerCjkGlyphSeed(const std::string& addonFolder);
// Keybinds
void ProcessKeybind(const char* aIdentifer, bool aIsRelease);
// Events
void HandleIdentityChanged(void* anEventArgs);
void HandleAddonMetaData(void* eventArgs);
// Settings
void LoadSettings();
void StoreSettings();
std::string getAddonFolder();

/* globals */
HMODULE hSelf				   = nullptr;
AddonDefinition AddonDef	   = {};
AddonAPI* APIDefs			   = nullptr;
NexusLinkData* NexusLink	   = nullptr;
Mumble::Data* MumbleLink	   = nullptr;
MapInventory* mapInventory     = nullptr; 
WorldInventory* worldInventory = nullptr;
gw2api::wvw::match* match      = nullptr;

bool unloading = false;

/* settings */
bool showDebug = false;
bool showTemplate[6] = { false, false, false, false, false, false };
int templateRace = 0;

// local temps
std::string characterName = "";

Settings settings = {
	0, // FontsVersion
	Locale::En, // localse
	// Racial Font settings [6] 
	{
		{
			"Generic", 28.0f, "@c / @r / @m", 72.0f, "@s", 300.0f, 25.0f, 1.0f,	{255,255,255}, "@s", 20.0f, {255,255,255}, 1, 2, {0,0,0}
		}, {
			"Asura", 28.0f,	"@c / @r / @m",	72.0f, "@s", 300.0f, 25.0f,	1.0f, {255,255,255}, "@s", 20.0f, {255,255,255}, 1, 2, {0,0,0}
		}, {
			"Charr", 28.0f,	"@c / @r / @m",	72.0f, "@s", 300.0f, 25.0f,	1.0f, {255,255,255}, "@s", 20.0f, {255,255,255}, 1, 2, {0,0,0}
		}, {
			"Human", 28.0f,	"@c / @r / @m",	72.0f, "@s", 300.0f, 25.0f, 1.0f, {255,255,255}, "@s", 20.0f, {255,255,255}, 1, 2, {0,0,0}
		}, {
			"Norn", 28.0f, "@c / @r / @m", 72.0f, "@s",	300.0f,	25.0f, 1.0f, {255,255,255}, "@s", 20.0f, {255,255,255}, 1, 2, {0,0,0}
		}, {
			"Sylvari", 28.0f, "@c / @r / @m", 72.0f, "@s", 300.0f, 25.0f, 1.0f,	{255,255,255}, "@s", 20.0f, {255,255,255}, 1, 2, {0,0,0}
		}
	}, // Racial Font settings end
	-1,		// wvw world
	1,		// fontMode
	1,
	false,	// disableAnimations
	true,	// enablePopup
	false,	// hidePopupInCompetitive;
	false,	// hidePopupInCombat;
	35,		// popupAnimationSpeed
	3,		// popupAnimationDuration
	false,	// widgetEnabled
	100.0f, // widgetPosX
	100.0f, // widgetPosY
	200.0f, // widgetWidt
	0.8f,	// widgetBackgroundOpacity
	0,		// widgetTextAlign

	// deprecated Legacy settings
	"@c / @r / @m",
	"@s",
	300.0f,
	25.0f,
	1.5f,
	{255,255,255}
};

// local temps
char displayFormatSmallBuffer[100] = "";
char displayFormatLargeBuffer[100] = "";

std::mutex identityMutex;
ImVector<ImWchar> cjkGlyphRanges;
ImFont* optionsCjkFont = nullptr;
bool cjkGlyphSeedRegistered = false;

/* services */
Renderer renderer;
MapLoaderService mapLoader;

///----------------------------------------------------------------------------------------------------
/// DllMain:
/// 	Main entry point for DLL.
/// 	We are not interested in this, all we get is our own HMODULE in case we need it.
///----------------------------------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH: hSelf = hModule; break;
		case DLL_PROCESS_DETACH: break;
		case DLL_THREAD_ATTACH: break;
		case DLL_THREAD_DETACH: break;
	}
	return TRUE;
}

///----------------------------------------------------------------------------------------------------
/// GetAddonDef:
/// 	Export needed to give Nexus information about the addon.
///----------------------------------------------------------------------------------------------------
extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef()
{
	AddonDef.Signature = -126452345; // set to random unused negative integer
	AddonDef.APIVersion = NEXUS_API_VERSION;
	AddonDef.Name = "Regions Of Tyria";
	AddonDef.Version.Major = 1;
	AddonDef.Version.Minor = 5;
	AddonDef.Version.Build = 1;
	AddonDef.Version.Revision = 3;
	AddonDef.Author = "HeavyMetalPirate.2695";
	AddonDef.Description = "Displays the current sector whenever you cross borders, much like your favorite (MMO)RPG does.";
	AddonDef.Load = AddonLoad;
	AddonDef.Unload = AddonUnload;
	AddonDef.Flags = EAddonFlags_None;

	/* not necessary if hosted on Raidcore, but shown anyway for the example also useful as a backup resource */
	AddonDef.Provider = EUpdateProvider_GitHub;
	AddonDef.UpdateLink = "https://github.com/HeavyMetalPirate/GW2Nexus-RegionsOfTyria";

	return &AddonDef;
}

///----------------------------------------------------------------------------------------------------
/// AddonLoad:
/// 	Load function for the addon, will receive a pointer to the API.
/// 	(You probably want to store it.)
///----------------------------------------------------------------------------------------------------
void AddonLoad(AddonAPI* aApi)
{
	unloading = false;
	APIDefs = aApi; // store the api somewhere easily accessible

	ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext); // cast to ImGuiContext*
	ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc, (void(*)(void*, void*))APIDefs->ImguiFree); // on imgui 1.80+

	NexusLink = (NexusLinkData*)APIDefs->DataLink.Get("DL_NEXUS_LINK");
	MumbleLink = (Mumble::Data*)APIDefs->DataLink.Get("DL_MUMBLE_LINK");

	// TODO clean this code up at some point, keep it for now so you remember how to do this
	//APIDefs->AddShortcut("QA_MYFIRSTADDON", "ICON_PIKACHU", "ICON_JAKE", KB_MFA, "ASDF!");
	APIDefs->InputBinds.RegisterWithString(KB_MFA, ProcessKeybind, "CTRL+ALT+SHIFT+L");
	//APIDefs->AddSimpleShortcut(ADDON_NAME_LONG, AddonShortcut);

	renderer = Renderer();
	mapLoader.reset();
	mapInventory = new MapInventory();
	worldInventory = new WorldInventory();

	// Register Events
	APIDefs->Events.Subscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityChanged);
	APIDefs->Events.Subscribe("EV_TYRIAN_REGIONS_CHECK", HandleAddonMetaData);

	// Unpack the addon resources to the addon data
	unpackResources();
	LoadSettings();

	// if fonts version is not yet set, do the magic
	if (settings.fontsVersion == 0) {
		UnpackFonts(true);
		settings.fontsVersion = fontsVersion;
		StoreSettings();
	}

	// Initialize the custom fonts
	registerCjkGlyphSeed(getAddonFolder());
	loadFonts();

	// Start filling the inventory in the background
	mapLoader.initializeMapStorage();

	// Add an options window and a regular render callback - always do this at the end I guess
	APIDefs->Renderer.Register(ERenderType_PreRender, PreRender);
	APIDefs->Renderer.Register(ERenderType_PostRender, PostRender);
	APIDefs->Renderer.Register(ERenderType_OptionsRender, AddonOptions);
	APIDefs->Renderer.Register(ERenderType_Render, AddonRender);

	// Log initialize
	APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "<c=#00ff00>Initialize complete.</c>");
}

void ReceiveFont(const char* aIdentifier, void* aFont) {
	std::string str = aIdentifier;

	if (aFont == nullptr) {
#ifndef NDEBUG
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME,("Received nullptr for font " + std::string(aIdentifier)).c_str());
#endif // !NDEBUG
		return;
	}

	if (str == "ROT_FONT_GENERIC_SMALL")
	{
		renderer.registerFont(fontNameGenericSmall, (ImFont*)aFont);
	} 
	else if (str == "ROT_FONT_GENERIC_LARGE")
	{
		renderer.registerFont(fontNameGenericLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_GENERIC_WIDGET")
	{
		renderer.registerFont(fontNameGenericWidget, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CJK_OPTIONS")
	{
		optionsCjkFont = (ImFont*)aFont;
	}
	else if (str == "ROT_FONT_CJK_SMALL")
	{
		renderer.registerFont(fontNameCjkSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CJK_LARGE")
	{
		renderer.registerFont(fontNameCjkLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CJK_WIDGET")
	{
		renderer.registerFont(fontNameCjkWidget, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CJK_ANIM_SMALL")
	{
		renderer.registerFont(fontNameCjkAnimSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CJK_ANIM_LARGE")
	{
		renderer.registerFont(fontNameCjkAnimLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_ASURA_SMALL")
	{
		renderer.registerFont(fontNameAsuraSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_ASURA_LARGE")
	{
		renderer.registerFont(fontNameAsuraLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_ASURA_WIDGET")
	{
		renderer.registerFont(fontNameAsuraWidget, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CHARR_SMALL")
	{
		renderer.registerFont(fontNameCharrSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CHARR_LARGE")
	{
		renderer.registerFont(fontNameCharrLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CHARR_WIDGET")
	{
		renderer.registerFont(fontNameCharrWidget, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_HUMAN_SMALL")
	{
		renderer.registerFont(fontNameHumanSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_HUMAN_LARGE")
	{
		renderer.registerFont(fontNameHumanLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_HUMAN_WIDGET")
	{
		renderer.registerFont(fontNameHumanWidget, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_NORN_SMALL")
	{
		renderer.registerFont(fontNameNornSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_NORN_LARGE")
	{
		renderer.registerFont(fontNameNornLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_NORN_WIDGET")
	{
		renderer.registerFont(fontNameNornWidget, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_SYLVARI_SMALL")
	{
		renderer.registerFont(fontNameSylvariSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_SYLVARI_LARGE")
	{
		renderer.registerFont(fontNameSylvariLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_SYLVARI_WIDGET")
	{
		renderer.registerFont(fontNameSylvariWidget, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_GENERIC_ANIM_SMALL") {
		renderer.registerFont(fontNameGenericAnimSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_GENERIC_ANIM_LARGE") {
		renderer.registerFont(fontNameGenericAnimLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_ASURA_ANIM_SMALL") {
		renderer.registerFont(fontNameAsuraAnimSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_ASURA_ANIM_LARGE") {
		renderer.registerFont(fontNameAsuraAnimLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CHARR_ANIM_SMALL") {
		renderer.registerFont(fontNameCharrAnimSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_CHARR_ANIM_LARGE") {
		renderer.registerFont(fontNameCharrAnimLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_HUMAN_ANIM_SMALL") {
		renderer.registerFont(fontNameHumanAnimSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_HUMAN_ANIM_LARGE") {
		renderer.registerFont(fontNameHumanAnimLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_NORN_ANIM_SMALL") {
		renderer.registerFont(fontNameNornAnimSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_NORN_ANIM_LARGE") {
		renderer.registerFont(fontNameNornAnimLarge, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_SYLVARI_ANIM_SMALL") {
		renderer.registerFont(fontNameSylvariAnimSmall, (ImFont*)aFont);
	}
	else if (str == "ROT_FONT_SYLVARI_ANIM_LARGE") {
		renderer.registerFont(fontNameSylvariAnimLarge, (ImFont*)aFont);
	}
}

///----------------------------------------------------------------------------------------------------
/// AddonUnload:
/// 	Everything you registered in AddonLoad, you should "undo" here.
///----------------------------------------------------------------------------------------------------
void AddonUnload()
{

	StoreSettings();

	// Disable everything that listens to this global
	unloading = true;
	mapLoader.unload();
	renderer.unload();

	//APIDefs->RemoveShortcut("QA_MYFIRSTADDON");
	APIDefs->InputBinds.Deregister(KB_MFA);
	//APIDefs->RemoveSimpleShortcut(ADDON_NAME_LONG);

	APIDefs->Events.Unsubscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityChanged);
	APIDefs->Events.Unsubscribe("EV_TYRIAN_REGIONS_CHECK", HandleAddonMetaData);

	APIDefs->Renderer.Deregister(PreRender);
	APIDefs->Renderer.Deregister(PostRender);
	APIDefs->Renderer.Deregister(AddonRender);
	APIDefs->Renderer.Deregister(AddonOptions);

	releaseFonts();

	APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "<c=#ff0000>Signing off</c>, it was an honor commander.");
}

/// <summary>
/// PreRender Functionality
/// </summary>
void PreRender() {
	renderer.preRender(ImGui::GetIO());
}

void PostRender() {
	renderer.postRender(ImGui::GetIO());
}

///----------------------------------------------------------------------------------------------------
/// AddonRender:
/// 	Called every frame. Safe to render any ImGui.
/// 	You can control visibility on loading screens with NexusLink->IsGameplay.
///----------------------------------------------------------------------------------------------------
void AddonRender()
{
	renderer.render();

	// Fonts upgrade dialogue
	if (settings.fontsVersion < fontsVersion) {
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_AlwaysAutoResize;
		if (ImGui::Begin("Font version upgraded"), nullptr, flags) {
			ImGui::TextWrapped("Fonts for Regions of Tyria have been updated to a new version. Do you want to upgrade your local installation?");
			ImGui::TextColored({ 255,0,0,1 }, "Warning: Upgrading the fonts will overwrite changes you made to the font files!");
			ImGui::TextWrapped("You can backup your current fonts by copying the TTF files in your <GW2-Install>/addons/TyrianRegions folder.");

			if (ImGui::Button("Yes, I want to upgrade")) {
				UnpackFonts(true);
				releaseFonts();
				loadFontsThreaded();

				settings.fontsVersion = fontsVersion;
			}
			ImGui::SameLine();
			if (ImGui::Button("No, I want to keep the current fonts")) {
				settings.fontsVersion = fontsVersion;
			}

			ImGui::TextWrapped("Note: you can always upgrade the fonts in the options if you choose to do so at a later point.");

			ImGui::End();
		}
	}
}

///----------------------------------------------------------------------------------------------------
/// AddonOptions:
/// 	Basically an ImGui callback that doesn't need its own Begin/End calls.
///----------------------------------------------------------------------------------------------------
/// 
/// 
int InputTextFilterNumbers(ImGuiInputTextCallbackData* data)
{
	if (data->EventChar < 256 && strchr("0123456789", (char)data->EventChar))
		return 0;
	return 1;
}


void AddonOptions()
{
	ImGui::Separator();
	ImGui::Text("Locale");
	ImGui::Text("");
	bool pushedLocaleFont = optionsCjkFont != nullptr && optionsCjkFont->IsLoaded();
	if (pushedLocaleFont) {
		ImGui::PushFont(optionsCjkFont);
	}
	for (auto item : localeItems) {
		bool selected = settings.locale == item.value;
		ImGui::SameLine();
		if (ImGui::Checkbox(item.description.c_str(), &selected))
		{
			if (selected)
			{
				settings.locale = item.value;
				StoreSettings();
				EnsureLocaleMapsLoaded(item.name);
			}
		}
	}
	if (pushedLocaleFont) {
		ImGui::PopFont();
	}

	ImGui::Separator();
	ImGui::Text("Features");

	if (ImGui::Checkbox("Enable Popup Text", &settings.enablePopup)) {
		StoreSettings();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Disable animations", &settings.disableAnimations)) {
		StoreSettings();
	}
	if (ImGui::Checkbox("Disable popup in competitive modes", &settings.hidePopupInCompetitive)) {
		StoreSettings();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Disable popup in combat", &settings.hidePopupInCombat)) {
		StoreSettings();
	}

	ImGui::DragInt("Popup Duration (sec)", &settings.popupAnimationDuration, 0.1f, 1, 20);
	ImGui::DragInt("Animation Speed", &settings.popupAnimationSpeed, 0.1f, 0, 500);
	ImGui::Text("Lower value = faster animation");

	if (ImGui::Checkbox("Enable mini widget", &settings.widgetEnabled)) {
		StoreSettings();
	}
	ImGui::DragFloat("Widget Position (X)", &settings.widgetPositionX, 0.1f, 0, ImGui::GetIO().DisplaySize.x, "%.2f");
	ImGui::DragFloat("Widget Position (Y)", &settings.widgetPositionY, 0.1f, 0, ImGui::GetIO().DisplaySize.y, "%.2f");
	ImGui::DragFloat("Widget Width", &settings.widgetWidth, 0.1f, 1, ImGui::GetIO().DisplaySize.x / 2);
	ImGui::DragFloat("Widget Background", &settings.widgetBackgroundOpacity, 0.05f, 0.0f, 1.0f);

	static const char* textAlignComboItems[3];
	textAlignComboItems[0] = "Center";
	textAlignComboItems[1] = "Left";
	textAlignComboItems[2] = "Right";
	if (ImGui::Combo("Widget Text Alignment", &settings.widgetTextAlign, textAlignComboItems, IM_ARRAYSIZE(textAlignComboItems))) {
		StoreSettings();
	}

	ImGui::Separator();
	ImGui::Text("Display & Styling");

	static const char* comboBoxItems[7];
	comboBoxItems[0] = "Use font depending on race";
	comboBoxItems[1] = "Use Generic font everywhere";
	comboBoxItems[2] = "Use Asuran font everywhere";
	comboBoxItems[3] = "Use Charr font everywhere";
	comboBoxItems[4] = "Use Human font everywhere";
	comboBoxItems[5] = "Use Norn font everywhere";
	comboBoxItems[6] = "Use Sylvari font everywhere";

	if (ImGui::Combo("Font Mode", &settings.fontMode, comboBoxItems, IM_ARRAYSIZE(comboBoxItems))) {
		StoreSettings();
		renderer.updateFontSettings();
	}
	if (ImGui::Combo("Widget Font mode", &settings.widgetFontMode, comboBoxItems, IM_ARRAYSIZE(comboBoxItems))) {
		StoreSettings();
		renderer.updateFontSettings();
	}
	
	ImGui::Separator();
	
	ImGui::Text("Placeholders: @c = Continent, @r = Region, @m = Map, @s = Sector");
	if (ImGui::BeginTabBar("##Tabs")) {
		int i = -1; // counter variable for templateRace index
		for (auto& fs : settings.fontSettings) {

			// Font identifier formats:
			// ROT_FONT_<RACE>_ANIM_LARGE
			// ROT_FONT_<RACE>_ANIM_SMALL
			// ROT_FONT_<RACE>_WIDGET
			// ROT_FONT_<RACE>_SMALL
			// ROT_FONT_<RACE>_LARGE
			std::string raceUpper = fs.race;
			std::ranges::transform(raceUpper, raceUpper.begin(), [](unsigned char c) {
				return std::toupper(c);
			});

			std::string fontAnimLargeId = "ROT_FONT_<RACE>_ANIM_LARGE";
			replaceAll(fontAnimLargeId, "<RACE>", raceUpper);
			std::string fontAnimSmallId = "ROT_FONT_<RACE>_ANIM_SMALL";
			replaceAll(fontAnimSmallId, "<RACE>", raceUpper);
			std::string fontLargeId = "ROT_FONT_<RACE>_LARGE";
			replaceAll(fontLargeId, "<RACE>", raceUpper);
			std::string fontSmallId = "ROT_FONT_<RACE>_SMALL";
			replaceAll(fontSmallId, "<RACE>", raceUpper);
			std::string fontWidgetId = "ROT_FONT_<RACE>_WIDGET";
			replaceAll(fontWidgetId, "<RACE>", raceUpper);

			++i;
			if (ImGui::BeginTabItem(fs.race.c_str())) {
				ImGui::Text(("Popup Text Settings for font " + fs.race).c_str());

				if (ImGui::Checkbox("Show sample text", &showTemplate[i])) {
					if (showTemplate[i]) {
						// Uncheck all other checkboxes
						for (int j = 0; j < 6; ++j) {
							if (j != i) showTemplate[j] = false;
						}
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Copy Settings from...")) {
					ImGui::OpenPopup("Copy From...");
				}
				if (ImGui::BeginPopup("Copy From...")) {
					for (int j = 0; j < 6; ++j) {
						if (i != j) {
							if (ImGui::Selectable(settings.fontSettings[j].race.c_str())) {
								fs.displayFormatLarge = settings.fontSettings[j].displayFormatLarge;
								fs.displayFormatSmall = settings.fontSettings[j].displayFormatSmall;
								fs.largeFontSize = settings.fontSettings[j].largeFontSize;
								fs.smallFontSize = settings.fontSettings[j].smallFontSize;
								fs.spacing = settings.fontSettings[j].spacing;
								fs.verticalPosition = settings.fontSettings[j].verticalPosition;
								fs.fontColor[0] = settings.fontSettings[j].fontColor[0];
								fs.fontColor[1] = settings.fontSettings[j].fontColor[1];
								fs.fontColor[2] = settings.fontSettings[j].fontColor[2];

								fs.fontBorderMode = settings.fontSettings[j].fontBorderMode;
								fs.fontBorderOffset = settings.fontSettings[j].fontBorderOffset;
								fs.fontBorderColor[0] = settings.fontSettings[j].fontBorderColor[0];
								fs.fontBorderColor[1] = settings.fontSettings[j].fontBorderColor[1];
								fs.fontBorderColor[2] = settings.fontSettings[j].fontBorderColor[2];

								fs.widgetDisplayFormat = settings.fontSettings[j].widgetDisplayFormat;
								fs.widgetFontSize = settings.fontSettings[j].widgetFontSize;
								fs.widgetFontColor[0] = settings.fontSettings[j].widgetFontColor[0];
								fs.widgetFontColor[1] = settings.fontSettings[j].widgetFontColor[1];
								fs.widgetFontColor[2] = settings.fontSettings[j].widgetFontColor[2];

								// TODO change font sizes with API

								StoreSettings();
							}
						}
					}
					ImGui::EndPopup();
				}

				char bufferSmall[256];
				strncpy_s(bufferSmall, fs.displayFormatSmall.c_str(), sizeof(bufferSmall));
				if (ImGui::InputText("Display Format Small", bufferSmall, sizeof(bufferSmall))) {
					fs.displayFormatSmall = bufferSmall;
				}
				char bufferLarge[256];
				strncpy_s(bufferLarge, fs.displayFormatLarge.c_str(), sizeof(bufferLarge));
				if (ImGui::InputText("Display Format Large", bufferLarge, sizeof(bufferLarge))) {
					fs.displayFormatLarge = bufferLarge;
				}

				ImGui::DragFloat("Vertical Position", &fs.verticalPosition, 0.1f, 0.0f, ImGui::GetIO().DisplaySize.y);
				ImGui::DragFloat("Spacing", &fs.spacing, 0.1f, -300.0f, 300.0f);
				if (ImGui::InputFloat("Small Font Size", &fs.smallFontSize)) {
					APIDefs->Fonts.Resize(fontSmallId.c_str(), fs.smallFontSize);
					APIDefs->Fonts.Resize(fontAnimSmallId.c_str(), fs.smallFontSize);
				}
				if (ImGui::InputFloat("Large Font Size", &fs.largeFontSize)) {
					APIDefs->Fonts.Resize(fontLargeId.c_str(), fs.largeFontSize);
					APIDefs->Fonts.Resize(fontAnimLargeId.c_str(), fs.smallFontSize);

				}
				ImGui::ColorEdit3("Font Color", fs.fontColor);

				static const char* fontBorderModeComboItems[3];
				fontBorderModeComboItems[0] = "None";
				fontBorderModeComboItems[1] = "Shadow";
				fontBorderModeComboItems[2] = "Full Border";
				if (ImGui::Combo("Text Border", &fs.fontBorderMode, fontBorderModeComboItems, IM_ARRAYSIZE(fontBorderModeComboItems))) {

				}
				ImGui::DragInt("Text Border Offset", &fs.fontBorderOffset, 0.1f, 0, 10);
				ImGui::ColorEdit3("Text Border Color", fs.fontBorderColor);
				

				ImGui::Separator();
				ImGui::Text(("Widget Settings for font " + fs.race).c_str());
				char bufferWidget[256];
				strncpy_s(bufferWidget, fs.widgetDisplayFormat.c_str(), sizeof(bufferWidget));
				if (ImGui::InputText("Widget Display Format", bufferWidget, sizeof(bufferWidget))) {
					fs.widgetDisplayFormat = bufferWidget;
				}
				if (ImGui::InputFloat("Widget Font Size", &fs.widgetFontSize)) {
					APIDefs->Fonts.Resize(fontWidgetId.c_str(), fs.widgetFontSize);
				}
				ImGui::ColorEdit3("Widget Font Color", fs.widgetFontColor);

				ImGui::EndTabItem();
				
			}
		}
		ImGui::EndTabBar();
	}
	ImGui::Separator();
	
	if (ImGui::Button("Reload fonts")) {
		StoreSettings(); // store current settings in case of crash (weird fonts etc)

		// if samples is active, disable samples first and enable afterwards.
		int selectedShowTemplate = -1;
		for (int j = 0; j < 6; ++j) {
			if (showTemplate[j]) selectedShowTemplate = j;
			showTemplate[j] = false;
		}

		APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Font reload requested.");
		releaseFonts(); // release fonts sync
		loadFontsThreaded(); // load fonts async, waiting for unload to be executed

		// reenable selected template display
		if (selectedShowTemplate != -1) {
			showTemplate[selectedShowTemplate] = true;
		}
	}
	ImGui::TextWrapped("To use custom fonts, navigate to your <GW2Install>/addons/TyrianRegions folder. Inside this folder, replace the existing *.ttf files with the fonts of your liking.");
	ImGui::TextWrapped("Font files in the pattern 'font_*.ttf' are used for the actual text display, fonts in the pattern 'fonts_*_anim.ttf' are used for the animation phase.");
	ImGui::TextWrapped("Use the button 'Reset fonts to default' to unpack all font files again, overwriting any pre-existing ones in the addon folder.");
	if (ImGui::Button("Reset fonts to default")) {
		APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Font reset requested.");
		UnpackFonts(true);
		releaseFonts();
		loadFontsThreaded();
	}
}

void AddonShortcut() {
	// TODO?
	if (ImGui::Checkbox("DebugFrame", &showDebug)) {

	}
}

void ProcessKeybind(const char* aIdentifier, bool aIsRelease)
{
	// TODO clean this up at some point, keep it for now to remember how this works if we need it
	if (strcmp(aIdentifier, KB_MFA) == 0)
	{
		showDebug = !showDebug;
		return;
	}
}

void HandleIdentityChanged(void* anEventArgs) {
	std::lock_guard<std::mutex> lock(identityMutex); // Ensures single-thread access

	Mumble::Identity* identity = (Mumble::Identity*)anEventArgs;
	std::string name(identity->Name);
	if (name != characterName) {
		characterName = std::string(identity->Name);
		if (characterName.empty()) return;
		// trigger sector reset on currentMapService
		renderer.changeCurrentCharacter(characterName);
		renderer.setRacialFont(identity->Race);
		APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Character changed: " + characterName).c_str());
	}
}

void LoadSettings() {
	// Get addon directory
	std::string pathData = getAddonFolder() + "/settings.json";
	if (fs::exists(pathData)) {
		std::ifstream dataFile(pathData);

		if (dataFile.is_open()) {
			json jsonData;
			dataFile >> jsonData;
			dataFile.close();
			// parse settings, yay
			settings = jsonData;
		}
	}
	else {
		// Create new settings!
		StoreSettings();
	}
}

void StoreSettings() {
	json j = settings;

	std::string pathData = getAddonFolder() + "/settings.json";
	std::ofstream outputFile(pathData);
	if (outputFile.is_open()) {
		outputFile << j.dump(4) << std::endl;
		outputFile.close();
	}
	else {
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Could not store default settings.json - configuration might get lost between loads.");
	}
}

void EnsureLocaleMapsLoaded(const std::string& locale) {
	mapLoader.ensureLocaleLoaded(locale);
}

void RequestMapLoad(const std::string& locale, int mapId) {
	mapLoader.requestMapFromAPI(locale, mapId);
}

void loadFont(std::string id, float size, std::string filename) {
	APIDefs->Fonts.AddFromFile(id.c_str(), size > 0 ? size : 10, filename.c_str(), ReceiveFont, nullptr);
}

std::vector<std::string> getSystemCjkFontCandidates() {
	return {
		"C:/Windows/Fonts/msyh.ttc",
		"C:/Windows/Fonts/simsun.ttc",
		"C:/Windows/Fonts/simhei.ttf",
		"C:/Windows/Fonts/Deng.ttf",
		"C:/Windows/Fonts/msjh.ttc",
		"C:/Windows/Fonts/mingliu.ttc"
	};
}

std::string getSystemCjkFontPath() {
	for (const auto& candidate : getSystemCjkFontCandidates()) {
		if (fs::exists(candidate)) {
			return candidate;
		}
	}

	return "";
}

std::string getSystemCjkAnimationFontPath(const std::string& primaryFontPath) {
	for (const auto& candidate : getSystemCjkFontCandidates()) {
		if (candidate != primaryFontPath && fs::exists(candidate)) {
			return candidate;
		}
	}

	return primaryFontPath;
}

int getUtf8CharLength(const char* text) {
	unsigned char c = static_cast<unsigned char>(*text);
	if (c < 0x80) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;
}

void appendUniqueUtf8Characters(std::string& target, std::unordered_set<std::string>& seen, const std::string& text) {
	for (size_t i = 0; i < text.size();) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		int charLength = getUtf8CharLength(text.c_str() + i);
		if (i + charLength > text.size()) {
			break;
		}

		if (c >= 0x80) {
			std::string character = text.substr(i, charLength);
			if (seen.insert(character).second) {
				target.append(character);
			}
		}

		i += charLength;
	}
}

void registerCjkGlyphSeed(const std::string& addonFolder) {
	if (cjkGlyphSeedRegistered || APIDefs == nullptr) {
		return;
	}

	std::string seed = "Español Français 中文 泰瑞亚 科瑞塔 狮子拱门 迷雾之地";
	std::unordered_set<std::string> seen;
	std::string uniqueSeed;
	appendUniqueUtf8Characters(uniqueSeed, seen, seed);

	std::ifstream zhData(addonFolder + "/zh.json", std::ios::binary);
	if (zhData.is_open()) {
		std::stringstream buffer;
		buffer << zhData.rdbuf();
		appendUniqueUtf8Characters(uniqueSeed, seen, buffer.str());
	}

	std::vector<std::string> languageIdentifiers = {
		"en", "de", "es", "fr", "zh",
		"en-GB", "en-US", "de-DE", "es-ES", "fr-FR", "zh-CN"
	};
	for (const auto& languageIdentifier : languageIdentifiers) {
		APIDefs->Localization.Set("ROT_CJK_GLYPH_SEED", languageIdentifier.c_str(), uniqueSeed.c_str());
	}
	cjkGlyphSeedRegistered = true;
	APIDefs->Log(ELogLevel_INFO, ADDON_NAME, ("Registered CJK glyph seed with " + std::to_string(seen.size()) + " unique non-ASCII characters.").c_str());
}

const ImWchar* getCjkGlyphRanges(const std::string& addonFolder) {
	if (cjkGlyphRanges.Size > 0) {
		return cjkGlyphRanges.Data;
	}

	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
	builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());

	std::ifstream zhData(addonFolder + "/zh.json", std::ios::binary);
	if (zhData.is_open()) {
		std::stringstream buffer;
		buffer << zhData.rdbuf();
		builder.AddText(buffer.str().c_str());
	}

	builder.BuildRanges(&cjkGlyphRanges);
	return cjkGlyphRanges.Data;
}

bool loadCjkFonts(const std::string& addonFolder) {
	std::string cjkFontPath = getSystemCjkFontPath();
	if (cjkFontPath.empty()) {
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "No system CJK font found. Chinese text may use ImGui fallback glyphs.");
		return false;
	}
	std::string cjkAnimFontPath = getSystemCjkAnimationFontPath(cjkFontPath);

	static ImFontConfig cjkFontConfig;
	cjkFontConfig = ImFontConfig();
	cjkFontConfig.OversampleH = 1;
	cjkFontConfig.OversampleV = 1;
	cjkFontConfig.GlyphRanges = getCjkGlyphRanges(addonFolder);

	float optionsFontSize = 16.0f;
	if (NexusLink != nullptr && NexusLink->FontUI != nullptr) {
		optionsFontSize = ((ImFont*)NexusLink->FontUI)->FontSize;
	}
	else if (NexusLink != nullptr && NexusLink->Font != nullptr) {
		optionsFontSize = ((ImFont*)NexusLink->Font)->FontSize;
	}

	APIDefs->Fonts.AddFromFile("ROT_FONT_CJK_OPTIONS", optionsFontSize, cjkFontPath.c_str(), ReceiveFont, &cjkFontConfig);
	APIDefs->Fonts.AddFromFile("ROT_FONT_CJK_SMALL", settings.fontSettings[0].smallFontSize, cjkFontPath.c_str(), ReceiveFont, &cjkFontConfig);
	APIDefs->Fonts.AddFromFile("ROT_FONT_CJK_LARGE", settings.fontSettings[0].largeFontSize, cjkFontPath.c_str(), ReceiveFont, &cjkFontConfig);
	APIDefs->Fonts.AddFromFile("ROT_FONT_CJK_WIDGET", settings.fontSettings[0].widgetFontSize, cjkFontPath.c_str(), ReceiveFont, &cjkFontConfig);
	APIDefs->Fonts.AddFromFile("ROT_FONT_CJK_ANIM_SMALL", settings.fontSettings[0].smallFontSize, cjkAnimFontPath.c_str(), ReceiveFont, &cjkFontConfig);
	APIDefs->Fonts.AddFromFile("ROT_FONT_CJK_ANIM_LARGE", settings.fontSettings[0].largeFontSize, cjkAnimFontPath.c_str(), ReceiveFont, &cjkFontConfig);
	return true;
}

void loadFonts() {
	std::string pathFolder = APIDefs->Paths.GetAddonDirectory(ADDON_NAME);
	loadFont("ROT_FONT_GENERIC_SMALL", settings.fontSettings[0].smallFontSize, (pathFolder + "/font_generic.ttf").c_str());
	loadFont("ROT_FONT_GENERIC_LARGE", settings.fontSettings[0].largeFontSize, (pathFolder + "/font_generic.ttf").c_str());
	loadFont("ROT_FONT_GENERIC_WIDGET", settings.fontSettings[0].widgetFontSize, (pathFolder + "/font_generic.ttf").c_str());
	loadFont("ROT_FONT_ASURA_SMALL", settings.fontSettings[1].smallFontSize, (pathFolder + "/font_asura.ttf").c_str());
	loadFont("ROT_FONT_ASURA_LARGE", settings.fontSettings[1].largeFontSize, (pathFolder + "/font_asura.ttf").c_str());
	loadFont("ROT_FONT_ASURA_WIDGET", settings.fontSettings[1].widgetFontSize, (pathFolder + "/font_asura.ttf").c_str());
	loadFont("ROT_FONT_CHARR_SMALL", settings.fontSettings[2].smallFontSize, (pathFolder + "/font_charr.ttf").c_str());
	loadFont("ROT_FONT_CHARR_LARGE", settings.fontSettings[2].largeFontSize, (pathFolder + "/font_charr.ttf").c_str());
	loadFont("ROT_FONT_CHARR_WIDGET", settings.fontSettings[2].widgetFontSize, (pathFolder + "/font_charr.ttf").c_str());
	loadFont("ROT_FONT_HUMAN_SMALL", settings.fontSettings[3].smallFontSize, (pathFolder + "/font_human.ttf").c_str());
	loadFont("ROT_FONT_HUMAN_LARGE", settings.fontSettings[3].largeFontSize, (pathFolder + "/font_human.ttf").c_str());
	loadFont("ROT_FONT_HUMAN_WIDGET", settings.fontSettings[3].widgetFontSize, (pathFolder + "/font_human.ttf").c_str());
	loadFont("ROT_FONT_NORN_SMALL", settings.fontSettings[4].smallFontSize, (pathFolder + "/font_norn.ttf").c_str());
	loadFont("ROT_FONT_NORN_LARGE", settings.fontSettings[4].largeFontSize, (pathFolder + "/font_norn.ttf").c_str());
	loadFont("ROT_FONT_NORN_WIDGET", settings.fontSettings[4].widgetFontSize, (pathFolder + "/font_norn.ttf").c_str());
	loadFont("ROT_FONT_SYLVARI_SMALL", settings.fontSettings[5].smallFontSize, (pathFolder + "/font_sylvari.ttf").c_str());
	loadFont("ROT_FONT_SYLVARI_LARGE", settings.fontSettings[5].largeFontSize, (pathFolder + "/font_sylvari.ttf").c_str());
	loadFont("ROT_FONT_SYLVARI_WIDGET", settings.fontSettings[5].widgetFontSize, (pathFolder + "/font_sylvari.ttf").c_str());

	// animation fonts as well
	loadFont("ROT_FONT_GENERIC_ANIM_SMALL", settings.fontSettings[0].smallFontSize, (pathFolder + "/fonts_generic_anim.ttf").c_str());
	loadFont("ROT_FONT_GENERIC_ANIM_LARGE", settings.fontSettings[0].largeFontSize, (pathFolder + "/fonts_generic_anim.ttf").c_str());
	loadFont("ROT_FONT_ASURA_ANIM_SMALL", settings.fontSettings[1].smallFontSize, (pathFolder + "/fonts_asura_anim.ttf").c_str());
	loadFont("ROT_FONT_ASURA_ANIM_LARGE", settings.fontSettings[1].largeFontSize, (pathFolder + "/fonts_asura_anim.ttf").c_str());
	loadFont("ROT_FONT_CHARR_ANIM_SMALL", settings.fontSettings[2].smallFontSize, (pathFolder + "/fonts_charr_anim.ttf").c_str());
	loadFont("ROT_FONT_CHARR_ANIM_LARGE", settings.fontSettings[2].largeFontSize, (pathFolder + "/fonts_charr_anim.ttf").c_str());
	loadFont("ROT_FONT_HUMAN_ANIM_SMALL", settings.fontSettings[3].smallFontSize, (pathFolder + "/fonts_human_anim.ttf").c_str());
	loadFont("ROT_FONT_HUMAN_ANIM_LARGE", settings.fontSettings[3].largeFontSize, (pathFolder + "/fonts_human_anim.ttf").c_str());
	loadFont("ROT_FONT_NORN_ANIM_SMALL", settings.fontSettings[4].smallFontSize, (pathFolder + "/fonts_norn_anim.ttf").c_str());
	loadFont("ROT_FONT_NORN_ANIM_LARGE", settings.fontSettings[4].largeFontSize, (pathFolder + "/fonts_norn_anim.ttf").c_str());
	loadFont("ROT_FONT_SYLVARI_ANIM_SMALL", settings.fontSettings[5].smallFontSize, (pathFolder + "/fonts_sylvari_anim.ttf").c_str());
	loadFont("ROT_FONT_SYLVARI_ANIM_LARGE", settings.fontSettings[5].largeFontSize, (pathFolder + "/fonts_sylvari_anim.ttf").c_str());

	loadCjkFonts(pathFolder);
}	


void loadFontsThreaded() {
	// in case we reload fonts during runtime we need to wait for the unload to be executed first
	// that happens before/after render execution. therefore run it threaded with sleeps and waiting
	// for fonts to unload first
	std::thread fontsThread([] {

		// wait for unload
		while (!renderer.isCleared()) {
			Sleep(1);
		}

		// grace period for Nexus to get its shit together
		Sleep(50);
		loadFonts();

		});

	fontsThread.detach();
}

void releaseFonts() {

	APIDefs->Fonts.Release("ROT_FONT_GENERIC_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_GENERIC_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_GENERIC_WIDGET", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CJK_OPTIONS", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CJK_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CJK_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CJK_WIDGET", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CJK_ANIM_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CJK_ANIM_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_ASURA_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_ASURA_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_ASURA_WIDGET", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CHARR_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CHARR_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CHARR_WIDGET", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_HUMAN_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_HUMAN_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_HUMAN_WIDGET", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_NORN_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_NORN_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_NORN_WIDGET", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_SYLVARI_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_SYLVARI_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_SYLVARI_WIDGET", ReceiveFont);

	APIDefs->Fonts.Release("ROT_FONT_GENERIC_ANIM_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_GENERIC_ANIM_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_ASURA_ANIM_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_ASURA_ANIM_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CHARR_ANIM_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_CHARR_ANIM_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_HUMAN_ANIM_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_HUMAN_ANIM_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_NORN_ANIM_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_NORN_ANIM_LARGE", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_SYLVARI_ANIM_SMALL", ReceiveFont);
	APIDefs->Fonts.Release("ROT_FONT_SYLVARI_ANIM_LARGE", ReceiveFont);

	optionsCjkFont = nullptr;
	renderer.clearFonts();
	APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Font unload queued successfully.");
}

void HandleAddonMetaData(void* eventArgs) {
	APIDefs->Events.Raise("EV_TYRIAN_REGIONS_AVAILABLE", &AddonDef);
}
