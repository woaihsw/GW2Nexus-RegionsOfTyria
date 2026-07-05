#ifndef MAP_INVENTORY_H
#define MAP_INVENTORY_H

#include <map>
#include <mutex>
#include <set>
#include "../entity/GW2API_Continents.h"

namespace gw2 = gw2api::continents;

class MapInventory {
public:
	MapInventory();

	void addMap(std::string locale, gw2::map* mapInfo);
	gw2::map* getMapInfo(std::string locale, int id);
	std::map<int, gw2::map*> getLoadedMaps(std::string locale);
	bool isLocaleLoaded(std::string locale);
	void markLocaleLoaded(std::string locale);

	bool isWvWMap(int id);

private:
	std::mutex inventoryMutex;
	std::set<std::string> loadedLocales = std::set<std::string>();

	/// <summary>
	/// Map with the loaded maps per locale
	/// outer map: locale -> map
	/// inner map: mapId -> mapData
	/// </summary>
	std::map<std::string, std::map<int, gw2::map*>> loadedMaps = std::map<std::string, std::map<int, gw2::map*>>();
};

#endif /* MAP_INVENTORY_H */
