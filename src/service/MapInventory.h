#ifndef MAP_INVENTORY_H
#define MAP_INVENTORY_H

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include "../entity/GW2API_Continents.h"

namespace gw2 = gw2api::continents;

class MapInventory {
public:
	MapInventory();

	void addMap(std::string locale, gw2::map mapInfo);
	gw2::map* getMapInfo(std::string locale, int id);
	std::map<int, gw2::map*> getLoadedMaps(std::string locale);
	bool isLocaleLoaded(std::string locale);
	void markLocaleLoaded(std::string locale);
	void clear();

	bool isWvWMap(int id);

private:
	std::mutex inventoryMutex;
	std::set<std::string> loadedLocales;
	std::map<std::string, std::map<int, std::unique_ptr<gw2::map>>> loadedMaps;
};

#endif
