#!/usr/bin/env python3
"""Generate Encounter Journal map metadata and project-owned override assets.

Burning Crusade maps are joined from a local MoP Classic CASC export through
UiMapAssignment -> UiMapXMapArt -> UiMapArtTile. Classic maps are copied from
the build-12340-era Atlas add-on supplied by the caller.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil


MOP_BUILD = "5.5.4.68159"
MOP_BUILD_KEY = "385e98d716641a8e5011281f9c6f5bbc"
ATLAS_VERSION = "1.17.0"
ATLAS_INTERFACE = "30300"
ATLAS_COPYRIGHT = "Copyright 2005-2010 Dan Gilbert <dan.b.gilbert@gmail.com>"

TBC_MAPS = {
    269: "黑色沼泽",
    532: "卡拉赞",
    534: "海加尔山之战",
    540: "破碎大厅",
    542: "鲜血熔炉",
    543: "地狱火城墙",
    544: "玛瑟里顿的巢穴",
    545: "蒸汽地窟",
    546: "幽暗沼泽",
    547: "奴隶围栏",
    548: "毒蛇神殿",
    550: "风暴要塞",
    552: "禁魔监狱",
    553: "生态船",
    554: "能源舰",
    555: "暗影迷宫",
    556: "塞泰克大厅",
    557: "法力陵墓",
    558: "奥金尼地穴",
    560: "旧希尔斯布莱德丘陵",
    564: "黑暗神殿",
    565: "格鲁尔的巢穴",
    568: "祖阿曼",
    580: "太阳之井高地",
    585: "魔导师平台",
}

# Atlas 1.17.0 was released for Interface 30300, so these maps represent the
# pre-Cataclysm layouts required by build 12340. Multi-wing instances retain
# Atlas's separate overview plates as selectable pages.
ATLAS_MAPS = {
    33: ("影牙城堡", (("地图", "ShadowfangKeep"),)),
    34: ("监狱", (("地图", "TheStockade"),)),
    36: ("死亡矿井", (("地图", "TheDeadmines"),)),
    43: ("哀嚎洞穴", (("地图", "WailingCaverns"),)),
    47: ("剃刀沼泽", (("地图", "RazorfenKraul"),)),
    48: ("黑暗深渊", (("地图", "BlackfathomDeeps"),)),
    70: ("奥达曼", (("地图", "Uldaman"),)),
    90: ("诺莫瑞根", (("地图", "Gnomeregan"),)),
    109: ("阿塔哈卡神庙", (("地图", "TheSunkenTemple"),)),
    129: ("剃刀高地", (("地图", "RazorfenDowns"),)),
    189: (
        "血色修道院",
        (
            ("墓地", "SMGraveyard"),
            ("图书馆", "SMLibrary"),
            ("武器库", "SMArmory"),
            ("大教堂", "SMCathedral"),
        ),
    ),
    209: ("祖尔法拉克", (("地图", "ZulFarrak"),)),
    229: (
        "黑石塔",
        (("下层", "BlackrockSpireLower"), ("上层", "BlackrockSpireUpper")),
    ),
    230: ("黑石深渊", (("地图", "BlackrockDepths"),)),
    249: ("奥妮克希亚的巢穴", (("地图", "OnyxiasLair"),)),
    289: ("通灵学院", (("地图", "Scholomance"),)),
    309: ("祖尔格拉布", (("地图", "ZulGurub"),)),
    329: ("斯坦索姆", (("地图", "Stratholme"),)),
    349: ("玛拉顿", (("地图", "Maraudon"),)),
    389: ("怒焰裂谷", (("地图", "RagefireChasm"),)),
    409: ("熔火之心", (("地图", "MoltenCore"),)),
    429: (
        "厄运之槌",
        (("东区", "DireMaulEast"), ("北区", "DireMaulNorth"), ("西区", "DireMaulWest")),
    ),
    469: ("黑翼之巢", (("地图", "BlackwingLair"),)),
    509: ("安其拉废墟", (("地图", "TheRuinsofAhnQiraj"),)),
    531: ("安其拉神殿", (("地图", "TheTempleofAhnQiraj"),)),
}

TARGET_DATA_AUDIT = {
    "build": 12340,
    "locale": "zhCN",
    "activeArchive": "Data/zhCN/patch-zhCN-2.MPQ",
    "tables": {
        "WorldMapArea.dbc": {
            "records": 108,
            "bytes": 6075,
            "sha256": "90e1ec678c8226c76f4dd9f7904f05a4cb0a58c8b4d386719b24949bd55925bb",
        },
        "DungeonMap.dbc": {
            "records": 55,
            "bytes": 1781,
            "sha256": "aa31db35a2266694d8318c410712a18540d86469e3d6134eb648a1d9694f21f5",
        },
        "DungeonMapChunk.dbc": {
            "records": 622,
            "bytes": 12461,
            "sha256": "c9fda294ce565501518aa782957a5fe45d92147a79c34a5f29493194a8d318c3",
        },
    },
    "tbcInstanceMapIDsPresent": [],
    "supplementMapIDsPresentInWorldMapArea": [],
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mop-export", required=True, type=pathlib.Path)
    parser.add_argument("--atlas-addon", required=True, type=pathlib.Path)
    parser.add_argument("--lua-output", required=True, type=pathlib.Path)
    parser.add_argument("--asset-output", required=True, type=pathlib.Path)
    parser.add_argument("--manifest-output", required=True, type=pathlib.Path)
    return parser.parse_args()


def read_json(path: pathlib.Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error


def rows_by_id(root: pathlib.Path, table: str) -> dict[int, dict]:
    value = read_json(root / "dbfilesclient" / f"{table}.db2.json")
    if not isinstance(value, list) or not value:
        raise ValueError(f"{table} contains no records")
    result: dict[int, dict] = {}
    for row in value:
        row_id = int(row["ID"])
        if row_id in result:
            raise ValueError(f"duplicate {table} row {row_id}")
        result[row_id] = row
    return result


def rows(root: pathlib.Path, table: str) -> list[dict]:
    value = read_json(root / "dbfilesclient" / f"{table}.db2.json")
    if not isinstance(value, list) or not value:
        raise ValueError(f"{table} contains no records")
    return value


def validate_atlas_source(root: pathlib.Path) -> None:
    try:
        toc = (root / "Atlas" / "Atlas.toc").read_text(encoding="utf-8-sig")
        source = (root / "Atlas" / "Atlas.lua").read_text(encoding="utf-8-sig")
        license_text = (root / "Atlas" / "Docs" / "gpl-v2-enUS.txt").read_text(
            encoding="utf-8-sig"
        )
    except OSError as error:
        raise ValueError(f"cannot read Atlas source metadata: {error}") from error

    metadata = {}
    for line in toc.splitlines():
        if not line.startswith("##") or ":" not in line:
            continue
        key, value = line[2:].split(":", 1)
        metadata[key.strip()] = value.strip()
    if metadata.get("Interface") != ATLAS_INTERFACE or metadata.get("Version") != ATLAS_VERSION:
        raise ValueError("Atlas source does not match Interface 30300 version 1.17.0")
    if metadata.get("Author") != "Dan Gilbert" or metadata.get("X-Date") != "August, 2010":
        raise ValueError("Atlas source author or release date does not match the pinned source")
    if ATLAS_COPYRIGHT not in source or "either version 2" not in source or "any later version" not in source:
        raise ValueError("Atlas source copyright or GPL-2.0-or-later notice is missing")
    if "GNU GENERAL PUBLIC LICENSE" not in license_text or "Version 2, June 1991" not in license_text:
        raise ValueError("Atlas GPL version 2 license text is missing or malformed")


def validate_blp(path: pathlib.Path, expected_size: tuple[int, int]) -> bytes:
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise ValueError(f"missing map asset {path}: {error}") from error
    if len(payload) < 20 or payload[:4] != b"BLP2":
        raise ValueError(f"map asset is not BLP2: {path}")
    width = int.from_bytes(payload[12:16], "little")
    height = int.from_bytes(payload[16:20], "little")
    if (width, height) != expected_size:
        raise ValueError(
            f"map asset {path} is {width}x{height}, expected {expected_size[0]}x{expected_size[1]}"
        )
    return payload


def copy_asset(
    source: pathlib.Path,
    destination: pathlib.Path,
    expected_size: tuple[int, int],
) -> dict:
    payload = validate_blp(source, expected_size)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != payload:
        shutil.copyfile(source, destination)
    return {
        "name": destination.name,
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def lua_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def build_mop_maps(root: pathlib.Path, output: pathlib.Path) -> tuple[dict[int, dict], list[dict]]:
    export_manifest = read_json(root / "export-manifest.json")
    if not isinstance(export_manifest, dict):
        raise ValueError("MoP export manifest is not an object")
    if export_manifest.get("product") != "wow_classic" or export_manifest.get("build") != MOP_BUILD:
        raise ValueError("MoP export build does not match the pinned source")
    if export_manifest.get("buildKey") != MOP_BUILD_KEY:
        raise ValueError("MoP export build key does not match the pinned source")
    if export_manifest.get("locale") != "zhCN" or export_manifest.get("failures"):
        raise ValueError("MoP export is incomplete or uses the wrong locale")

    source_by_file_data_id = {
        int(entry["fileDataID"]): root / entry["fileName"]
        for entry in export_manifest["files"]
    }
    ui_maps = rows_by_id(root, "UiMap")

    map_ids_by_ui_map: dict[int, set[int]] = {}
    for row in rows(root, "UiMapAssignment"):
        map_ids_by_ui_map.setdefault(int(row["UiMapID"]), set()).add(int(row["MapID"]))

    art_ids_by_ui_map: dict[int, list[int]] = {}
    for row in rows(root, "UiMapXMapArt"):
        art_ids_by_ui_map.setdefault(int(row["UiMapID"]), []).append(int(row["UiMapArtID"]))

    tiles_by_art: dict[int, list[dict]] = {}
    for row in rows(root, "UiMapArtTile"):
        tiles_by_art.setdefault(int(row["UiMapArtID"]), []).append(row)

    result: dict[int, dict] = {}
    assets: list[dict] = []
    for map_id, target_name in sorted(TBC_MAPS.items()):
        matching_ui_maps = sorted(
            ui_map_id for ui_map_id, assigned in map_ids_by_ui_map.items() if map_id in assigned
        )
        if not matching_ui_maps:
            raise ValueError(f"MoP UiMapAssignment has no rows for target map {map_id}")
        floors = []
        for floor_number, ui_map_id in enumerate(matching_ui_maps, 1):
            ui_map = ui_maps.get(ui_map_id)
            if ui_map is None:
                raise ValueError(f"UiMapAssignment references missing UiMap {ui_map_id}")
            art_ids = art_ids_by_ui_map.get(ui_map_id, [])
            if len(art_ids) != 1:
                raise ValueError(f"UiMap {ui_map_id} has {len(art_ids)} map-art rows")
            art_tiles = tiles_by_art.get(art_ids[0], [])
            base_tiles = [tile for tile in art_tiles if int(tile["LayerIndex"]) == 0]
            coordinate_map = {
                (int(tile["RowIndex"]), int(tile["ColIndex"])): tile
                for tile in base_tiles
            }
            expected_coordinates = {(row, column) for row in range(3) for column in range(4)}
            if len(base_tiles) != 12 or set(coordinate_map) != expected_coordinates:
                raise ValueError(f"UiMap {ui_map_id} does not contain one complete 4x3 base layer")
            tile_paths = []
            for row in range(3):
                for column in range(4):
                    tile = coordinate_map[(row, column)]
                    file_data_id = int(tile["FileDataID"])
                    source = source_by_file_data_id.get(file_data_id)
                    if source is None:
                        raise ValueError(f"MoP export is missing map tile {file_data_id}")
                    name = f"mop-{file_data_id}.blp"
                    asset = copy_asset(source, output / "MoP" / name, (256, 256))
                    assets.append(
                        {
                            **asset,
                            "source": "mop",
                            "fileDataID": file_data_id,
                            "logicalPath": str(source.relative_to(root)).replace("\\", "/"),
                        }
                    )
                    tile_paths.append(f"Interface\\EncounterJournal\\OpenWoW\\Maps\\MoP\\mop-{file_data_id}")
            floors.append(
                {
                    "name": f"第 {floor_number} 层" if len(matching_ui_maps) > 1 else "地图",
                    "uiMapID": ui_map_id,
                    "tiles": tile_paths,
                }
            )
        result[map_id] = {
            "name": target_name,
            "source": "mop",
            "sourceLabel": f"MoP Classic {MOP_BUILD}",
            "floors": floors,
        }
    return result, assets


def build_atlas_maps(root: pathlib.Path, output: pathlib.Path) -> tuple[dict[int, dict], list[dict]]:
    validate_atlas_source(root)
    maps_root = root / "Atlas" / "Images" / "Maps"
    result: dict[int, dict] = {}
    assets: list[dict] = []
    for map_id, (name, source_floors) in sorted(ATLAS_MAPS.items()):
        floors = []
        for floor_name, source_name in source_floors:
            source = maps_root / f"{source_name}.blp"
            output_name = f"atlas-{source_name}.blp"
            asset = copy_asset(source, output / "Atlas" / output_name, (512, 512))
            assets.append(
                {
                    **asset,
                    "source": "atlas",
                    "logicalPath": f"Atlas/Images/Maps/{source_name}.blp",
                }
            )
            floors.append(
                {
                    "name": floor_name,
                    "texture": f"Interface\\EncounterJournal\\OpenWoW\\Maps\\Atlas\\atlas-{source_name}",
                }
            )
        result[map_id] = {
            "name": name,
            "source": "atlas",
            "sourceLabel": f"Atlas {ATLAS_VERSION}",
            "floors": floors,
        }
    return result, assets


def write_lua(path: pathlib.Path, maps: dict[int, dict]) -> None:
    lines = [
        "-- Generated by tools/encounter_journal/generate_maps.py.",
        "-- Map presentation only; instance availability remains gated by build-12340 DBC records.",
        "OpenWoW_EncounterJournal_Maps = {",
        "\tschema = 1,",
        "\tlocale = \"zhCN\",",
        "\ttargetBuild = 12340,",
        "\tmaps = {",
    ]
    for map_id, entry in sorted(maps.items()):
        lines.extend(
            [
                f"\t\t[{map_id}] = {{",
                f"\t\t\tname = {lua_string(entry['name'])},",
                f"\t\t\tsource = {lua_string(entry['source'])},",
                f"\t\t\tsourceLabel = {lua_string(entry['sourceLabel'])},",
                "\t\t\tfloors = {",
            ]
        )
        for floor in entry["floors"]:
            lines.append("\t\t\t\t{")
            lines.append(f"\t\t\t\t\tname = {lua_string(floor['name'])},")
            if "uiMapID" in floor:
                lines.append(f"\t\t\t\t\tuiMapID = {floor['uiMapID']},")
            if "texture" in floor:
                lines.append(f"\t\t\t\t\ttexture = {lua_string(floor['texture'])},")
            else:
                lines.append("\t\t\t\t\ttiles = {")
                for texture in floor["tiles"]:
                    lines.append(f"\t\t\t\t\t\t{lua_string(texture)},")
                lines.append("\t\t\t\t\t},")
            lines.append("\t\t\t\t},")
        lines.extend(["\t\t\t},", "\t\t},"])
    lines.extend(["\t},", "}", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def remove_stale_assets(output: pathlib.Path, expected: set[pathlib.Path]) -> None:
    if not output.exists():
        return
    for path in output.rglob("*.blp"):
        if path not in expected:
            path.unlink()


def main() -> None:
    args = parse_args()
    mop_maps, mop_assets = build_mop_maps(args.mop_export, args.asset_output)
    atlas_maps, atlas_assets = build_atlas_maps(args.atlas_addon, args.asset_output)
    overlap = set(mop_maps).intersection(atlas_maps)
    if overlap:
        raise ValueError(f"duplicate target map records: {sorted(overlap)}")
    maps = {**atlas_maps, **mop_maps}
    assets = sorted(mop_assets + atlas_assets, key=lambda entry: (entry["source"], entry["name"]))

    expected = {
        args.asset_output / ("MoP" if asset["source"] == "mop" else "Atlas") / asset["name"]
        for asset in assets
    }
    remove_stale_assets(args.asset_output, expected)
    write_lua(args.lua_output, maps)

    source_manifest = read_json(args.mop_export / "export-manifest.json")
    manifest = {
        "schema": 1,
        "targetDataAudit": TARGET_DATA_AUDIT,
        "sources": {
            "mop": {
                "product": source_manifest["product"],
                "build": source_manifest["build"],
                "buildKey": source_manifest["buildKey"],
                "locale": source_manifest["locale"],
            },
            "atlas": {
                "name": "Atlas",
                "version": ATLAS_VERSION,
                "interface": int(ATLAS_INTERFACE),
                "date": "August, 2010",
                "copyright": ATLAS_COPYRIGHT,
                "license": "GPL-2.0-or-later",
            },
        },
        "maps": [
            {
                "mapID": map_id,
                "name": entry["name"],
                "source": entry["source"],
                "floors": len(entry["floors"]),
            }
            for map_id, entry in sorted(maps.items())
        ],
        "assets": assets,
    }
    args.manifest_output.parent.mkdir(parents=True, exist_ok=True)
    args.manifest_output.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
