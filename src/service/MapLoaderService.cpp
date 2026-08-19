#include "MapLoaderService.h"
#include <Windows.h>
#include <limits>
#include "../resource.h"

#include "../ziplib/src/zip.h"
#include "HttpClient.h"

int timeoutCounter = 0;
int retryCounter = 0;

static void addDefaultSector(gw2::map& mapInfo) {
	if (mapInfo.sectors.size() > 0) {
		return;
	}

	gw2api::continents::sector empty = gw2api::continents::sector();
	empty.id = -1;
	empty.name = mapInfo.name;
	empty.level = 80;
	empty.chatLink = "undefined";
	empty.bounds.push_back({ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() });
	empty.bounds.push_back({ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() });
	mapInfo.sectors.emplace("-1", empty);
}

static int on_extract_entry(const char* filename, void* arg) {
	static int i = 0;
	int n = *(int*)arg;

	APIDefs->Log(ELogLevel::ELogLevel_DEBUG, ADDON_NAME, (filename));
	return 0;
}

MapLoaderService::MapLoaderService() {}
MapLoaderService::~MapLoaderService() {
	unload();
}

void MapLoaderService::reset() {
	std::lock_guard<std::mutex> lock(requestMutex);
	pendingLocales.clear();
	pendingMaps.clear();
	failedMaps.clear();
}

void MapLoaderService::startWorker() {
	std::lock_guard<std::mutex> lock(queueMutex);
	if (worker.joinable()) {
		return;
	}
	worker = std::jthread([this](std::stop_token stopToken) {
		workerLoop(stopToken);
	});
}

void MapLoaderService::enqueueJob(std::function<void()> job) {
	if (unloading.load()) {
		return;
	}
	startWorker();
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		if (worker.joinable() && worker.get_stop_token().stop_requested()) {
			return;
		}
		jobs.push(std::move(job));
	}
	queueCv.notify_one();
}

void MapLoaderService::workerLoop(std::stop_token stopToken) {
	while (!stopToken.stop_requested()) {
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			queueCv.wait(lock, [&] {
				return stopToken.stop_requested() || !jobs.empty();
			});
			if (stopToken.stop_requested() && jobs.empty()) {
				return;
			}
			if (jobs.empty()) {
				continue;
			}
			job = std::move(jobs.front());
			jobs.pop();
		}
		if (unloading.load() || stopToken.stop_requested()) {
			continue;
		}
		job();
	}
}

void MapLoaderService::unload() {
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		while (!jobs.empty()) {
			jobs.pop();
		}
	}
	if (worker.joinable()) {
		worker.request_stop();
		queueCv.notify_all();
		worker.join();
	}
}

/// <summary>
/// Utility function to request an URI with yhirose/cpp-httlib.
/// TODO: maybe generalize the return type to decouple from HTTL lib implementation?
/// </summary>
/// <param name="uri">uri to be requested</param>
/// <returns>Result of the call, or nullptr in case of a timeout on the client</returns>
std::string MapLoaderService::performRequest(std::string uri) {	
	std::string requestUri = baseUrl + uri;
	std::string response = "";
	try {
		response = HTTPClient::GetRequest(requestUri);

		int retry = 1;
		while (response.empty() && retry < 10) {
			if (unloading.load()) break;
			Sleep(50);
			if (unloading.load()) break;
			retry++;
			response = HTTPClient::GetRequest(requestUri);
		}
#ifndef NDEBUG
		APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Fetched result for " + uri + " after " + std::to_string(retry) + " attempts.").c_str());
#endif
	}
	catch (...) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Unknown exception performing HTTP call.");
	}
	return response;
}

void MapLoaderService::initializeMapStorage() {
	startWorker();
}

void MapLoaderService::ensureLocaleLoaded(std::string locale) {
	if (unloading.load() || locale.empty() || locale == "Unknown") return;
	if (mapInventory->isLocaleLoaded(locale)) return;

	{
		std::lock_guard<std::mutex> lock(requestMutex);
		if (pendingLocales.contains(locale)) return;
		pendingLocales.insert(locale);
	}

	enqueueJob([this, locale] {
		loadMapsFromStorage(locale);
		std::lock_guard<std::mutex> lock(requestMutex);
		pendingLocales.erase(locale);
	});
}

void MapLoaderService::requestMapFromAPI(std::string locale, int mapId) {
	if (unloading.load() || locale.empty() || locale == "Unknown" || mapId <= 0) return;
	if (mapInventory->getMapInfo(locale, mapId) != nullptr) return;

	std::string requestKey = locale + ":" + std::to_string(mapId);
	{
		std::lock_guard<std::mutex> lock(requestMutex);
		if (failedMaps.contains(requestKey)) return;
		if (pendingMaps.contains(requestKey)) return;
		pendingMaps.insert(requestKey);
	}

	enqueueJob([this, locale, mapId, requestKey] {
		bool loaded = loadMapFromAPI(locale, mapId);
		std::lock_guard<std::mutex> lock(requestMutex);
		pendingMaps.erase(requestKey);
		if (!loaded) {
			failedMaps.insert(requestKey);
		}
	});
}

void MapLoaderService::loadAlliancesFromStorage() {
	try {
		// Get addon directory
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

		for (auto lang : SUPPORTED_LOCAL) {
			if (unloading.load()) return;
			// Load events from data.json
			std::string pathData = pathFolder + "/alliances_en.json"; // TODO base off locale once we have them all
			if (fs::exists(pathData)) {
				std::ifstream dataFile(pathData);

				if (dataFile.is_open()) {
					json jsonData;
					dataFile >> jsonData;
					dataFile.close();

					std::vector<gw2api::worlds::alliance> alliances = jsonData.get<std::vector<gw2api::worlds::alliance>>();

					for (auto alliance : alliances) {
						worldInventory->addAlliance(lang, alliance);
					}
				}
			}
		}
	}
	catch (const std::exception& e) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Exception in alliance initialization thread.");
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, e.what());
	}
	catch (...) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Unknown exception in alliance initialization thread.");
	}
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Alliance loading from storage complete.");
}

void MapLoaderService::loadWorldsFromAPI() {
	for (auto lang : SUPPORTED_LOCAL) {
		std::string worldsResponse = performRequest("/v2/worlds?ids=all&lang=" + lang);
		if (worldsResponse.empty()) {
			APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Loading of Worlds data failed! Sector display in WvW may be incomplete!");
			return;
		}
		json worldsJson = json::parse(worldsResponse);
		std::vector<gw2api::worlds::world> worlds = worldsJson.get<std::vector <gw2api::worlds::world>>();

		for (auto world : worlds)
		{
			worldInventory->addWorld(lang, world);
		}
	}
}

void MapLoaderService::loadWvWMatchFromAPI() {
	APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WvW matchup API loading is disabled for this build.");
}

void MapLoaderService::unpackMaps() {

	// get resource from internal bundled resources
	HRSRC hResource = FindResource(hSelf, MAKEINTRESOURCE(IDR_MAPS_ZIP), L"ZIP");
	if (hResource == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Did not find resource.");
		return;
	}

	// Get Handle of resource
	HGLOBAL hLoadedResource = LoadResource(hSelf, hResource);
	if (hLoadedResource == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Could not load resource.");
		return;
	}

	// Lock resource
	LPVOID lpResourceData = LockResource(hLoadedResource);
	if (lpResourceData == NULL) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Could not lock resource.");
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
	std::string outputPath = pathFolder + "/maps.zip";

	// Open file for writing in binary
	FILE* file = nullptr;
	errno_t err = fopen_s(&file, outputPath.c_str(), "wb");
	if (err != 0 || file == nullptr) {
		APIDefs->Log(ELogLevel::ELogLevel_CRITICAL, ADDON_NAME, "Error trying to write maps.zip (fopen_s)");
		return;
	}

	// Write resource
	size_t resourceSize = SizeofResource(hSelf, hResource);
	fwrite(lpResourceData, 1, resourceSize, file);

	// Close data, free up resources
	fclose(file);
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "maps.zip extracted from module.");
	
	int arg = 2;
	zip_extract(outputPath.c_str(), pathFolder.c_str(), on_extract_entry, &arg);
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Map JSON data extracted from maps.zip.");
}

/// <summary>
/// Load all the Maps from addon storage and store them in the mapInventory.
/// </summary>
void MapLoaderService::loadAllMapsFromStorage() {
	for (auto lang : SUPPORTED_LOCAL) {
		if (unloading.load()) return;
		loadMapsFromStorage(lang);
	}
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Map loading from storage complete.");
}

void MapLoaderService::loadMapsFromStorage(std::string lang) {
	try {
		// Get addon directory
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

		if (unloading.load() || mapInventory->isLocaleLoaded(lang)) return;

		std::string pathData = pathFolder + "/" + lang + ".json";
		if (fs::exists(pathData)) {
			std::ifstream dataFile(pathData);

			if (!dataFile.is_open()) {
				APIDefs->Log(ELogLevel::ELogLevel_WARNING, ADDON_NAME, ("Could not open maps file for language: " + lang + ".json").c_str());
				mapInventory->markLocaleLoaded(lang);
				return;
			}

			json jsonData;
			dataFile >> jsonData;
			dataFile.close();

			gw2::region region = jsonData;

			for (auto map : region.maps)
			{
				if (unloading.load()) return;
				mapInventory->addMap(lang, map.second);
			}
			mapInventory->markLocaleLoaded(lang);
		}
		else {
			APIDefs->Log(ELogLevel::ELogLevel_WARNING, ADDON_NAME, ("Maps file for language not found: " + lang + ".json").c_str());
			mapInventory->markLocaleLoaded(lang);
			return;
		}

		if (unloading.load()) return;
		std::stringstream stream;
		stream << "Maps for locale '" << lang << "' in inventory: " << std::to_string(mapInventory->getLoadedMaps(lang).size());
		APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, stream.str().c_str());
	}

	catch (const std::exception& e) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Exception in map initialization thread.");
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, e.what());
		mapInventory->markLocaleLoaded(lang);
	}
	catch (...) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Unknown exception in map initialization thread.");
		mapInventory->markLocaleLoaded(lang);
	}
}

bool MapLoaderService::loadMapFromAPI(std::string lang, int mapId) {
	try {
		if (unloading.load()) return false;

		std::string mapResponse = performRequest("/v2/maps/" + std::to_string(mapId) + "?lang=" + lang);
		if (unloading.load()) return false;
		if (mapResponse.empty()) {
			APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, ("Could not load map " + std::to_string(mapId) + " from GW2 API.").c_str());
			return false;
		}

		json mapJson = json::parse(mapResponse);
		gw2::map mapInfo = {};
		mapInfo.id = mapJson.value("id", mapId);
		mapInfo.name = mapJson.value("name", std::string("Unknown"));
		mapInfo.minLevel = mapJson.value("min_level", 0);
		mapInfo.maxLevel = mapJson.value("max_level", 0);
		mapInfo.regionId = mapJson.value("region_id", 0);
		mapInfo.regionName = mapJson.value("region_name", std::string(""));
		mapInfo.continentId = mapJson.value("continent_id", 0);
		mapInfo.continentName = mapJson.value("continent_name", std::string(""));
		if (mapJson.contains("map_rect")) {
			mapJson.at("map_rect").get_to(mapInfo.mapRect);
		}
		if (mapJson.contains("continent_rect")) {
			mapJson.at("continent_rect").get_to(mapInfo.continentRect);
		}

		std::vector<int> floors;
		if (mapJson.contains("floors")) {
			mapJson.at("floors").get_to(floors);
		}
		else if (mapJson.contains("default_floor")) {
			floors.push_back(mapJson.at("default_floor").get<int>());
		}

		for (auto floorId : floors) {
			if (unloading.load()) return false;
			std::string floorResponse = performRequest("/v2/continents/" + std::to_string(mapInfo.continentId) + "/floors/" + std::to_string(floorId) + "?lang=" + lang);
			if (unloading.load()) return false;
			if (floorResponse.empty()) {
				continue;
			}

			json floorJson = json::parse(floorResponse);
			gw2api::continents::floor floor = floorJson.get<gw2api::continents::floor>();
			auto region = floor.regions.find(std::to_string(mapInfo.regionId));
			if (region == floor.regions.end()) {
				continue;
			}
			auto map = region->second.maps.find(std::to_string(mapInfo.id));
			if (map == region->second.maps.end()) {
				continue;
			}

			if (mapInfo.regionName.empty()) {
				mapInfo.regionName = region->second.name;
			}
			if (mapInfo.mapRect.empty()) {
				mapInfo.mapRect = map->second.mapRect;
			}
			if (mapInfo.continentRect.empty()) {
				mapInfo.continentRect = map->second.continentRect;
			}
			for (auto sector : map->second.sectors) {
				mapInfo.sectors.emplace(sector);
			}
		}

		addDefaultSector(mapInfo);
		mapInventory->addMap(lang, mapInfo);
		APIDefs->Log(ELogLevel_INFO, ADDON_NAME, ("Loaded missing map " + std::to_string(mapInfo.id) + " for locale '" + lang + "' from GW2 API.").c_str());
		return true;
	}
	catch (const std::exception& e) {
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, ("Exception loading missing map from GW2 API: " + std::string(e.what())).c_str());
	}
	catch (...) {
		APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception loading missing map from GW2 API.");
	}

	return false;
}

/// <summary>
/// Loads all maps from GW2 API and stores them in the mapInventory.
/// </summary>
void MapLoaderService::loadAllMapsFromApi() {
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Preloading map data from GW2 API.");
	for (auto lang : SUPPORTED_LOCAL) {
		APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, ("Loading maps for locale: " + lang).c_str());
		std::map<std::string, gw2::map> mapInfos = std::map<std::string, gw2::map>();

		auto continentsResponse = performRequest("/v2/continents?lang=" + lang);
		if (continentsResponse.empty()) {
			APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Could not request continents data within retry limits!");
			return;
		}
		json continentsJson = json::parse(continentsResponse);

		for (auto continent : continentsJson.get<std::vector<int>>()) {
			if (unloading.load()) break;
			auto continentResponse = performRequest("/v2/continents/" + std::to_string(continent) + "?lang=" + lang);
			if (continentResponse.empty()) {
				APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Could not request floor data within retry limits!");
				return;
			}

			json continentJson = json::parse(continentResponse);

			for (auto floor : continentJson["floors"].get<std::vector<int>>()) {
				if (unloading.load()) break;
				auto floorResponse = performRequest("/v2/continents/" + std::to_string(continent) + "/floors/" + std::to_string(floor) + "?lang=" + lang);
				if (floorResponse.empty()) {
					APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Could not request region data within retry limits!");
					return;
				}

				json floorJson = json::parse(floorResponse);

				gw2api::continents::floor floor = floorJson.get< gw2api::continents::floor>();
				if (floor.regions.size() == 0) {
					// empty floor, skip
					continue;
				}

				// for every region, for every map create a MapInfo*
				for (auto region: floor.regions)
				{
					for (auto entry: region.second.maps) {
						std::string id = entry.first;
						gw2api::continents::map* map = &entry.second;

						gw2api::continents::map mapInfo;

						if (mapInfos.count(id)) {
							mapInfo = mapInfos[id];
						}
						else {
							mapInfo = *map;
							// enrich with additional data
							mapInfo.continentId = continentJson["id"];
							mapInfo.continentName = continentJson["name"];
							mapInfo.regionId = region.second.id;
							mapInfo.regionName = region.second.name;
							// end enrichment
							mapInfos.emplace(id, mapInfo);
						}

						for (auto sector: map->sectors)
						{
							mapInfo.sectors.emplace(sector);
						}
					}
				}
			}
		}

		// precautionary if we made it here without unloading, store the map data
		if (unloading.load()) return;

		// fill up empty maps with default sectors
		for (auto map: mapInfos)
		{
			// add default sector to maps without sectors
			if (map.second.sectors.size() == 0) {
				gw2api::continents::sector empty = gw2api::continents::sector();
				empty.id = -1;
				empty.name = map.second.name;
				empty.level = 80;
				empty.chatLink = "undefined";
				empty.bounds.push_back({ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() });
				empty.bounds.push_back({ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() });
				map.second.sectors.emplace("-1", empty);
			}
		}

		// add all maps found to the inventory
		for (auto map : mapInfos) {
			mapInventory->addMap(lang, map.second);
		}

		std::stringstream stream;
		stream << "Maps for locale '" << lang << "' in inventory: " << std::to_string(mapInventory->getLoadedMaps(lang).size());
		APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, stream.str().c_str());
	

	}
	APIDefs->Log(ELogLevel::ELogLevel_INFO, ADDON_NAME, "Map loading from API complete.");
}

/*
bool MapLoaderService::loadFromApi(int mapId) {
	// Possible TODO: refactor this to be more efficient;
	// Map loading this way may take 30+ seconds per map, lol

	// Build request URI
	std::string uri = "/v2/maps/";
	uri += std::to_string(mapId);
	uri += "?lang=en"; // TODO can I get the language from Mumble?

	// Log the update call in our debug log, please
	std::string logmsg = "Requesting: " + uri;
	APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logmsg.c_str());

	retryCounter = 0;
	httplib::Result res = performRequest(uri);
	if (res == nullptr) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Could not request map data within retry limits!");
		return false;
	}

	int status = res->status;
	std::string data = res->body;

	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Status: " + std::to_string(status)).c_str());
	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Data: " + data).c_str());

	// TODO here we wanna put it into "cool json library" and do as the above
	if (status == 200) {
		nlohmann::json mapJson = nlohmann::json::parse(data);
		std::string name = mapJson["name"].get<std::string>();
		int regionId = mapJson["region_id"].get<int>();
		std::string regionName = mapJson["region_name"].get<std::string>();
		int continentId = mapJson["continent_id"].get<int>();
		std::string continentName = mapJson["continent_name"].get<std::string>();

		MapInfo* mapInfo = new MapInfo(mapId, name, regionId, regionName, continentId, continentName);

		// TODO iterate floors for sectors pew pew
		std::vector<int> floors = mapJson["floors"];
		for (auto floor : floors)
		{
			if (this->unloading) return false; // skip further loading if we are currently unloading the addon
			loadSectors(floor, mapInfo);
		}
		mapInventory->addMap(mapInfo);
		return true;
	}
	else {
		std::stringstream stream;
		stream << "Bad response from server, could not load map data for mapId " << std::to_string(mapId);
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, stream.str().c_str());
	}

	return false;
}

void MapLoaderService::loadSectors(int floor, MapInfo* mapInLoad) {
	// Build request URI
	// v2/continents/{continent_id}/floors/{floor_id}/regions/{region_id}/maps/{map_id}/sectors/{sector_id}

	std::stringstream getAllSectorsUri;
	getAllSectorsUri << "/v2/continents/"
		<< std::to_string(mapInLoad->getContinentId())
		<< "/floors/"
		<< std::to_string(floor)
		<< "/regions/"
		<< std::to_string(mapInLoad->getRegionId())
		<< "/maps/"
		<< std::to_string(mapInLoad->getId())
		<< "/sectors";

	std::string logmsg = "Requesting: " + getAllSectorsUri.str();
	APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logmsg.c_str());

	retryCounter = 0;
	httplib::Result res = performRequest(getAllSectorsUri.str());
	if (res == nullptr) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Could not request sector list within retry limits!");
		return;
	}

	int status = res->status;
	std::string data = res->body;
	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Status: " + std::to_string(status)).c_str());
	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Data: " + data).c_str());

	if (status == 200) {
		// Ideally we get an array of sector Ids here
		nlohmann::json sectorsJson = nlohmann::json::parse(data);
		std::vector<int> sectorIds = sectorsJson.get<std::vector<int>>();
		for (auto sectorId: sectorIds)
		{
			if (this->unloading) return; // skip further loading if we are currently unloading the addon

			// check if we already know that sector, and if so, skip it
			if(mapInLoad->hasSector(sectorId)) {
				continue;
			}

			loadSector(floor, sectorId, mapInLoad);
		}
	}
	else {
		std::stringstream stream;
		stream << "Bad response from server, could not load sector data for mapId " << std::to_string(mapInLoad->getId());
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, stream.str().c_str());

		// Likely the map has an invalid region, floor or whatever, which could possibly indicate it is a single sector map so add that one
		std::string name = mapInLoad->getName();
		int id = mapInLoad->getId();
		Sector* newSector = new Sector(id, name, -1, "");
		newSector->addBound(std::numeric_limits<float>::min(), std::numeric_limits<float>::min());
		newSector->addBound(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		mapInLoad->addSector(newSector);
	}

}
void MapLoaderService::loadSector(int floor, int sectorId, MapInfo* mapInLoad) {

	std::stringstream getSingleSectorUri;
	getSingleSectorUri << "/v2/continents/"
		<< std::to_string(mapInLoad->getContinentId())
		<< "/floors/"
		<< std::to_string(floor)
		<< "/regions/"
		<< std::to_string(mapInLoad->getRegionId())
		<< "/maps/"
		<< std::to_string(mapInLoad->getId())
		<< "/sectors/"
		<< std::to_string(sectorId);
	std::string logmsg = "Requesting: " + getSingleSectorUri.str();
	APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logmsg.c_str());

	retryCounter = 0;
	httplib::Result sectorResponse = performRequest(getSingleSectorUri.str());
	if (sectorResponse == nullptr) {
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, "Could not request sector list within retry limits!");
		return;
	}

	int sectorStatus = sectorResponse->status;
	std::string sectorData = sectorResponse->body;
	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Status: " + std::to_string(sectorStatus)).c_str());
	APIDefs->Log(ELogLevel_TRACE, ADDON_NAME, ("Data: " + sectorData).c_str());

	if (sectorStatus == 200) {
		nlohmann::json sectorJson = nlohmann::json::parse(sectorData);
		std::string name = sectorJson["name"].get<std::string>();
		int level = sectorJson["level"].get<int>();
		std::string chatLink = sectorJson["chat_link"].get<std::string>();


		Sector* newSector = new Sector(sectorId, name, level, chatLink);

		std::vector<std::vector<float>> bounds = sectorJson["bounds"].get<std::vector<std::vector<float>>>();

		for (auto bound: bounds)
		{
			if (this->unloading) return; // skip further loading if we are currently unloading the addon

			newSector->addBound(bound[0], bound[1]);
		}
		mapInLoad->addSector(newSector);
	}
	else {
		std::stringstream stream;
		stream << "Bad response from server, could not load sector data for mapId " << std::to_string(mapInLoad->getId());
		APIDefs->Log(ELogLevel_CRITICAL, ADDON_NAME, stream.str().c_str());
	}
} */
