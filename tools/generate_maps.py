#!/usr/bin/env python3
"""Rebuild src/maps/maps.zip from the live Guild Wars 2 continents API.

The addon reads one JSON file per locale. Each file is a synthetic region:
  { "id": -1, "name": "<locale>", "continent_rect": [], "maps": { "<id>": ... } }

Usage:
  python3 tools/generate_maps.py
  python3 tools/generate_maps.py --fresh
  python3 tools/generate_maps.py --locales zh,en --workers 6
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time
import zipfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

BASE_URL = "https://api.guildwars2.com"
LOCALES = ("en", "de", "es", "fr", "zh")
USER_AGENT = "GW2Nexus-RegionsOfTyria-mapgen/1.0"
UI_SEED = "Español Français 中文 泰瑞亚 科瑞塔 狮子拱门 迷雾之地"
DEFAULT_SECTOR_BOUNDS = [
    [-3.4028234663852886e38, -3.4028234663852886e38],
    [3.4028234663852886e38, 3.4028234663852886e38],
]
REQUIRED_NEW_MAPS = (1550, 1554, 1564, 1622, 1625)


class RateLimiter:
    def __init__(self, rate: float) -> None:
        self.min_interval = 1.0 / rate
        self.lock = threading.Lock()
        self.next_time = 0.0

    def wait(self) -> None:
        with self.lock:
            now = time.monotonic()
            wait_s = max(0.0, self.next_time - now)
            self.next_time = max(now, self.next_time) + self.min_interval
        if wait_s:
            time.sleep(wait_s)


class Gw2Client:
    def __init__(self, cache_dir: Path, rate: float, fresh: bool) -> None:
        self.cache_dir = cache_dir
        self.fresh = fresh
        self.limiter = RateLimiter(rate)
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def get_json(self, path: str, params: dict[str, str] | None = None) -> Any:
        query = urlencode(params or {})
        cache_name = path.strip("/").replace("/", "_")
        if query:
            cache_name += "_" + query.replace("=", "-").replace("&", "_")
        cache_file = self.cache_dir / f"{cache_name}.json"

        if not self.fresh and cache_file.exists():
            with cache_file.open("r", encoding="utf-8") as handle:
                cached = json.load(handle)
            if isinstance(cached, dict) and cached.get("_missing"):
                return None
            return cached

        payload = self._request(f"{BASE_URL}{path}", params)
        if payload is None:
            cache_file.write_text(json.dumps({"_missing": True}), encoding="utf-8")
            return None
        cache_file.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
        return payload

    def _request(self, url: str, params: dict[str, str] | None) -> Any:
        if params:
            url = f"{url}?{urlencode(params)}"
        last_error: Exception | None = None
        for attempt in range(8):
            self.limiter.wait()
            request = Request(url, headers={"User-Agent": USER_AGENT})
            try:
                with urlopen(request, timeout=120) as response:
                    return json.load(response)
            except HTTPError as exc:
                last_error = exc
                if exc.code == 404:
                    return None
                if exc.code in (429, 500, 502, 503, 504):
                    time.sleep(min(30.0, 1.5 ** attempt))
                    continue
                raise
            except (URLError, TimeoutError, json.JSONDecodeError) as exc:
                last_error = exc
                time.sleep(min(30.0, 1.5 ** attempt))
        raise RuntimeError(f"Failed {url}: {last_error}")


def normalize_sector(raw: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": raw.get("id", -1),
        "level": raw.get("level", 0),
        "name": raw.get("name", ""),
        "chat_link": raw.get("chat_link", ""),
        "bounds": raw.get("bounds", []),
    }


def normalize_map(
    raw: dict[str, Any],
    continent_id: int,
    continent_name: str,
    region_id: int,
    region_name: str,
) -> dict[str, Any]:
    sectors = {
        str(sector_id): normalize_sector(sector)
        for sector_id, sector in (raw.get("sectors") or {}).items()
        if isinstance(sector, dict)
    }
    return {
        "id": raw.get("id"),
        "min_level": raw.get("min_level", 0),
        "max_level": raw.get("max_level", 0),
        "name": raw.get("name", ""),
        "map_rect": raw.get("map_rect", []),
        "continent_rect": raw.get("continent_rect", []),
        "regionId": region_id,
        "regionName": region_name,
        "continentId": continent_id,
        "continentName": continent_name,
        "sectors": sectors,
    }


def add_default_sector(map_info: dict[str, Any]) -> None:
    if map_info.get("sectors"):
        return
    map_info["sectors"] = {
        "-1": {
            "id": -1,
            "level": 80,
            "name": map_info.get("name", ""),
            "chat_link": "undefined",
            "bounds": DEFAULT_SECTOR_BOUNDS,
        }
    }


def merge_floor(
    maps: dict[str, dict[str, Any]],
    floor: dict[str, Any],
    continent_id: int,
    continent_name: str,
) -> None:
    for region in (floor.get("regions") or {}).values():
        if not isinstance(region, dict):
            continue
        region_id = int(region.get("id") or 0)
        region_name = region.get("name") or ""
        for map_id, raw_map in (region.get("maps") or {}).items():
            if not isinstance(raw_map, dict):
                continue
            incoming = normalize_map(
                raw_map,
                continent_id,
                continent_name,
                region_id,
                region_name,
            )
            existing = maps.get(str(map_id))
            if existing is None:
                maps[str(map_id)] = incoming
                continue
            existing["sectors"].update(incoming["sectors"])
            if not existing.get("name") and incoming.get("name"):
                existing["name"] = incoming["name"]
            if not existing.get("map_rect") and incoming.get("map_rect"):
                existing["map_rect"] = incoming["map_rect"]
            if not existing.get("continent_rect") and incoming.get("continent_rect"):
                existing["continent_rect"] = incoming["continent_rect"]


def unique_non_ascii(text: str) -> str:
    seen: set[str] = set()
    chars: list[str] = []
    for char in text:
        if ord(char) < 0x80 or char in seen:
            continue
        seen.add(char)
        chars.append(char)
    return "".join(chars)


def collect_seed_text(locale_maps: dict[str, dict[str, dict[str, Any]]]) -> str:
    chunks = [UI_SEED]
    for maps in locale_maps.values():
        for map_info in maps.values():
            chunks.append(map_info.get("name") or "")
            chunks.append(map_info.get("regionName") or "")
            chunks.append(map_info.get("continentName") or "")
            for sector in map_info.get("sectors", {}).values():
                chunks.append(sector.get("name") or "")
    return unique_non_ascii("".join(chunks))


def fetch_continents(client: Gw2Client) -> list[dict[str, Any]]:
    continent_ids = client.get_json("/v2/continents")
    continents: list[dict[str, Any]] = []
    for continent_id in continent_ids:
        continent = client.get_json(f"/v2/continents/{continent_id}")
        if not continent:
            raise RuntimeError(f"Could not load continent {continent_id}")
        continents.append(continent)
        print(
            f"continent {continent_id} {continent.get('name')} "
            f"floors={len(continent.get('floors') or [])}",
            flush=True,
        )
    return continents


def fetch_continent_names(client: Gw2Client, locale: str, continents: list[dict[str, Any]]) -> dict[int, str]:
    names: dict[int, str] = {}
    for continent in continents:
        continent_id = int(continent["id"])
        localized = client.get_json(f"/v2/continents/{continent_id}", {"lang": locale})
        names[continent_id] = (localized or continent).get("name") or continent.get("name") or ""
    print(f"[{locale}] continent names {names}", flush=True)
    return names


def apply_continent_names(maps: dict[str, dict[str, Any]], names: dict[int, str]) -> None:
    for map_info in maps.values():
        continent_id = int(map_info.get("continentId") or 0)
        localized = names.get(continent_id)
        if localized:
            map_info["continentName"] = localized


def load_locale_maps(
    client: Gw2Client,
    locale: str,
    continents: list[dict[str, Any]],
    workers: int,
    floor_limit: int | None,
) -> dict[str, dict[str, Any]]:
    jobs: list[tuple[dict[str, Any], int]] = []
    for continent in continents:
        floors = [int(floor_id) for floor_id in (continent.get("floors") or [])]
        if floor_limit is not None:
            floors = floors[:floor_limit]
        for floor_id in floors:
            jobs.append((continent, floor_id))

    maps: dict[str, dict[str, Any]] = {}
    completed = 0
    lock = threading.Lock()
    continent_names = fetch_continent_names(client, locale, continents)

    def fetch_one(continent: dict[str, Any], floor_id: int) -> tuple[dict[str, Any], dict[str, Any] | None]:
        payload = client.get_json(
            f"/v2/continents/{continent['id']}/floors/{floor_id}",
            {"lang": locale},
        )
        return continent, payload if isinstance(payload, dict) else None

    print(f"[{locale}] fetching {len(jobs)} floors with {workers} workers", flush=True)
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(fetch_one, continent, floor_id) for continent, floor_id in jobs]
        for future in as_completed(futures):
            continent, payload = future.result()
            with lock:
                if payload:
                    continent_id = int(continent["id"])
                    merge_floor(
                        maps,
                        payload,
                        continent_id,
                        continent_names.get(continent_id) or continent.get("name") or "",
                    )
                completed += 1
                if completed % 15 == 0 or completed == len(jobs):
                    print(
                        f"[{locale}] {completed}/{len(jobs)} floors, maps={len(maps)}",
                        flush=True,
                    )

    apply_continent_names(maps, continent_names)
    for map_info in maps.values():
        add_default_sector(map_info)
    return maps


def region_payload(locale: str, maps: dict[str, dict[str, Any]]) -> dict[str, Any]:
    return {
        "id": -1,
        "name": locale,
        "continent_rect": [],
        "maps": dict(sorted(maps.items(), key=lambda item: int(item[0]))),
    }


def validate_maps(locale: str, maps: dict[str, dict[str, Any]]) -> list[str]:
    errors: list[str] = []
    if not maps:
        errors.append(f"{locale}: no maps loaded")
        return errors
    for map_id in REQUIRED_NEW_MAPS:
        if str(map_id) not in maps:
            errors.append(f"{locale}: missing map {map_id}")
    queensdale = maps.get("15")
    if queensdale is None:
        errors.append(f"{locale}: missing Queensdale (15)")
    elif len(queensdale.get("sectors") or {}) < 3:
        errors.append(f"{locale}: Queensdale has too few sectors")
    sample = maps.get("1550") or next(iter(maps.values()))
    for key in ("id", "name", "map_rect", "continent_rect", "sectors", "regionId", "continentId"):
        if key not in sample:
            errors.append(f"{locale}: map missing {key}")
    if locale == "zh":
        queensdale_continent = (maps.get("15") or {}).get("continentName")
        if queensdale_continent != "泰瑞亚":
            errors.append(f"zh: Queensdale continentName is {queensdale_continent!r}")
        mists_name = next((m.get("continentName") for m in maps.values() if m.get("continentId") == 2), "")
        if mists_name != "迷雾之地":
            errors.append(f"zh: Mists continentName is {mists_name!r}")
    return errors


def write_zip(
    output: Path,
    locale_maps: dict[str, dict[str, dict[str, Any]]],
    seed: str,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = output.with_suffix(".zip.tmp")
    if tmp_path.exists():
        tmp_path.unlink()
    with zipfile.ZipFile(tmp_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for locale, maps in locale_maps.items():
            payload = json.dumps(region_payload(locale, maps), ensure_ascii=False, separators=(",", ":"))
            archive.writestr(f"{locale}.json", payload.encode("utf-8"))
            print(f"wrote {locale}.json maps={len(maps)} bytes={len(payload)}", flush=True)
        archive.writestr("cjk_seed.txt", seed.encode("utf-8"))
        print(f"wrote cjk_seed.txt chars={len(seed)}", flush=True)
    tmp_path.replace(output)
    print(f"wrote {output} ({output.stat().st_size} bytes)", flush=True)


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Generate packed map JSON for Regions of Tyria")
    parser.add_argument("--locales", default=",".join(LOCALES), help="Comma-separated locale list")
    parser.add_argument("--out", type=Path, default=repo_root / "src" / "maps" / "maps.zip")
    parser.add_argument("--cache-dir", type=Path, default=repo_root / "tools" / ".maps_cache")
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--rate", type=float, default=8.0, help="Max API requests per second")
    parser.add_argument("--fresh", action="store_true", help="Ignore cached API responses")
    parser.add_argument("--floor-limit", type=int, default=None, help="Debug: only first N floors per continent")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    locales = tuple(locale.strip() for locale in args.locales.split(",") if locale.strip())
    unknown = [locale for locale in locales if locale not in LOCALES]
    if unknown:
        print(f"unsupported locales: {unknown}", file=sys.stderr)
        return 2

    client = Gw2Client(args.cache_dir, rate=args.rate, fresh=args.fresh)
    continents = fetch_continents(client)
    locale_maps: dict[str, dict[str, dict[str, Any]]] = {}
    errors: list[str] = []

    for locale in locales:
        maps = load_locale_maps(client, locale, continents, args.workers, args.floor_limit)
        locale_maps[locale] = maps
        locale_errors = validate_maps(locale, maps)
        errors.extend(locale_errors)
        print(f"[{locale}] complete maps={len(maps)}", flush=True)

    if errors:
        for error in errors:
            print(f"validation error: {error}", file=sys.stderr)
        if args.floor_limit is None:
            return 1
        print("floor-limit run: continuing despite validation errors", flush=True)

    seed = collect_seed_text(locale_maps)
    write_zip(args.out, locale_maps, seed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
