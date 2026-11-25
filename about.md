# DGDbot

Record a run in **practice mode** and play it back in **normal mode**.

## How to use

Open the pause menu and press the **Replay** button on the left. The replay menu has:

- **Record** – arms recording. Remove your checkpoints, then restart the level. Play in practice mode as usual. When you die, the inputs after your last checkpoint are removed, so the final replay contains only the clean run.
- **Play** – (normal mode only) arms playback. Restart the level and the recorded inputs are played back automatically. Your real clicks are ignored while a replay is running.
- **Export** – writes a `.gdr` file to the mod's save folder (`replays/`) and shows you the full path.

Finishing the level while recording also auto-saves the replay.

Tip: release your jump button before you die / respawn, so held-button state stays consistent.

- **Import** – loads the newest `.gdr` from the `replays/import/` folder (JSON-style GDR only, 240 TPS, must match the level).

## Safe mode
Whenever a replay is playing, the attempt is automatically a test run: nothing is saved (no progress, stars or records). This can't be turned off.
