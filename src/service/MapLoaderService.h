#ifndef MAP_LOADER_SERVICE_H
#define MAP_LOADER_SERVICE_H

#include <nlohmann/json.hpp>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <stop_token>
#include <thread>

#include "../Globals.h"
#include "../MapRetry.h"
#include "../entity/GW2API_Continents.h"
#include "../entity/GW2API_Worlds.h"
#include "../entity/GW2API_WvW.h"

class MapLoaderService {
public:
	MapLoaderService();
	~MapLoaderService();

	void reset();
	void initializeMapStorage();
	void ensureLocaleLoaded(std::string locale);
	void requestMapFromAPI(std::string locale, int mapId);
	void loadWvWMatchFromAPI();
	void unload();

private:
	void startWorker();
	void enqueueJob(std::function<void()> job);
	void workerLoop(std::stop_token stopToken);

	std::string performRequest(std::string uri);
	void loadAllMapsFromApi();
	void loadAllMapsFromStorage();
	void loadMapsFromStorage(std::string lang);
	bool loadMapFromAPI(std::string lang, int mapId);
	void unpackMaps();
	void loadWorldsFromAPI();
	void loadAlliancesFromStorage();

	std::mutex requestMutex;
	std::set<std::string> pendingLocales;
	std::set<std::string> pendingMaps;
	std::map<std::string, MapLoadRetryState> failedMaps;

	std::mutex queueMutex;
	std::condition_variable queueCv;
	std::queue<std::function<void()>> jobs;
	std::jthread worker;
};

#endif
