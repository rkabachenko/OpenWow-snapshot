# Third-party notices

OpenWoW itself is licensed under the GNU Affero General Public License v3 (see
[LICENSE](LICENSE)). It bundles the components below, each under its own terms.
All of them are permissive and compatible with the AGPL.

Dependencies resolved through vcpkg at build time (FFmpeg, Boost, SDL2, bgfx,
FreeType, OpenSSL, and their transitive dependencies) are **not** distributed
here; vcpkg fetches them from their own upstreams, and each carries its own
licence in the build tree it produces.

| Component | Licence | Notice |
| --- | --- | --- |
| [StormLib](https://github.com/ladislav-zezula/StormLib) | MIT | `third_party/StormLib/LICENSE` |
| [Dear ImGui](https://github.com/ocornut/imgui) (SDL2 backend only) | MIT | `third_party/imgui_backends/LICENSE` |
| [Lua 5.1](https://www.lua.org/) (modified) | MIT | `third_party/wow_lua/COPYRIGHT` |
| [stb](https://github.com/nothings/stb) | Public domain / MIT | at the end of each header |
| [dr_libs](https://github.com/mackron/dr_libs) | Public domain / MIT-0 | at the end of each header |
| [minimp3](https://github.com/lieff/minimp3) | CC0 | at the top of each header |

## Notes

**Lua is modified.** `third_party/wow_lua` is Lua 5.1 adapted for this project.
Its copyright notices are preserved in place — including the pointers in each
file's header to the full notice in `src/lua.h` — and the upstream `COPYRIGHT`
is kept verbatim.

**Only the ImGui SDL2 backend is vendored**, not Dear ImGui itself. The
upstream backend sources carry descriptive headers but no copyright notice, so
the MIT text is supplied alongside them.

**StormLib bundles further components** — libtommath, zlib, bzip2 and LZMA SDK
pieces — under their own permissive terms, noted in the headers of the files
under `third_party/StormLib/src/`.

## Trademarks

World of Warcraft and Blizzard Entertainment are trademarks or registered
trademarks of Blizzard Entertainment, Inc. This project is not affiliated with,
endorsed by, or associated with Blizzard Entertainment. No game assets, data
files, or code from the original client are distributed here.
