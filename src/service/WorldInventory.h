#ifndef WORLD_INVENTORY_H
#define WORLD_INVENTORY_H

#include <map>
#include <mutex>
#include "../entity/GW2API_Worlds.h"

class WorldInventory {
public:
	WorldInventory();

	void addWorld(std::string locale, gw2api::worlds::world* world);
	void addAlliance(std::string locale, gw2api::worlds::alliance * alliance);

	gw2api::worlds::world* getWorld(std::string locale, int id);
	gw2api::worlds::alliance* getAlliance(std::string locale, int id);

	std::vector<gw2api::worlds::world*> getAllWorlds(std::string locale);
	std::vector < gw2api::worlds::alliance*> getAllAlliances(std::string locale);

private:
	std::mutex inventoryMutex;

	/// <summary>
	/// Map with the loaded maps per locale
	/// outer map: locale -> map
	/// inner map: worldId -> world data
	/// </summary>
	std::map<std::string, std::map<int, gw2api::worlds::world*>> loadedWorlds = std::map<std::string, std::map<int, gw2api::worlds::world*>>();

	/// <summary>
	/// Same as above but for alliances
	/// </summary>
	std::map<std::string, std::map<int, gw2api::worlds::alliance*>> loadedAlliances = std::map<std::string, std::map<int, gw2api::worlds::alliance*>>();
};

#endif /* WORLD_INVENTORY_H */
