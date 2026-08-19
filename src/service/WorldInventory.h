#ifndef WORLD_INVENTORY_H
#define WORLD_INVENTORY_H

#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "../entity/GW2API_Worlds.h"

class WorldInventory {
public:
	WorldInventory();

	void addWorld(std::string locale, gw2api::worlds::world world);
	void addAlliance(std::string locale, gw2api::worlds::alliance alliance);

	gw2api::worlds::world* getWorld(std::string locale, int id);
	gw2api::worlds::alliance* getAlliance(std::string locale, int id);

	std::vector<gw2api::worlds::world*> getAllWorlds(std::string locale);
	std::vector<gw2api::worlds::alliance*> getAllAlliances(std::string locale);
	void clear();

private:
	std::mutex inventoryMutex;
	std::map<std::string, std::map<int, std::unique_ptr<gw2api::worlds::world>>> loadedWorlds;
	std::map<std::string, std::map<int, std::unique_ptr<gw2api::worlds::alliance>>> loadedAlliances;
};

#endif
