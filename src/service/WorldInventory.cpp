#include "WorldInventory.h"

#include <algorithm>

WorldInventory::WorldInventory() {}

void WorldInventory::addWorld(std::string locale, gw2api::worlds::world world) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	const int id = world.id;
	this->loadedWorlds[locale][id] = std::make_unique<gw2api::worlds::world>(std::move(world));
}

void WorldInventory::addAlliance(std::string locale, gw2api::worlds::alliance alliance) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	const int id = alliance.id;
	this->loadedAlliances[locale][id] = std::make_unique<gw2api::worlds::alliance>(std::move(alliance));
}

gw2api::worlds::world* WorldInventory::getWorld(std::string locale, int id) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	auto localeIt = this->loadedWorlds.find(locale);
	if (localeIt == this->loadedWorlds.end()) {
		return nullptr;
	}
	auto worldIt = localeIt->second.find(id);
	if (worldIt == localeIt->second.end()) {
		return nullptr;
	}
	return worldIt->second.get();
}

gw2api::worlds::alliance* WorldInventory::getAlliance(std::string locale, int id) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	auto localeIt = this->loadedAlliances.find(locale);
	if (localeIt == this->loadedAlliances.end()) {
		return nullptr;
	}
	auto allianceIt = localeIt->second.find(id);
	if (allianceIt == localeIt->second.end()) {
		return nullptr;
	}
	return allianceIt->second.get();
}

std::vector<gw2api::worlds::world*> WorldInventory::getAllWorlds(std::string locale) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	std::vector<gw2api::worlds::world*> worlds;
	auto localeIt = this->loadedWorlds.find(locale);
	if (localeIt != this->loadedWorlds.end()) {
		for (auto& entry : localeIt->second) {
			worlds.push_back(entry.second.get());
		}
	}
	std::sort(worlds.begin(), worlds.end(), [](const auto& a, const auto& b) {
		return a->name < b->name;
	});
	return worlds;
}

std::vector<gw2api::worlds::alliance*> WorldInventory::getAllAlliances(std::string locale) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	std::vector<gw2api::worlds::alliance*> alliances;
	auto localeIt = this->loadedAlliances.find(locale);
	if (localeIt != this->loadedAlliances.end()) {
		for (auto& entry : localeIt->second) {
			alliances.push_back(entry.second.get());
		}
	}
	std::sort(alliances.begin(), alliances.end(), [](const auto& a, const auto& b) {
		return a->name < b->name;
	});
	return alliances;
}

void WorldInventory::clear() {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	this->loadedWorlds.clear();
	this->loadedAlliances.clear();
}
