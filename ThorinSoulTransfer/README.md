# Thorin Soul Transfer v1.0.0

A script-free SKSE/CommonLibSSE NG plugin for False Dragonborn roleplay.

## What it does

When a dragon dies, the plugin watches the vanilla dragon-soul absorption sequence. If a loaded actor whose displayed name contains **Thorin** or **Throin** is within 6000 Skyrim units of that dragon when the soul-stream effect starts:

- The exact vanilla/modded `dragon -> player` soul-stream visual is stopped on the player.
- The same currently active Art/Shader is recreated as `dragon -> Thorin`.
- The matching `player -> dragon` absorption visual is stopped on the player and recreated as `Thorin -> dragon`.
- Vanilla `DragonPowerAbsorbFXS` (`Skyrim.esm` FormID `000280C0`) is stopped on the player and played on Thorin.

## What it deliberately does NOT change

- Player `DragonSouls` actor value
- `MQKillDragonScript`
- Dragon Rising / MQ quest stages
- Quest aliases
- Dragon actor records
- Thorin actor records
- AI packages
- Dragon death scripts
- Papyrus scripts of any kind

The real player therefore still receives exactly the soul that vanilla would have awarded, and quest dragons continue to advance their quests normally. The plugin only redirects the visible absorption effects.

Miraak/DLC2 soul-steal cases are naturally preserved: if vanilla sends the soul effect to Miraak instead of the player, the plugin sees no player<->dragon effect pair and does nothing.

## Requirements

- Skyrim SE/AE
- SKSE64
- Address Library / requirements of the CommonLibSSE NG build used by your setup

## Installation

Install the generated MO2 ZIP normally. It contains only:

`SKSE/Plugins/ThorinSoulTransfer.dll`

No ESP. No PEX. No MCM.

## Notes

The plugin identifies Thorin by the loaded actor's displayed name. Both spellings `Thorin` and `Throin` are accepted. It chooses the nearest matching actor within 6000 units.

The dragon's own death/burning visuals, sounds, and the vanilla camera image-space effect are intentionally left alone. Those effects do not identify the player as the soul recipient; only the actor-targeted absorption visuals are redirected.
