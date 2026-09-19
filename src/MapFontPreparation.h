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
	template<typename AddText, typename Prepare, typename Covered, typename Publish>
	bool advance(AddText addText, Prepare prepare, Covered covered, Publish publish) {
		std::vector<Batch> received;
		{
			std::lock_guard<std::mutex> lock(mutex);
			received.swap(incoming);
		}
		for (auto& batch : received) {
			for (const auto& map : batch.maps) addText(mapNameText(map));
			waiting.push_back(std::move(batch));
		}
		prepare();
		for (auto it = waiting.begin(); it != waiting.end();) {
			Batch published{it->locale, {}, it->completesLocale};
			std::vector<gw2api::continents::map> pending;
			for (auto& map : it->maps) {
				if (covered(mapNameText(map))) published.maps.push_back(std::move(map));
				else pending.push_back(std::move(map));
			}
			// Locale enumeration can finish with isolated maps still pending.
			// This allows requests for other, previously unknown map IDs.
			if (!published.maps.empty() || published.completesLocale) publish(std::move(published));
			it->completesLocale = false;
			it->maps = std::move(pending);
			if (it->maps.empty()) it = waiting.erase(it);
			else ++it;
		}
		return waiting.empty();
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
