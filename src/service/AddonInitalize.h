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

static size_t getResourceSize(const int resourceName, const std::string& resourceType) {
	std::wstring resourceTypeW(resourceType.begin(), resourceType.end());
	HRSRC hResource = FindResource(hSelf, MAKEINTRESOURCE(resourceName), resourceTypeW.c_str());
	if (hResource == NULL) {
		return 0;
	}

	return SizeofResource(hSelf, hResource);
}

static unsigned long long getResourceHash(const int resourceName, const std::string& resourceType) {
	std::wstring resourceTypeW(resourceType.begin(), resourceType.end());
	HRSRC hResource = FindResource(hSelf, MAKEINTRESOURCE(resourceName), resourceTypeW.c_str());
	if (hResource == NULL) {
		return 0;
	}

	HGLOBAL hLoadedResource = LoadResource(hSelf, hResource);
	if (hLoadedResource == NULL) {
		return 0;
	}

	LPVOID lpResourceData = LockResource(hLoadedResource);
	if (lpResourceData == NULL) {
		return 0;
	}

	size_t resourceSize = SizeofResource(hSelf, hResource);
	const unsigned char* data = static_cast<const unsigned char*>(lpResourceData);
	unsigned long long hash = 1469598103934665603ULL;
	for (size_t i = 0; i < resourceSize; i++) {
		hash ^= data[i];
		hash *= 1099511628211ULL;
	}

	return hash;
}

static std::string getPackedResourceSignature() {
	return std::to_string(packedResourcesVersion) + ":" +
		std::to_string(getResourceSize(IDR_MAPS_ZIP, "ZIP")) + ":" +
		std::to_string(getResourceHash(IDR_MAPS_ZIP, "ZIP"));
}

static bool mapResourceFilesExist(const std::string& pathFolder) {
	for (const auto& lang : SUPPORTED_LOCAL) {
		if (!fs::exists(pathFolder + "/" + lang + ".json")) {
			return false;
		}
	}

	return true;
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
	return marker == getPackedResourceSignature();
}

static void storeMapResourceMarker(const std::string& pathFolder) {
	std::ofstream markerFile(pathFolder + "/resources.version");
	if (markerFile.is_open()) {
		markerFile << getPackedResourceSignature();
	}
}

static void unpackResource(const int resourceName, const std::string& resourceType, const std::string& targetFileName, bool overwrite = true) {

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
	std::string outputPath = pathFolder + "/" + targetFileName;

	if (fs::exists(outputPath) && !overwrite) {
		APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, ("Resource already exists, and should not be overwritten: " + targetFileName).c_str());
		return;
	}

	FILE* file = nullptr;
	errno_t err = fopen_s(&file, outputPath.c_str(), "wb");
	if (err != 0 || file == nullptr) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, ("Error trying to write (fopen_s): " + targetFileName).c_str());
		return;
	}

	size_t resourceSize = SizeofResource(hSelf, hResource);
	fwrite(lpResourceData, 1, resourceSize, file);

	fclose(file);
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, (targetFileName + " extracted from module.").c_str());

	if ("ZIP" == resourceType) {
		int arg = 2;
		zip_extract(outputPath.c_str(), pathFolder.c_str(), on_extract_entry, &arg);
		APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, ("Extracted data from " + targetFileName).c_str());
	}
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
		unpackResource(IDR_MAPS_ZIP, "ZIP", "Maps.zip");
		if (mapResourceFilesExist(pathFolder)) {
			storeMapResourceMarker(pathFolder);
		}
	}
	UnpackFonts(false);
}


#endif
