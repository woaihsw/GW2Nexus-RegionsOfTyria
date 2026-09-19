#pragma once

#include "entity/GW2API_Continents.h"

#include <filesystem>
#include <fstream>

using CachedMaps = std::map<int, gw2api::continents::map>;

inline CachedMaps readMapCache(const std::filesystem::path& path) {
	std::ifstream input(path);
	if (!input) return {};
	const auto document = json::parse(input);
	if (document.at("version") != 1) return {};
	CachedMaps maps;
	for (const auto& item : document.at("maps")) {
		auto map = item.get<gw2api::continents::map>();
		if (map.id > 0) maps.emplace(map.id, std::move(map));
	}
	return maps;
}

// Caller replaces the destination atomically using the platform's rename API.
inline void writeMapCacheTemporary(const std::filesystem::path& path, const CachedMaps& maps) {
	json document = {{"version", 1}, {"maps", json::array()}};
	for (const auto& [id, map] : maps) document["maps"].push_back(map);
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.exceptions(std::ios::failbit | std::ios::badbit);
	output << document.dump();
	output.close();
}
