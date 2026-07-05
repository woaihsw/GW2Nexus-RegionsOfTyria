#ifndef MAP_LOADER_SERVICE_H
#define MAP_LOADER_SERVICE_H

#include <nlohmann/json.hpp>
#include <mutex>
#include <set>
#include <thread>

#include "../Globals.h"
#include "../entity/GW2API_Continents.h"
#include "../entity/GW2API_Worlds.h"
#include "../entity/GW2API_WvW.h"

class MapLoaderService {
public:
	MapLoaderService();
	~MapLoaderService();

	void reset();

	/// <summary>
	/// Startup function to initialize the Map Inventory
	/// </summary>
	void initializeMapStorage();
	void ensureLocaleLoaded(std::string locale);
	void requestMapFromAPI(std::string locale, int mapId);

	/// <summary>
	/// Loads WvW Match data from the API.
	/// Requires either an API key or world Id set in the settings.
	/// </summary>
	void loadWvWMatchFromAPI();

	void unload();
private:
	std::mutex requestMutex;
	std::set<std::string> pendingLocales;
	std::set<std::string> pendingMaps;
	std::set<std::string> failedMaps;

	std::string performRequest(std::string uri);

	void loadAllMapsFromApi();
	void loadAllMapsFromStorage();
	void loadMapsFromStorage(std::string lang);
	bool loadMapFromAPI(std::string lang, int mapId);
	void unpackMaps();

	void loadWorldsFromAPI();
	void loadAlliancesFromStorage();
};

#endif /* MAP_LOADER_SERVICE_H */
