# DGDbot (Geode mod)

Targets **Geometry Dash 2.2091** and **Geode v6.0.0+**.

Record a run in practice mode, play it back in normal mode, export as `.gdr`.

## Build
1. Install the Geode SDK + CLI: https://docs.geode-sdk.org/getting-started/
2. Edit `mod.json`: change `id` (e.g. `yourname.practice-replay`) and `developer`.
3. In this folder run:
       geode sdk install-binaries   (once, if needed)
       geode build
   The result is a `.geode` file in `build/`. Drop it in your GD `geode/mods` folder.
   No local toolchain? Push this folder to GitHub: the included workflow builds for Windows, macOS, Android, iOS.

## Getting the .geode file
A .geode is a compiled binary, so it has to be built from this source:
- Easiest (no setup): push this folder to a GitHub repo -> Actions tab -> "Build Geode Mod" -> download the artifact. It contains the .geode.
- Locally: run `build.bat` (Windows) or `./build.sh` (macOS/Linux). Output: `build/*.geode`.
Drop the .geode into your GD `geode/mods` folder.

## Use
Pause menu -> **Replay** button (left side) -> Record / Play / Export.
- Record: practice mode only. Remove checkpoints, restart, play. Deaths are trimmed automatically.
- Play: normal mode only. Restart the level to start the replay.
- Export: writes `<Level>_<ID>.gdr` into the mod save dir `replays/` and shows the path.
