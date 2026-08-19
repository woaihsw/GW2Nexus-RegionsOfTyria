#ifndef CURRENT_MAP_SERVICE_H
#define CURRENT_MAP_SERVICE_H

#include "../Globals.h"

struct SectorData {
	int id;
	std::string name;
};

struct MapData {
	int id;
	std::string name;
	int regionId;
	std::string regionName;
	int continentId;
	std::string continentName;

	SectorData currentSector;
};

class CurrentMapService {
public:
	CurrentMapService();
	~CurrentMapService();

	MapData* getCurrentMap();
	gw2::coordinate calculatePos();

private:
	MapData currentMap;
	bool hasCurrentMap = false;
};

#endif
