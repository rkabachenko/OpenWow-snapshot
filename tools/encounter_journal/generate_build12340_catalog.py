#!/usr/bin/env python3
"""Generate the build 12340 encounter-to-creature/spell supplement.

The generated table contains only numeric joins. Runtime code still validates
every encounter and spell against the active build 12340 DBC stores before it
publishes the record to Lua.
"""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import re
import sys
from dataclasses import dataclass


INSTANCE_ROW = re.compile(r"^\((\d+),(\d+),(\d+),")
SPELL_ROW = re.compile(r"^\((\d+),(\d+),(\d+|NULL),(NULL|-?\d+)\)[,;]$")
SYNTHESIZED_WORLD_LOOT_REFERENCE_MIN = 1_000_000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--instance-encounters", required=True, type=pathlib.Path)
    parser.add_argument("--creature-spells", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--creature-template", required=True, type=pathlib.Path)
    parser.add_argument("--creature-model", type=pathlib.Path)
    parser.add_argument("--creature-loot", required=True, type=pathlib.Path)
    parser.add_argument("--reference-loot", required=True, type=pathlib.Path)
    parser.add_argument("--item-template", required=True, type=pathlib.Path)
    parser.add_argument("--item-locale", required=True, type=pathlib.Path)
    parser.add_argument("--loot-output", required=True, type=pathlib.Path)
    return parser.parse_args()


@dataclass(frozen=True)
class CreatureTemplate:
    difficulty_entries: tuple[int, int, int]
    display_id: int
    loot_id: int


@dataclass(frozen=True)
class LootRow:
    item_id: int
    reference_id: int
    quest_required: bool


@dataclass(frozen=True)
class ItemTemplate:
    item_id: int
    english_name: str
    display_id: int
    quality: int
    inventory_type: int
    allowable_class: int
    item_level: int
    required_level: int


def iter_sql_rows(path: pathlib.Path, expected_fields: int):
    with path.open("r", encoding="utf-8") as source:
        for line_number, raw_line in enumerate(source, 1):
            line = raw_line.rstrip("\r\n")
            if not line.startswith("("):
                continue
            if len(line) < 3 or line[-1] not in ",;" or line[-2] != ")":
                raise ValueError(f"{path}:{line_number}: unsupported SQL row ending")
            payload = line[1:-2]
            try:
                fields = next(
                    csv.reader(
                        [payload],
                        delimiter=",",
                        quotechar="'",
                        escapechar="\\",
                        strict=True,
                    )
                )
            except csv.Error as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
            if len(fields) != expected_fields:
                raise ValueError(
                    f"{path}:{line_number}: expected {expected_fields} fields, "
                    f"found {len(fields)}"
                )
            yield line_number, fields


def parse_instance_encounters(path: pathlib.Path) -> dict[int, int]:
    result: dict[int, int] = {}
    row_count = 0
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.startswith("("):
            continue
        match = INSTANCE_ROW.match(line)
        if match is None:
            raise ValueError(f"{path}:{line_number}: unsupported instance row")
        row_count += 1
        encounter_id, credit_type, credit_entry = map(int, match.groups())
        if encounter_id in result:
            raise ValueError(f"{path}:{line_number}: duplicate encounter {encounter_id}")
        if credit_type == 0 and credit_entry != 0:
            result[encounter_id] = credit_entry
    if row_count == 0:
        raise ValueError(f"{path}: no instance encounter rows found")
    return result


def parse_creature_spells(
    path: pathlib.Path, creature_ids: set[int]
) -> dict[int, list[tuple[int, int, int]]]:
    result: dict[int, list[tuple[int, int, int]]] = {}
    row_count = 0
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.startswith("("):
            continue
        match = SPELL_ROW.match(line)
        if match is None:
            raise ValueError(f"{path}:{line_number}: unsupported creature spell row")
        row_count += 1
        creature_id = int(match.group(1))
        if creature_id not in creature_ids:
            continue
        index = int(match.group(2))
        spell_text = match.group(3)
        build_text = match.group(4)
        if index > 7:
            raise ValueError(f"{path}:{line_number}: spell index {index} is out of range")
        if spell_text == "NULL" or int(spell_text) == 0:
            continue
        verified_build = 0 if build_text == "NULL" else int(build_text)
        result.setdefault(creature_id, []).append(
            (index, int(spell_text), verified_build)
        )
    if row_count == 0:
        raise ValueError(f"{path}: no creature spell rows found")
    for creature_id, spells in result.items():
        indices = [entry[0] for entry in spells]
        if len(indices) != len(set(indices)):
            raise ValueError(f"{path}: duplicate spell index for creature {creature_id}")
        spells.sort()
    return result


def render(
    encounter_creatures: dict[int, int],
    creature_spells: dict[int, list[tuple[int, int, int]]],
    creature_templates: dict[int, CreatureTemplate],
) -> str:
    rows: list[str] = []
    for encounter_id, creature_id in sorted(encounter_creatures.items()):
        spells = creature_spells.get(creature_id, [])
        creature_template = creature_templates.get(creature_id)
        display_id = creature_template.display_id if creature_template else 0
        padded_spells = [
            f"JournalSupplementSpell{{{spell_id}u, {verified_build}}}"
            for _, spell_id, verified_build in spells
        ]
        padded_spells.extend(
            "JournalSupplementSpell{}" for _ in range(8 - len(padded_spells))
        )
        rendered_spells = ", ".join(padded_spells)
        rows.append(
            "    JournalEncounterSupplement{"
            f"{encounter_id}u, {creature_id}u, {display_id}u, "
            f"std::array{{{rendered_spells}}}, {len(spells)}u" "},"
        )

    return "\n".join(
        [
            "// Generated by tools/encounter_journal/generate_build12340_catalog.py.",
            "// Numeric server mappings are supplemental; runtime publication is",
            "// gated by the active build 12340 DungeonEncounter/Spell/CreatureDisplayInfo DBC records.",
            "constexpr auto kJournalEncounterSupplements = std::array{",
            *rows,
            "};",
            "",
        ]
    )


def parse_creature_templates(path: pathlib.Path) -> dict[int, CreatureTemplate]:
    result: dict[int, CreatureTemplate] = {}
    for line_number, fields in iter_sql_rows(path, 55):
        entry = int(fields[0])
        if entry in result:
            raise ValueError(f"{path}:{line_number}: duplicate creature {entry}")
        try:
            display_id = next(
                (int(value) for value in fields[6:10] if int(value) != 0), 0
            )
        except ValueError:
            # Current AzerothCore keeps model rows in creature_template_model;
            # older exports stored four display IDs in creature_template.
            display_id = 0
        result[entry] = CreatureTemplate(
            difficulty_entries=(int(fields[1]), int(fields[2]), int(fields[3])),
            display_id=display_id,
            loot_id=int(fields[34]),
        )
    if not result:
        raise ValueError(f"{path}: no creature template rows found")
    return result


def apply_creature_models(
    path: pathlib.Path, creatures: dict[int, CreatureTemplate]
) -> None:
    for _, fields in iter_sql_rows(path, 6):
        creature_id = int(fields[0])
        model_index = int(fields[1])
        display_id = int(fields[2])
        creature = creatures.get(creature_id)
        if (
            creature is None
            or display_id == 0
            or (creature.display_id != 0 and model_index != 0)
        ):
            continue
        creatures[creature_id] = CreatureTemplate(
            difficulty_entries=creature.difficulty_entries,
            display_id=display_id,
            loot_id=creature.loot_id,
        )


def parse_loot_table(path: pathlib.Path) -> dict[int, list[LootRow]]:
    result: dict[int, list[LootRow]] = {}
    for _, fields in iter_sql_rows(path, 10):
        entry = int(fields[0])
        result.setdefault(entry, []).append(
            LootRow(
                item_id=int(fields[1]),
                reference_id=abs(int(fields[2])),
                quest_required=int(fields[4]) != 0,
            )
        )
    if not result:
        raise ValueError(f"{path}: no loot rows found")
    return result


def parse_item_templates(path: pathlib.Path) -> dict[int, ItemTemplate]:
    result: dict[int, ItemTemplate] = {}
    for line_number, fields in iter_sql_rows(path, 138):
        item_id = int(fields[0])
        if item_id in result:
            raise ValueError(f"{path}:{line_number}: duplicate item {item_id}")
        result[item_id] = ItemTemplate(
            item_id=item_id,
            english_name=fields[4],
            display_id=int(fields[5]),
            quality=int(fields[6]),
            inventory_type=int(fields[12]),
            allowable_class=int(fields[13]),
            item_level=int(fields[15]),
            required_level=int(fields[16]),
        )
    if not result:
        raise ValueError(f"{path}: no item template rows found")
    return result


def parse_zhcn_item_names(path: pathlib.Path) -> dict[int, str]:
    result: dict[int, str] = {}
    for line_number, fields in iter_sql_rows(path, 5):
        if fields[1] != "zhCN" or not fields[2]:
            continue
        item_id = int(fields[0])
        if item_id in result:
            raise ValueError(f"{path}:{line_number}: duplicate zhCN item {item_id}")
        result[item_id] = fields[2]
    if not result:
        raise ValueError(f"{path}: no zhCN item names found")
    return result


def expand_reference_items(
    reference_id: int,
    reference_loot: dict[int, list[LootRow]],
    active_path: set[int],
) -> set[int]:
    if reference_id in active_path:
        raise ValueError(f"reference_loot_template contains a cycle at {reference_id}")
    if reference_id not in reference_loot:
        raise ValueError(
            f"reference_loot_template is missing referenced entry {reference_id}"
        )
    active_path.add(reference_id)
    result: set[int] = set()
    for row in reference_loot.get(reference_id, []):
        if row.quest_required:
            continue
        if row.item_id and not row.reference_id:
            result.add(row.item_id)
        if row.reference_id:
            result.update(
                expand_reference_items(row.reference_id, reference_loot, active_path)
            )
    active_path.remove(reference_id)
    return result


def build_encounter_loot(
    encounter_creatures: dict[int, int],
    creature_templates: dict[int, CreatureTemplate],
    creature_loot: dict[int, list[LootRow]],
    reference_loot: dict[int, list[LootRow]],
    item_templates: dict[int, ItemTemplate],
) -> tuple[list[tuple[int, tuple[int, int, int, int]]], dict[int, list[ItemTemplate]]]:
    encounter_variants: list[tuple[int, tuple[int, int, int, int]]] = []
    creature_items: dict[int, list[ItemTemplate]] = {}
    for encounter_id, base_creature_id in sorted(encounter_creatures.items()):
        base_template = creature_templates.get(base_creature_id)
        if base_template is None:
            continue
        variants = [base_creature_id]
        for difficulty_variant in base_template.difficulty_entries:
            variants.append(difficulty_variant or base_creature_id)
        encounter_variants.append((encounter_id, tuple(variants)))

        for creature_id in sorted(set(variants)):
            if creature_id in creature_items:
                continue
            creature = creature_templates.get(creature_id)
            if creature is None or creature.loot_id == 0:
                creature_items[creature_id] = []
                continue

            item_ids: set[int] = set()
            for row in creature_loot.get(creature.loot_id, []):
                if row.quest_required:
                    continue
                if row.item_id and not row.reference_id:
                    item_ids.add(row.item_id)
                if (
                    row.reference_id
                    and row.reference_id < SYNTHESIZED_WORLD_LOOT_REFERENCE_MIN
                ):
                    item_ids.update(
                        expand_reference_items(
                            row.reference_id, reference_loot, set()
                        )
                    )

            items = [
                item_templates[item_id]
                for item_id in item_ids
                if item_id in item_templates and item_templates[item_id].quality >= 2
            ]
            items.sort(
                key=lambda item: (
                    -item.quality,
                    -item.item_level,
                    item.english_name.casefold(),
                    item.item_id,
                )
            )
            creature_items[creature_id] = items
    return encounter_variants, creature_items


def render_loot(
    encounter_variants: list[tuple[int, tuple[int, int, int, int]]],
    creature_items: dict[int, list[ItemTemplate]],
    zhcn_names: dict[int, str],
) -> tuple[str, int, int]:
    unique_items = {
        item.item_id: item
        for items in creature_items.values()
        for item in items
    }
    rendered = [
        "// Generated by tools/encounter_journal/generate_build12340_catalog.py.",
        "// Boss loot is supplemental; runtime publication is gated by the active",
        "// build 12340 DungeonEncounter/Item/ItemDisplayInfo DBC records.",
        "constexpr auto kJournalEncounterLootCreatures = std::array{",
    ]
    for encounter_id, variants in encounter_variants:
        rendered.append(
            "    JournalEncounterLootCreatures{"
            f"{encounter_id}u, std::array{{{variants[0]}u, {variants[1]}u, "
            f"{variants[2]}u, {variants[3]}u}}" "},"
        )
    rendered.extend(["};", "", "constexpr auto kJournalCreatureLootItems = std::array{"])
    association_count = 0
    for creature_id, items in sorted(creature_items.items()):
        for item in items:
            association_count += 1
            rendered.append(
                "    JournalCreatureLootItem{"
                f"{creature_id}u, {item.item_id}u" "},"
            )
    rendered.extend(["};", "", "constexpr auto kJournalLootItems = std::array{"])
    for item_id, item in sorted(unique_items.items()):
        name = zhcn_names.get(item.item_id, item.english_name)
        rendered.append(
            "    JournalLootItem{"
            f"{item_id}u, "
            f"{item.display_id}u, {item.quality}u, {item.inventory_type}u, "
            f"{item.allowable_class}, "
            f"{item.item_level}u, {item.required_level}u, "
            f"{json.dumps(name, ensure_ascii=False)}" "},"
        )
    rendered.extend(["};", ""])
    return "\n".join(rendered), association_count, len(unique_items)


def main() -> int:
    args = parse_args()
    try:
        encounter_creatures = parse_instance_encounters(args.instance_encounters)
        creature_spells = parse_creature_spells(
            args.creature_spells, set(encounter_creatures.values())
        )
        creature_templates = parse_creature_templates(args.creature_template)
        if args.creature_model is not None:
            apply_creature_models(args.creature_model, creature_templates)
        output = render(encounter_creatures, creature_spells, creature_templates)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
        creature_loot = parse_loot_table(args.creature_loot)
        reference_loot = parse_loot_table(args.reference_loot)
        item_templates = parse_item_templates(args.item_template)
        zhcn_names = parse_zhcn_item_names(args.item_locale)
        encounter_variants, creature_items = build_encounter_loot(
            encounter_creatures,
            creature_templates,
            creature_loot,
            reference_loot,
            item_templates,
        )
        loot_output, loot_association_count, unique_loot_item_count = render_loot(
            encounter_variants, creature_items, zhcn_names
        )
        args.loot_output.parent.mkdir(parents=True, exist_ok=True)
        args.loot_output.write_text(loot_output, encoding="utf-8")
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    print(
        f"wrote {len(encounter_creatures)} encounter mappings to {args.output}",
        file=sys.stderr,
    )
    print(
        f"wrote {loot_association_count} creature-loot associations and "
        f"{unique_loot_item_count} item records to {args.loot_output}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
