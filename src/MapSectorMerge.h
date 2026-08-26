#ifndef MAP_SECTOR_MERGE_H
#define MAP_SECTOR_MERGE_H

#include "entity/GW2API_Continents.h"

#include <limits>
#include <map>
#include <string>

inline void mergeMapSectors(gw2api::continents::map& dest, const gw2api::continents::map& source) {
	for (const auto& sector : source.sectors) {
		dest.sectors.emplace(sector);
	}
}

inline void addDefaultSector(gw2api::continents::map& mapInfo) {
	if (!mapInfo.sectors.empty()) {
		return;
	}

	gw2api::continents::sector empty{};
	empty.id = -1;
	empty.name = mapInfo.name;
	empty.level = 80;
	empty.chatLink = "undefined";
	empty.bounds.push_back({ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() });
	empty.bounds.push_back({ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() });
	mapInfo.sectors.emplace("-1", empty);
}

inline gw2api::continents::map& storedMapForMerge(
	std::map<std::string, gw2api::continents::map>& mapInfos,
	const std::string& id,
	const gw2api::continents::map& incoming) {
	auto found = mapInfos.find(id);
	if (found == mapInfos.end()) {
		found = mapInfos.emplace(id, incoming).first;
	}
	return found->second;
}

#endif
