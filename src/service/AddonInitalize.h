#ifndef INITIALIZE_SERVICE_H
#define INITIALIZE_SERVICE_H

#include "../Globals.h"
#include "../resource.h"
#include "../ziplib/src/zip.h"

#include <Windows.h>

namespace fs = std::filesystem;

static int on_extract_entry(const char* filename, void* arg) {
	static int i = 0;
	int n = *(int*)arg;

	APIDefs->Log(ELogLevel::ELogLevel_DEBUG, ADDON_NAME, filename);
	return 0;
}

static bool mapResourceFilesExist(const std::string& pathFolder) {
	if (!fs::exists(pathFolder + "/" + CJK_SEED_FILE)) {
		return false;
	}

	for (const auto& lang : SUPPORTED_LOCAL) {
		if (!fs::exists(pathFolder + "/" + lang + ".json")) {
			return false;
		}
	}

	return true;
}

static std::string packedResourceMarker() {
	return std::to_string(packedResourcesVersion);
}

static bool mapResourcesAreCurrent(const std::string& pathFolder) {
	if (!mapResourceFilesExist(pathFolder)) {
		return false;
	}

	std::string markerPath = pathFolder + "/resources.version";
	if (!fs::exists(markerPath)) {
		return false;
	}

	std::ifstream markerFile(markerPath);
	if (!markerFile.is_open()) {
		return false;
	}

	std::string marker;
	std::getline(markerFile, marker);
	return marker == packedResourceMarker();
}

static void storeMapResourceMarker(const std::string& pathFolder) {
	std::ofstream markerFile(pathFolder + "/resources.version");
	if (markerFile.is_open()) {
		markerFile << packedResourceMarker();
	}
}

static bool extractPackedMaps(const std::string& pathFolder) {
	HRSRC hResource = FindResource(hSelf, MAKEINTRESOURCE(IDR_MAPS_ZIP), L"ZIP");
	if (hResource == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Did not find packed maps resource.");
		return false;
	}

	HGLOBAL hLoadedResource = LoadResource(hSelf, hResource);
	if (hLoadedResource == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Could not load packed maps resource.");
		return false;
	}

	LPVOID lpResourceData = LockResource(hLoadedResource);
	if (lpResourceData == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Could not lock packed maps resource.");
		return false;
	}

	if (!fs::exists(pathFolder)) {
		try {
			fs::create_directory(pathFolder);
		}
		catch (const std::exception&) {
			APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, ("Could not create addon directory: " + pathFolder).c_str());
			return false;
		}
	}

	int arg = 2;
	int err = zip_stream_extract(
		static_cast<const char*>(lpResourceData),
		SizeofResource(hSelf, hResource),
		pathFolder.c_str(),
		on_extract_entry,
		&arg);
	if (err != 0) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Failed to extract packed maps from module.");
		return false;
	}
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Packed map data extracted from module.");
	if (!mapResourceFilesExist(pathFolder)) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Packed map extraction did not produce the expected map files.");
		return false;
	}
	return true;
}

static void unpackResource(const int resourceName, const std::string& resourceType, const std::string& targetFileName, bool overwrite = true) {
	std::string pathFolder = APIDefs->Paths.GetAddonDirectory(ADDON_NAME);
	std::string outputPath = pathFolder + "/" + targetFileName;
	if (fs::exists(outputPath) && !overwrite) {
		return;
	}

	std::wstring resourceTypeW(resourceType.begin(), resourceType.end());
	HRSRC hResource = FindResource(hSelf, MAKEINTRESOURCE(resourceName), resourceTypeW.c_str());
	if (hResource == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, ("Did not find resource: " + targetFileName).c_str());
		return;
	}

	HGLOBAL hLoadedResource = LoadResource(hSelf, hResource);
	if (hLoadedResource == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, ("Could not load resource: " + targetFileName).c_str());
		return;
	}

	LPVOID lpResourceData = LockResource(hLoadedResource);
	if (lpResourceData == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, ("Could not lock resource: " + targetFileName).c_str());
		return;
	}

	if (!fs::exists(pathFolder)) {
		try {
			fs::create_directory(pathFolder);
		}
		catch (const std::exception&) {
			APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, ("Could not create addon directory: " + pathFolder).c_str());
			return;
		}
	}

	FILE* file = nullptr;
	errno_t err = fopen_s(&file, outputPath.c_str(), "wb");
	if (err != 0 || file == nullptr) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, ("Error trying to write (fopen_s): " + targetFileName).c_str());
		return;
	}

	fwrite(lpResourceData, 1, SizeofResource(hSelf, hResource), file);
	fclose(file);
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, (targetFileName + " extracted from module.").c_str());
}

static void UnpackFonts(bool overwrite) {
	unpackResource(IDR_FONTS_CHARR, "TTF", "font_charr.ttf", overwrite);
	unpackResource(IDR_FONTS_HUMAN, "TTF", "font_human.ttf", overwrite);
	unpackResource(IDR_FONTS_SYLVARI, "TTF", "font_sylvari.ttf", overwrite);
	unpackResource(IDR_FONTS_NORN, "TTF", "font_norn.ttf", overwrite);
	unpackResource(IDR_FONTS_ASURA, "TTF", "font_asura.ttf", overwrite);
	unpackResource(IDR_FONTS_GENERIC, "TTF", "font_generic.ttf", overwrite);
	unpackResource(IDR_FONTS_CHARR_ANIM, "TTF", "fonts_charr_anim.ttf", overwrite);
	unpackResource(IDR_FONTS_HUMAN_ANIM, "TTF", "fonts_human_anim.ttf", overwrite);
	unpackResource(IDR_FONTS_SYLVARI_ANIM, "TTF", "fonts_sylvari_anim.ttf", overwrite);
	unpackResource(IDR_FONTS_NORN_ANIM, "TTF", "fonts_norn_anim.ttf", overwrite);
	unpackResource(IDR_FONTS_ASURA_ANIM, "TTF", "fonts_asura_anim.ttf", overwrite);
	unpackResource(IDR_FONTS_GENERIC_ANIM, "TTF", "fonts_generic_anim.ttf", overwrite);
}

static void unpackResources() {
	std::string pathFolder = getAddonFolder();
	if (mapResourcesAreCurrent(pathFolder)) {
		APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Packed map resources are current; skipping extraction.");
	}
	else {
		APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Packed map resources missing or outdated; extracting.");
		if (extractPackedMaps(pathFolder)) {
			storeMapResourceMarker(pathFolder);
		}
	}
	UnpackFonts(false);
}

#endif
