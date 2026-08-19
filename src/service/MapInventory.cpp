#include "MapInventory.h"

MapInventory::MapInventory() {}

void MapInventory::addMap(std::string locale, gw2::map mapInfo) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	const int id = mapInfo.id;
	this->loadedMaps[locale][id] = std::make_unique<gw2::map>(std::move(mapInfo));
}

gw2::map* MapInventory::getMapInfo(std::string locale, int id) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	auto localeIt = this->loadedMaps.find(locale);
	if (localeIt == this->loadedMaps.end()) {
		return nullptr;
	}
	auto mapIt = localeIt->second.find(id);
	if (mapIt == localeIt->second.end()) {
		return nullptr;
	}
	return mapIt->second.get();
}

std::map<int, gw2::map*> MapInventory::getLoadedMaps(std::string locale) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	std::map<int, gw2::map*> result;
	auto localeIt = this->loadedMaps.find(locale);
	if (localeIt == this->loadedMaps.end()) {
		return result;
	}
	for (auto& entry : localeIt->second) {
		result[entry.first] = entry.second.get();
	}
	return result;
}

bool MapInventory::isLocaleLoaded(std::string locale) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	return this->loadedLocales.contains(locale);
}

void MapInventory::markLocaleLoaded(std::string locale) {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	this->loadedLocales.insert(locale);
}

void MapInventory::clear() {
	std::lock_guard<std::mutex> lock(inventoryMutex);
	this->loadedMaps.clear();
	this->loadedLocales.clear();
}

bool MapInventory::isWvWMap(int id) {
	return (id == 38 || id == 1099 || id == 96 || id == 95);
}
