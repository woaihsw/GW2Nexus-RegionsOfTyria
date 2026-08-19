#include "CurrentMapService.h"
#include "../SectorGeometry.h"

CurrentMapService::CurrentMapService() {}
CurrentMapService::~CurrentMapService() {}

MapData* CurrentMapService::getCurrentMap() {
	if (!NexusLink->IsGameplay) {
		return nullptr;
	}

	int currentMapId = MumbleLink->Context.MapID;
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
	if (map->sectors.size() < 3) {
		currentSector.id = -1;
		currentSector.name = map->name;
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

		for (auto& sector : map->sectors) {
			if (pointInPolygon(x, y, sector.second.bounds)) {
				currentSector.id = sector.second.id;
				currentSector.name = sector.second.name;
				break;
			}
		}
	}

	if (hasCurrentMap
		&& map->id == currentMap.id
		&& currentSector.id == currentMap.currentSector.id) {
		return &currentMap;
	}

	currentMap.id = map->id;
	currentMap.name = map->name;
	currentMap.regionId = map->regionId;
	currentMap.regionName = map->regionName;
	currentMap.continentId = map->continentId;
	currentMap.continentName = map->continentName;
	currentMap.currentSector = currentSector;
	hasCurrentMap = true;
	return &currentMap;
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

	float x = MumbleLink->AvatarPosition.X * 39.3700787f;
	float y = MumbleLink->AvatarPosition.Z * 39.3700787f;

	float calculatedX = map->continentRect[0].x + (1 * (x - map->mapRect[0].x) / (map->mapRect[1].x - map->mapRect[0].x) * (map->continentRect[1].x - map->continentRect[0].x));
	float calculatedY = map->continentRect[0].y + (-1 * (y - map->mapRect[1].y) / (map->mapRect[1].y - map->mapRect[0].y) * (map->continentRect[1].y - map->continentRect[0].y));
	return { calculatedX, calculatedY };
}
