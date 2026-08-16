# OpenWoW

<p align="center">
  <a href="https://github.com/sponsors/rkabachenko">
    <img alt="Sponsor OpenWoW"
         src="https://img.shields.io/badge/%E2%9D%A4%20Sponsor%20this%20project-db61a2?style=for-the-badge&logo=githubsponsors&logoColor=white">
  </a>
</p>

<p align="center">
  <em>OpenWoW is developed by one person. Sponsorship is what pays for the time
  that goes into it — if it is useful to you, please consider supporting it.</em>
</p>

[![CI](https://github.com/rkabachenko/OpenWow-snapshot/actions/workflows/ci.yml/badge.svg)](https://github.com/rkabachenko/OpenWow-snapshot/actions/workflows/ci.yml)
[![Licence: AGPL v3](https://img.shields.io/badge/licence-AGPL--3.0-blue.svg)](LICENSE)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-informational)

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.24%2B-064F8C?logo=cmake&logoColor=white)
![vcpkg](https://img.shields.io/badge/deps-vcpkg-0078D4?logo=microsoft&logoColor=white)
![SDL2](https://img.shields.io/badge/SDL2-platform-1D4F8C)
![bgfx](https://img.shields.io/badge/bgfx-renderer-6E4C13)
![Lua 5.1](https://img.shields.io/badge/Lua-5.1-2C2D72?logo=lua&logoColor=white)
![FFmpeg](https://img.shields.io/badge/FFmpeg-video-007808?logo=ffmpeg&logoColor=white)
[![Sponsor](https://img.shields.io/badge/sponsor-db61a2?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/rkabachenko)

A from-scratch reimplementation of the World of Warcraft 3.3.5a (build 12340)
game **client**, written in modern C++.

This is not a mod, a patch, or a launcher — it is a new client binary. No game
code or game assets are included; you supply those yourself from your own copy
of the game.

It is built as a **drop-in replacement**: a single executable (or `.app` bundle)
you place inside an existing 3.3.5a game folder. It reads `Data/`, `Interface/`
and `WTF/` from that folder and ships no game content of its own. The goal is
full compatibility — everything written for the original client should work
here, unchanged.

## What works

You can log in, pick a realm and character, enter the world, and play: movement,
combat, chat, inventory, quests, addons and much of the interface all work
against servers of the era, tested against AzerothCore.

- **World rendering** — terrain, map objects, animated models with skeletal
  animation and particle systems, water, lighting and weather.
- **Interface** — a Lua 5.1 runtime with the widget API, frame system and event
  dispatch that stock FrameXML and GlueXML are written against, so the shipped
  interface code runs unmodified — as do addons written for the 3.3.5a API,
  with their saved variables, bindings and secure-execution rules.
- **Networking** — the original wire protocol: world, movement, spell,
  inventory, quest and chat opcodes.

It is very much experimental, so expect gaps and rough edges.

The guiding rule of the project is that behaviour may not diverge from the
original client. Where the two differ, this client is considered wrong.

## Platforms and architectures

Portability is a design goal rather than an afterthought, which is why the
renderer sits on [bgfx](https://github.com/bkaradzic/bgfx) instead of being
written against one graphics API. bgfx abstracts the backend, so the same
rendering code targets Direct3D, Metal, Vulkan and OpenGL, and adding a
platform is mostly a build-and-test exercise.

| | |
| --- | --- |
| Operating systems | Linux, macOS, Windows |
| Architectures | x86-64, ARM64 (including Apple Silicon), RISC-V 64 |
| Graphics backends | Metal, Vulkan, Direct3D, OpenGL — selected per platform by bgfx |

## Requirements

You need your own legitimate copy of the original game data (the `Data/`
directory containing the MPQ archives). **None of it ships here**, and the
client will not run without it.

Building requires a C++20 toolchain, CMake 3.24+, and vcpkg for dependencies.

## Building

See **[docs/BUILDING.md](docs/BUILDING.md)** for the full instructions,
toolchain versions and platform notes, including the LTO and PGO presets.

Short version:

```sh
cmake --preset release
cmake --build build/release --target openwow-client
```

## Support and community

- **Bug reports and feature requests** —
  [open an issue](https://github.com/rkabachenko/OpenWow-snapshot/issues)
- **Questions, ideas and general discussion** —
  [GitHub Discussions](https://github.com/rkabachenko/OpenWow-snapshot/discussions)
- **Security issues** — please report privately via
  [GitHub security advisories](https://github.com/rkabachenko/OpenWow-snapshot/security/advisories/new)
  rather than in a public issue.

When reporting a rendering or behaviour bug, a screenshot or short clip
alongside the description is worth a great deal, since the project is measured
against how the original client behaves.

## About this repository

This is a **published snapshot**, not the working repository. It is a complete,
buildable tree, but it arrives as a single commit with no history, and it is
regenerated wholesale rather than committed to incrementally.

That is deliberate. The client is reimplemented by studying the behaviour of the
original, and the private working tree carries that research throughout: commit
messages and in-code comments cite disassembly of the original binary by symbol
and address, because every non-obvious behavioural decision has to be
justifiable and re-derivable. That material is
research only, not something to redistribute — so the snapshot is produced
by stripping unnecessary comments/docs and republishing only the buildable source.

The practical consequence: pull
requests are merged by applying them upstream rather than by merging the branch.
Issues, discussions and patches are all still very welcome.

## Contributing

Contributions are welcome. Please read
[CONTRIBUTING.md](CONTRIBUTING.md) first — it covers the ground rules and the
contributor licence terms, which you need to agree to before a pull request can
be merged.

## Support the project

I have spent several months of sustained work getting this to where it is —
the rendering, the protocol, the Lua and interface layers, and the long tail of
matching the original client's behaviour precisely enough that code written for
it just runs. Sponsorship is what makes it possible for me to keep putting that
kind of time in.

If OpenWoW is useful to you and you would like to help it keep going:

- [Become a sponsor on GitHub](https://github.com/sponsors/rkabachenko)
- Star the repository — it genuinely helps people find it
- Report bugs with enough detail to reproduce them, or send a pull request

Contributions of time are worth as much as anything else: a precise bug report
against the original client's behaviour is often the hardest part of a fix.

## Licence

Released under the **GNU Affero General Public License, version 3**. See
[LICENSE](LICENSE) for the full text.

In short: you may use, study, modify and redistribute this software, but if you
distribute it — or run a modified version as a network service — you must make
your source available under the same licence.

The copyright holder also offers this software under separate commercial terms
for those who cannot meet the AGPL's conditions. Enquiries via the issue
tracker.

## Legal

This project is not affiliated with, endorsed by, or associated with Blizzard
Entertainment, Inc. World of Warcraft and Blizzard Entertainment are trademarks
or registered trademarks of Blizzard Entertainment, Inc. in the United States
and other countries.

No game assets, data files, or code from the original client are distributed
here. Using this software requires that you already own the game data it reads.

Third-party components under `third_party/` are distributed under their own
licences. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the full
inventory and where each notice lives.
