#!/usr/bin/env python3
"""Generate the Encounter Journal presentation supplement from local CASC exports.

The output intentionally contains presentation-only data. Runtime publication is
still joined to the active build-12340 Map and DungeonEncounter DBC records.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import unicodedata
from dataclasses import dataclass


MOP_BUILD = "5.5.4.68159"
RETAIL_BUILD = "12.0.7.68256"

# Modern revisions replaced these maps. Their instance artwork remains a faithful
# thematic match, but encounter text is still joined by target boss name at runtime.
MOP_ALIASES = {
    76: (309, 1),   # Cataclysm Zul'Gurub art -> build-12340 Zul'Gurub raid
    246: (289, 1),  # MoP Scholomance art -> build-12340 Scholomance
}

# These rows are not members of the first three modern JournalTier groups but map
# directly to build-12340 instances and are useful presentation donors.
MOP_ADDITIONAL = {
    63: (36, 1),
    64: (33, 1),
    235: (189, 1),
}

RETAIL_TARGETS = {
    77: (568, 2),
    741: (409, 1),
    742: (469, 1),
    743: (509, 1),
    744: (531, 1),
    745: (532, 2),
    746: (565, 2),
    747: (544, 2),
    748: (548, 2),
    749: (550, 2),
    750: (534, 2),
    751: (564, 2),
    752: (580, 2),
    753: (624, 3),
    754: (533, 3),
    755: (615, 3),
    756: (616, 3),
    757: (649, 3),
    758: (631, 3),
    759: (603, 3),
    760: (249, 3),
    761: (724, 3),
}

TIER_IDS = {68: 1, 70: 2, 72: 3}
TIER_NAMES = {1: "经典旧世", 2: "燃烧的远征", 3: "巫妖王之怒"}
TIER_ART = {
    1: ("mop", 605327),
    2: ("mop", 605326),
    3: ("mop", 605329),
}
SHARED_UI_ART = {
    5767279,  # Classic WowStyle1Dropdown atlas.
}
ART_FIELDS = {
    "background": "BackgroundFileDataID",
    "button": "ButtonFileDataID",
    "buttonSmall": "ButtonSmallFileDataID",
    "lore": "LoreFileDataID",
}


@dataclass(frozen=True)
class Source:
    key: str
    build: str
    root: pathlib.Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mop-export", required=True, type=pathlib.Path)
    parser.add_argument("--retail-export", required=True, type=pathlib.Path)
    parser.add_argument("--lua-output", required=True, type=pathlib.Path)
    parser.add_argument("--asset-output", required=True, type=pathlib.Path)
    parser.add_argument("--manifest-output", required=True, type=pathlib.Path)
    return parser.parse_args()


def read_rows(source: Source, name: str) -> list[dict]:
    path = source.root / f"{name}.db2.json"
    try:
        rows = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {source.key} {name}: {error}") from error
    if not isinstance(rows, list) or not rows:
        raise ValueError(f"{source.key} {name} contains no records")
    return rows


def index_unique(rows: list[dict], field: str, label: str) -> dict[int, dict]:
    result: dict[int, dict] = {}
    for row in rows:
        key = int(row[field])
        if key in result:
            raise ValueError(f"duplicate {label} {key}")
        result[key] = row
    return result


def lua_string(value: str) -> str:
    return json.dumps(value or "", ensure_ascii=False)


def normalize_name(value: str) -> str:
    normalized = unicodedata.normalize("NFKC", value or "").casefold()
    return "".join(character for character in normalized if character.isalnum())


def asset_path(source_key: str, file_data_id: int) -> str:
    if not file_data_id:
        return ""
    return f"Interface\\EncounterJournal\\OpenWoW\\{source_key}-{file_data_id}"


def source_art_file(source: Source, donor_instance_id: int, kind: str, file_data_id: int) -> pathlib.Path:
    return source.root / f"{source.key}-instance-{donor_instance_id}-{kind}-{file_data_id}.blp"


def source_icon_file(source: Source, file_data_id: int) -> pathlib.Path:
    return source.root / f"{source.key}-section-icon-{file_data_id}.blp"


def copy_asset(source_path: pathlib.Path, output_root: pathlib.Path, output_name: str) -> dict:
    try:
        payload = source_path.read_bytes()
    except OSError as error:
        raise ValueError(f"missing presentation asset {source_path}: {error}") from error
    if len(payload) < 4 or payload[:4] not in (b"BLP1", b"BLP2"):
        raise ValueError(f"presentation asset is not BLP: {source_path}")
    output_root.mkdir(parents=True, exist_ok=True)
    destination = output_root / output_name
    if not destination.exists() or destination.read_bytes() != payload:
        destination.write_bytes(payload)
    return {
        "name": output_name,
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def selected_mop_instances(source: Source) -> list[tuple[dict, int, int]]:
    instances = index_unique(read_rows(source, "JournalInstance"), "ID", "MoP JournalInstance")
    tier_relations = read_rows(source, "JournalTierXInstance")
    selected: dict[int, tuple[int, int]] = {}
    for relation in tier_relations:
        tier = TIER_IDS.get(int(relation["FieldA"]))
        if tier is None:
            continue
        donor_id = int(relation["FieldB"])
        row = instances.get(donor_id)
        if row is None:
            raise ValueError(f"MoP tier relation references missing instance {donor_id}")
        target_map = int(row["MapID"])
        if donor_id in (311, 316):
            continue
        if donor_id in MOP_ALIASES:
            target_map, tier = MOP_ALIASES[donor_id]
        selected[donor_id] = (target_map, tier)
    selected.update(MOP_ADDITIONAL)
    for donor_id, target in MOP_ALIASES.items():
        selected.setdefault(donor_id, target)
    return [(instances[donor_id], target_map, tier) for donor_id, (target_map, tier) in sorted(selected.items())]


def selected_retail_instances(source: Source) -> list[tuple[dict, int, int]]:
    instances = index_unique(read_rows(source, "JournalInstance"), "ID", "Retail JournalInstance")
    result = []
    for donor_id, (target_map, tier) in sorted(RETAIL_TARGETS.items()):
        row = instances.get(donor_id)
        if row is None:
            raise ValueError(f"Retail supplement is missing JournalInstance {donor_id}")
        if int(row["MapID"]) != target_map and donor_id != 77:
            raise ValueError(
                f"Retail JournalInstance {donor_id} map changed: expected {target_map}, got {row['MapID']}"
            )
        result.append((row, target_map, tier))
    return result


def validate_section_graph(encounter: dict, sections: list[dict]) -> None:
    by_id = index_unique(sections, "ID", f"section for encounter {encounter['ID']}")
    first = int(encounter.get("FirstSectionID", 0))
    if first and first not in by_id:
        raise ValueError(f"encounter {encounter['ID']} first section {first} is missing")
    for section in sections:
        for field in ("ParentSectionID", "FirstChildSectionID", "NextSiblingSectionID"):
            target = int(section.get(field, 0))
            if target and target not in by_id:
                raise ValueError(f"section {section['ID']} {field} references missing section {target}")


def build_instance_record(
    source: Source,
    instance: dict,
    target_map: int,
    tier: int,
    encounters_by_instance: dict[int, list[dict]],
    sections_by_encounter: dict[int, list[dict]],
    output_assets: pathlib.Path,
    asset_manifest: dict[str, dict],
) -> dict:
    donor_id = int(instance["ID"])
    art: dict[str, str] = {}
    for kind, field in ART_FIELDS.items():
        file_data_id = int(instance.get(field, 0))
        if not file_data_id:
            art[kind] = ""
            continue
        key = f"{source.key}-{file_data_id}.blp"
        source_path = source_art_file(source, donor_id, kind, file_data_id)
        asset_manifest.setdefault(key, copy_asset(source_path, output_assets, key))
        art[kind] = asset_path(source.key, file_data_id)

    encounter_records = []
    for encounter in sorted(
        encounters_by_instance.get(donor_id, []),
        key=lambda row: (int(row.get("OrderIndex", 0)), int(row["ID"])),
    ):
        sections = sections_by_encounter.get(int(encounter["ID"]), [])
        validate_section_graph(encounter, sections)
        rendered_sections = []
        for section in sorted(sections, key=lambda row: int(row["ID"])):
            icon_id = int(section.get("IconFileDataID", 0))
            icon = ""
            if icon_id:
                key = f"{source.key}-{icon_id}.blp"
                source_path = source_icon_file(source, icon_id)
                asset_manifest.setdefault(key, copy_asset(source_path, output_assets, key))
                icon = asset_path(source.key, icon_id)
            rendered_sections.append(
                {
                    "id": int(section["ID"]),
                    "title": section.get("Title_lang", ""),
                    "body": section.get("BodyText_lang", ""),
                    "parent": int(section.get("ParentSectionID", 0)),
                    "child": int(section.get("FirstChildSectionID", 0)),
                    "sibling": int(section.get("NextSiblingSectionID", 0)),
                    "type": int(section.get("Type", 0)),
                    "spell": int(section.get("SpellID", 0)),
                    "flags": int(section.get("Flags", 0)),
                    "iconFlags": int(section.get("IconFlags", 0)),
                    "difficultyMask": int(section.get("DifficultyMask", -1)),
                    "icon": icon,
                }
            )
        encounter_records.append(
            {
                "id": int(encounter["ID"]),
                "name": encounter.get("Name_lang", ""),
                "matchName": normalize_name(encounter.get("Name_lang", "")),
                "description": encounter.get("Description_lang", ""),
                "order": int(encounter.get("OrderIndex", 0)),
                "firstSection": int(encounter.get("FirstSectionID", 0)),
                "sections": rendered_sections,
            }
        )

    return {
        "map": target_map,
        "tier": tier,
        "source": source.key,
        "build": source.build,
        "donorInstance": donor_id,
        "name": instance.get("Name_lang", ""),
        "matchName": normalize_name(instance.get("Name_lang", "")),
        "description": instance.get("Description_lang", ""),
        "art": art,
        "encounters": encounter_records,
    }


def render_lua_value(value, indent: int) -> str:
    prefix = "\t" * indent
    if isinstance(value, dict):
        lines = ["{"]
        for key, item in value.items():
            if isinstance(key, int):
                rendered_key = f"[{key}]"
            elif isinstance(key, str) and key.isidentifier():
                rendered_key = key
            else:
                rendered_key = f"[{lua_string(str(key))}]"
            rendered_item = render_lua_value(item, indent + 1)
            lines.append(f"{prefix}\t{rendered_key} = {rendered_item},")
        lines.append(prefix + "}")
        return "\n".join(lines)
    elif isinstance(value, list):
        lines = ["{"]
        for item in value:
            lines.append(prefix + "\t" + render_lua_value(item, indent + 1) + ",")
        lines.append(prefix + "}")
        return "\n".join(lines)
    elif isinstance(value, str):
        return lua_string(value)
    elif isinstance(value, bool):
        return "true" if value else "false"
    elif value is None:
        return "nil"
    else:
        return str(value)


def render_lua(instances: list[dict]) -> str:
    tiers = {
        tier: {
            "name": name,
            "background": asset_path(*TIER_ART[tier]),
        }
        for tier, name in TIER_NAMES.items()
    }
    document = {
        "schema": 1,
        "locale": "zhCN",
        "targetBuild": 12340,
        "sources": {"mop": MOP_BUILD, "retail": RETAIL_BUILD},
        "tiers": tiers,
        "instances": {record["map"]: record for record in sorted(instances, key=lambda row: row["map"])},
    }
    return "\n".join(
        [
            "-- Generated by tools/encounter_journal/generate_presentation.py.",
            "-- Presentation data only; runtime joins remain gated by build-12340 DBC records.",
            "OpenWoW_EncounterJournal_Presentation = " + render_lua_value(document, 0),
            "",
        ]
    )


def main() -> int:
    args = parse_args()
    mop = Source("mop", MOP_BUILD, args.mop_export)
    retail = Source("retail", RETAIL_BUILD, args.retail_export)
    asset_manifest: dict[str, dict] = {}
    all_instances: list[dict] = []
    used_maps: set[int] = set()

    for source, selected in ((mop, selected_mop_instances(mop)), (retail, selected_retail_instances(retail))):
        encounters = read_rows(source, "JournalEncounter")
        sections = read_rows(source, "JournalEncounterSection")
        encounters_by_instance: dict[int, list[dict]] = {}
        sections_by_encounter: dict[int, list[dict]] = {}
        for row in encounters:
            encounters_by_instance.setdefault(int(row["JournalInstanceID"]), []).append(row)
        for row in sections:
            sections_by_encounter.setdefault(int(row["JournalEncounterID"]), []).append(row)
        for instance, target_map, tier in selected:
            if target_map in used_maps:
                raise ValueError(f"multiple presentation donors target map {target_map}")
            used_maps.add(target_map)
            all_instances.append(
                build_instance_record(
                    source,
                    instance,
                    target_map,
                    tier,
                    encounters_by_instance,
                    sections_by_encounter,
                    args.asset_output,
                    asset_manifest,
                )
            )

    for tier, (source_key, file_data_id) in TIER_ART.items():
        source = mop if source_key == "mop" else retail
        source_path = source.root / f"tier-art-{file_data_id}.blp"
        key = f"{source_key}-{file_data_id}.blp"
        asset_manifest.setdefault(key, copy_asset(source_path, args.asset_output, key))

    for file_data_id in SHARED_UI_ART:
        source_path = mop.root / f"ui-art-{file_data_id}.blp"
        key = f"mop-{file_data_id}.blp"
        asset_manifest.setdefault(key, copy_asset(source_path, args.asset_output, key))

    args.lua_output.parent.mkdir(parents=True, exist_ok=True)
    args.lua_output.write_text(render_lua(all_instances), encoding="utf-8")
    manifest = {
        "schema": 1,
        "target": {"build": 12340, "locale": "zhCN"},
        "sources": {
            "mop": {"product": "wow_classic", "build": MOP_BUILD, "locale": "zhCN"},
            "retail": {"product": "wow", "build": RETAIL_BUILD, "locale": "zhCN"},
        },
        "instanceRecords": len(all_instances),
        "encounterRecords": sum(len(record["encounters"]) for record in all_instances),
        "sectionRecords": sum(len(encounter["sections"]) for record in all_instances for encounter in record["encounters"]),
        "assets": [asset_manifest[key] for key in sorted(asset_manifest)],
    }
    args.manifest_output.parent.mkdir(parents=True, exist_ok=True)
    args.manifest_output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        f"generated {manifest['instanceRecords']} instances, {manifest['encounterRecords']} encounters, "
        f"{manifest['sectionRecords']} sections, and {len(asset_manifest)} assets"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
