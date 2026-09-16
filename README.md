# Thorin Dragon Soul Visuals

False Dragonborn RP SKSE/CommonLibSSE-NG plugin. No Papyrus scripts and no ESP.

## Behaviour

When a dragon dies, if a loaded actor named `Thorin` or `Throin` is within the configured radius:

- the real player remains the vanilla `AbsorbActor`;
- the player still receives the dragon soul normally;
- quest stages and quest-dragon handling remain vanilla;
- the dragon-side streaming VFX is recreated facing Thorin;
- the absorber-side streaming VFX is removed from the player and recreated on Thorin;
- vanilla `DragonPowerAbsorbFXS` (`Skyrim.esm:000280C0`) is removed from the player and recreated on Thorin.

It does **not** patch `DragonActorScript.pex`, `MQKillDragonScript.pex`, AI packages, Thorin's actor record, DragonSouls, or quest stages.

If Thorin is not nearby when the dragon dies, vanilla visuals are untouched.

## Install

Install the generated MO2 ZIP. Requirements are the usual SKSE64 + Address Library requirements for CommonLibSSE-NG plugins.

Optional configuration: `Data/SKSE/Plugins/ThorinDragonSoulVisuals.ini`.

## Default settings

- `Radius=6000`
- `VisualWindowSeconds=22`
- `Names=Thorin,Throin`

## Test

1. Put Thorin beside the player.
2. Kill a normal dragon.
3. The dragon corpse should burn normally, but the streaming/man/shader absorb visuals should appear on/fly to Thorin.
4. Check the player's Dragon Souls count: it should still increase exactly as vanilla.
5. Test Mirmulnir / a quest dragon on a backup save: quest progression should remain vanilla because this DLL does not change `AbsorbActor` or any quest state.

Log: `Documents/My Games/Skyrim Special Edition/SKSE/ThorinDragonSoulVisuals.log`.
