#pragma once

#include "entity/GW2API_Continents.h"

#include <functional>
#include <mutex>
#include <utility>

inline std::string mapNameText(const gw2api::continents::map& map) {
	std::string text = map.name + "\n" + map.regionName + "\n" + map.continentName;
	for (const auto& [id, sector] : map.sectors) text += "\n" + sector.name;
	return text;
}

// The worker only submits data. Preparation and publication happen together on
// the render thread, before the popup can observe a new map or locale.
class MapFontPreparation {
public:
	struct Batch {
		std::string locale;
		std::vector<gw2api::continents::map> maps;
		bool completesLocale = false;
	};
	void submit(Batch batch) {
		std::lock_guard<std::mutex> lock(mutex);
		incoming.push_back(std::move(batch));
	}
	template<typename AddText, typename Prepare, typename Publish>
	bool advance(AddText addText, Prepare prepare, Publish publish) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (auto& batch : incoming) {
				for (const auto& map : batch.maps) addText(mapNameText(map));
				waiting.push_back(std::move(batch));
			}
			incoming.clear();
		}
		if (!prepare()) return false;
		for (auto& batch : waiting) publish(std::move(batch));
		waiting.clear();
		return true;
	}
	// Only after the worker has stopped.
	void clear() {
		std::lock_guard<std::mutex> lock(mutex);
		incoming.clear();
		waiting.clear();
	}
private:
	std::mutex mutex;
	std::vector<Batch> incoming;
	std::vector<Batch> waiting;
};
