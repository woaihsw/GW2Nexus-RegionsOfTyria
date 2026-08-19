#ifndef GLOBALS_H
#define GLOBALS_H

#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#ifndef UNICODE
#define UNICODE
#endif // !UNICODE

#ifndef STRICT
#define STRICT
#endif // !STRICT

#include <atomic>
#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <chrono>
#include <numeric>
#include <math.h>
#include <filesystem>
#include <fstream>

#include "imgui/imgui.h"
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"

#include "Constants.h"
#include "Settings.h"
#include "service/MapInventory.h"
#include "service/WorldInventory.h"
#include "entity/GW2API_WvW.h"

namespace fs = std::filesystem;

extern HMODULE hSelf;
extern AddonAPI* APIDefs;
extern Mumble::Data* MumbleLink;
extern NexusLinkData* NexusLink;

extern std::unique_ptr<MapInventory> mapInventory;
extern std::unique_ptr<WorldInventory> worldInventory;
extern gw2api::wvw::match* match;

extern std::atomic<bool> unloading;

void EnsureLocaleMapsLoaded(const std::string& locale);
void RequestMapLoad(const std::string& locale, int mapId);

/* Utility */
inline void replaceAll(std::string& str, const std::string& from, const std::string& to) {
	size_t startPos = 0;
	while ((startPos = str.find(from, startPos)) != std::string::npos) {
		str.replace(startPos, from.length(), to);
		startPos += to.length(); // Move past the replaced substring
	}
}
inline std::string getAddonFolder() {
	std::string pathFolder = APIDefs->Paths.GetAddonDirectory(ADDON_NAME);
	// Create folder if not exist
	if (!fs::exists(pathFolder)) {
		try {
			fs::create_directory(pathFolder);
		}
		catch (const std::exception& e) {
			std::string message = "Could not create addon directory: ";
			message.append(pathFolder);
			APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, message.c_str());

			// Suppress the warning for the unused variable 'e'
#pragma warning(suppress: 4101)
			e;
		}
	}
	return pathFolder;
}
#endif // GLOBALS_H
