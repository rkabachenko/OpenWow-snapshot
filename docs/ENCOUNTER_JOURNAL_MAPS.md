# Encounter Journal map supplement

OpenWoW supplements the build-12340 Dungeon Journal with instance-map
presentation data. The supplement does not add instances or change their
availability; the existing Encounter Journal records remain authoritative.

## Target-data audit

The active `zhCN` archive winner for the relevant build-12340 tables is
`Data/zhCN/patch-zhCN-2.MPQ`. The audited tables contain no Burning Crusade
instance map IDs, and `WorldMapArea.dbc` contains none of the 50 map IDs covered
by this supplement:

| Table | Records | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `WorldMapArea.dbc` | 108 | 6,075 | `90e1ec678c8226c76f4dd9f7904f05a4cb0a58c8b4d386719b24949bd55925bb` |
| `DungeonMap.dbc` | 55 | 1,781 | `aa31db35a2266694d8318c410712a18540d86469e3d6134eb648a1d9694f21f5` |
| `DungeonMapChunk.dbc` | 622 | 12,461 | `c9fda294ce565501518aa782957a5fe45d92147a79c34a5f29493194a8d318c3` |

Consequently, adding texture files alone cannot make the stock world-map path
work: `WorldMapArea.dbc` has no area record for those instances. The journal
therefore owns a presentation-only map overlay and continues to fall back to
the stock world map when a valid build-12340 `WorldMapArea` ID exists.

## Donor sources

Burning Crusade maps come from the locally installed MoP Classic
`wow_classic` build `5.5.4.68159`, locale `zhCN`, build key
`385e98d716641a8e5011281f9c6f5bbc`. The generator joins the exported DB2 data
through:

`UiMapAssignment -> UiMapXMapArt -> UiMapArtTile`

Every included floor must resolve to exactly one complete 4 by 3 base layer of
256 by 256 BLP2 tiles. The local MoP Classic source provided complete coverage
for all 25 Burning Crusade instance map IDs, including multi-floor maps, so no
Retail fallback was needed.

Classic maps come from Atlas 1.17.0 (`Interface: 30300`, August 2010), supplied
in `addon-file-3933-67877568895d7`. It contains complete 512 by 512 BLP2 maps
for the 24 Classic-tier instance map IDs exposed by the journal. The same
build-12340-era source also covers the missing map for the Wrath-era Onyxia
reissue. Separate Atlas plates are retained as pages for Scarlet Monastery,
Blackrock Spire and Dire Maul. Because this source matches the pre-Cataclysm
layouts, minimap stitching was not needed. These maps are GPL-2.0-or-later; see
the license and notice stored beside the Atlas map assets, plus
`THIRD_PARTY_NOTICES.md`. Keeping those files in the override tree also puts
them beside the maps in installed application bundles.

## Generated outputs

- `assets/overrides/Interface/AddOns/OpenWoW_EncounterJournal/OpenWoW_EncounterJournal_Maps.lua`
  contains the runtime map-to-texture metadata.
- `assets/overrides/Interface/EncounterJournal/OpenWoW/Maps/` contains the
  renamed BLP2 assets.
- `tools/encounter_journal/map_manifest.json` records the target-data audit,
  donor builds, map coverage, source paths, sizes and SHA-256 hashes.

Regenerate from audited local exports with:

```sh
python3 tools/encounter_journal/generate_maps.py \
  --mop-export /path/to/mop-tbc-map-export \
  --atlas-addon /path/to/addon-file-3933-67877568895d7 \
  --lua-output assets/overrides/Interface/AddOns/OpenWoW_EncounterJournal/OpenWoW_EncounterJournal_Maps.lua \
  --asset-output assets/overrides/Interface/EncounterJournal/OpenWoW/Maps \
  --manifest-output tools/encounter_journal/map_manifest.json
```

The generator rejects an unexpected MoP product, build, build key or locale,
an unexpected Atlas version/interface/licence notice, an incomplete export,
missing or malformed BLP2 assets, duplicate target records, and incomplete MoP
tile grids. It also removes stale BLP files only from the dedicated generated
map directory.
