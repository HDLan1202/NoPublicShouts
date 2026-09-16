# NoPublicShouts

A tiny SKSE plugin for **False Dragonborn roleplay**.

When the player has a genuine Dragon Shout equipped and is inside a location tagged as a city, town, or settlement, the shout input is blocked before vanilla shout execution and the HUD shows:

> I can't shout in front of these people.

## Scope

Blocked location keywords:
- `LocTypeCity`
- `LocTypeTown`
- `LocTypeSettlement`

Parent locations are checked as well, so interiors such as shops, inns, palaces, and homes inherit the city/town/settlement restriction.

The mod intentionally does **not** use `LocTypeHabitation`, avoiding overly broad blocking of isolated farms and inns.

## Design

- SKSE/CommonLibSSE-NG plugin
- No ESP/ESL
- No Papyrus
- No quest edits
- No save serialization
- Only genuine `TESShout` use is blocked; powers sharing the same input are left alone

## Build

GitHub Actions builds the plugin on Windows and publishes an MO2-ready artifact:

`NoPublicShouts_FalseDragonborn_MO2.zip`

Its structure is:

```text
SKSE/
  Plugins/
    NoPublicShouts.dll
```
