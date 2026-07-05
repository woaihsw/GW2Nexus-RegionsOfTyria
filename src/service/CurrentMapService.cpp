#include "CurrentMapService.h"

CurrentMapService::CurrentMapService() {
	currentMap = nullptr;
}
CurrentMapService::~CurrentMapService() {}

MapData* CurrentMapService::getCurrentMap() {

	if (!NexusLink->IsGameplay) {
		return nullptr; // do not update while not in gameplay
	}
	
	int currentMapId = MumbleLink->Context.MapID;
	auto position = MumbleLink->AvatarPosition;

	std::string localestr = GetLocaleAsString(settings.locale);	
	gw2::map* map = mapInventory->getMapInfo(localestr, currentMapId);
	if (map == nullptr) {
		if (mapInventory->isLocaleLoaded(localestr)) {
			RequestMapLoad(localestr, currentMapId);
		}
		else {
			EnsureLocaleMapsLoaded(localestr);
		}
		return nullptr;
	}

	SectorData currentSector = SectorData();
	// To detect the current sector we can iterate over the sectors and check if the position is within all boundaries;
	// MumbleLink->Context.Compass.PlayerPosition has the global position

	// we need at least 3 
	if (map->sectors.size() < 3) {
		currentSector.id = -1;
		currentSector.name = map->name; // use map name instead
	}
	else {
		float x, y;

		if (MumbleLink->Context.IsCompetitive) {
			gw2::coordinate calcPos = calculatePos();
			x = calcPos.x;
			y = calcPos.y;
		}
		else {
			x = MumbleLink->Context.Compass.PlayerPosition.X;
			y = MumbleLink->Context.Compass.PlayerPosition.Y;
		}

		for (auto sector : map->sectors) {
			bool inside = false;
			size_t count = sector.second.bounds.size();

			//Ray - Casting - Algorithm, sponsored by ChatGPT
			for (int i = 0, j = count - 1; i < count; j = i++) {
				if ((sector.second.bounds[i].y > y) != (sector.second.bounds[j].y > y) &&
					(x < (sector.second.bounds[j].x - sector.second.bounds[i].x) * (y - sector.second.bounds[i].y) / (sector.second.bounds[j].y - sector.second.bounds[i].y) + sector.second.bounds[i].x)) {
					inside = !inside;
				}
			}
			if (inside) {
				currentSector.id = sector.second.id;
				currentSector.name = sector.second.name;
				break;
			}
		}
	}
	if(currentMap != nullptr)
		if (map->id == currentMap->id 
			&& currentSector.id == currentMap->currentSector.id) 
			return currentMap; // no change, return same pointer

	currentMap = new MapData();
	currentMap->id = map->id;
	currentMap->name = map->name;
	currentMap->regionId = map->regionId;
	currentMap->regionName = map->regionName;
	currentMap->continentId = map->continentId;
	currentMap->continentName = map->continentName;
	currentMap->currentSector = currentSector;

	return currentMap;
}

gw2::coordinate CurrentMapService::calculatePos() {
	std::string localestr = GetLocaleAsString(settings.locale);
	gw2::map* map = mapInventory->getMapInfo(localestr, MumbleLink->Context.MapID);
	if (map == nullptr) {
		if (mapInventory->isLocaleLoaded(localestr)) {
			RequestMapLoad(localestr, MumbleLink->Context.MapID);
		}
		else {
			EnsureLocaleMapsLoaded(localestr);
		}
		return { 0,0 };
	}

	// convert from metres (mumble) to inches (map API) by * 39.3700787
	float x = MumbleLink->AvatarPosition.X * 39.3700787f;
	float y = MumbleLink->AvatarPosition.Z * 39.3700787f;

	float calculatedX, calculatedY;
	calculatedX = map->continentRect[0].x + (1 * (x - map->mapRect[0].x) / (map->mapRect[1].x - map->mapRect[0].x) * (map->continentRect[1].x - map->continentRect[0].x));
	calculatedY = map->continentRect[0].y + (-1 * (y - map->mapRect[1].y) / (map->mapRect[1].y - map->mapRect[0].y) * (map->continentRect[1].y - map->continentRect[0].y));

	return { calculatedX, calculatedY };
}
