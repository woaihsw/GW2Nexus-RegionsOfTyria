# Regions of Tyria (Chinese locale fork)

Nexus addon for Guild Wars 2 that shows continent, region, map, and sector names when you cross a border, plus an optional on-screen widget.

This repository is a fork of [https://github.com/HeavyMetalPirate/GW2Nexus-RegionsOfTyria](https://github.com/HeavyMetalPirate/GW2Nexus-RegionsOfTyria). The original addon and its racial fonts, popup, and widget design are HeavyMetalPirate's work. This fork exists **primarily to add Chinese locale support** (`zh`): Chinese map data, CJK font fallback, and a Chinese option in Nexus settings.

It is an unofficial port of the BlishHUD module [bhm-zone-display](https://github.com/agaertner/bhm-zone-display).

## Features

- Popup text when you enter a new sector, with a fade-in / hold / fade-out
- Mini widget during gameplay
- Racial fonts (Asura, Charr, Human, Norn, Sylvari) plus a generic font; custom TTF files are supported
- Per-font layout, color, size, and format strings
- Locales: English, German, Spanish, French, and Chinese
- Packed map names from the Guild Wars 2 API; missing maps can be fetched on demand

## Install

Install from the [Releases](https://github.com/woaihsw/GW2Nexus-RegionsOfTyria/releases) page (`RegionsDisplay.dll`) or let Nexus update from `https://github.com/woaihsw/GW2Nexus-RegionsOfTyria`.

Every push and pull request builds the DLL and runs `tests/addon_units`. A GitHub Release is created only when you push a tag that **exactly matches** `AddonDef.Version` in `src/entry.cpp` (for example `1.5.1.5`, no `v` prefix):

```text
bump AddonDef.Version
→ commit
→ git tag 1.5.1.5
→ git push origin 1.5.1.5
```

Packed fonts and map JSON extract into `<GW2Install>/addons/TyrianRegions` on first launch or when the packed resource version changes.

## Settings

Nexus options for this addon cover:

1. Output language (map and sector names), including Chinese
2. Popup enable, combat/competitive hide, animation speed and duration
3. Mini widget position, width, opacity, and alignment
4. Font mode: follow character race, or force one font everywhere
5. Per-race display formats, sizes, colors, and borders (`@c` continent, `@r` region, `@m` map, `@s` sector)
6. Reload or reset packed fonts

Custom fonts live in `<GW2Install>/addons/TyrianRegions`:

- Readable text: `font_<race>.ttf`
- Fade animation: `fonts_<race>_anim.ttf`

Reloading fonts from options applies replacements. Resetting fonts overwrites those files with the packed defaults.

## Regenerating map data

Packed locale files (`en.json`, `de.json`, `es.json`, `fr.json`, `zh.json`) and `cjk_seed.txt` are built from the live continents API:

```bash
python3 tools/generate_maps.py
```

Then bump `packedResourcesVersion` in `src/Constants.h` so existing installs extract the new pack. Do not crawl the API during game login; the addon reads the packed JSON for the selected locale and only requests a missing map when you enter it.

## Developer events

Raise `EV_TYRIAN_REGIONS_CHECK` to receive `EV_TYRIAN_REGIONS_AVAILABLE` with the current `AddonDefinition`.

`EV_TYRIAN_REGIONS_SECTOR_CHANGED` is raised when the popup sector changes. Payload:

```c++
struct MapData {
	int id;
	std::string name;
	int regionId;
	std::string regionName;
	int continentId;
	std::string continentName;
	SectorData currentSector;
};

struct SectorData {
	int id;
	std::string name;
};
```

## Known issues

- A blocked `api.guildwars2.com` request cannot be cancelled mid-call; unload waits until that request returns, then joins workers.
- WvW alliance display still depends on API names that may lag the live mode.

## License

MIT, as in the original project.
