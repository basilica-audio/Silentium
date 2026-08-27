# Branding: which name appears where

Silentium is part of the **Basilica Audio** plugin suite. Two names are in play, and they are not
interchangeable: *Basilica Audio* is the trading name the suite is sold under, *Yves Vogl* is the
legal person who holds the copyright. This file records which one each identifier carries, and why
some of them deliberately still say the old thing.

The suite-level decision is
[ADR 0001](https://github.com/basilica-audio/.github/blob/main/docs/adr/0001-suite-vendor-identity.md)
in `basilica-audio/.github`, answering
[basilica-audio/.github#2](https://github.com/basilica-audio/.github/issues/2).

## What a user sees

| Surface | Value | Set in |
| --- | --- | --- |
| DAW vendor column / plugin-manager grouping | **Basilica Audio** | `COMPANY_NAME`, `CMakeLists.txt` |
| Audio Unit `name` string | **Basilica Audio: Silentium** | derived from `COMPANY_NAME` + `PRODUCT_NAME` |
| VST3 vendor field | **Basilica Audio** | derived from `COMPANY_NAME` |
| Copyright notice (`NSHumanReadableCopyright`) | **Copyright (c) 2026 Yves Vogl** | `COMPANY_COPYRIGHT`, `CMakeLists.txt` |
| User preset folder (macOS) | `~/Library/Audio/Presets/Basilica Audio/Silentium/` | `PresetManagerConfig::manufacturerName` |
| User preset folder (Windows) | `%APPDATA%\Basilica Audio\Silentium\Presets\` | same |

## What stays on the old name, on purpose

| Identifier | Value | Why it does not move |
| --- | --- | --- |
| `BUNDLE_ID` | `com.yvesvogl.silentium` | A host recognises a plugin across sessions by its bundle ID. Changing it makes every existing project treat this as a *different* plugin and lose its instance. Nothing a user sees flows from it, so there is no benefit to weigh against that. |
| Preset JSON `plugin` field | `com.yvesvogl.silentium` | The same string. Freezing it keeps every preset file ever written importable, including ones exported by older builds and shared between users. |
| `COMPANY_COPYRIGHT` | `Copyright (c) 2026 Yves Vogl` | A trading name is not a copyright holder. Aligning this to the brand would make the notice less accurate, not more consistent. |

`PLUGIN_MANUFACTURER_CODE` stays `Yvsv` and `PLUGIN_CODE` is unchanged, which is what makes the
`COMPANY_NAME` move safe: the VST3 class ID derives from those two alone (JUCE 8.0.14,
`juce_VST3ModuleInfo.h`, `VST3Interface::jucePluginId`), and the Audio Unit identity triple is
`(aufx, <PLUGIN_CODE>, Yvsv)`. Neither changed, so a session saved with an older build still
resolves to this plugin.

**Do not "finish the rename".** The values in the second table are decisions, not leftovers. The
`CMakeLists.txt` entries carry the same warning at the definition.

## What happened to presets saved before the rename

Presets used to live under `~/Library/Audio/Presets/Yves Vogl/Silentium/` (macOS) and
`%APPDATA%\Yves Vogl\Silentium\Presets\` (Windows). Nothing there is lost:

- On the first launch after updating, `PresetManager` **copies** every `.basilicapreset` file from
  the old folder into the new one.
- It **never moves or deletes** the originals, so an older build of Silentium - or a downgrade -
  still finds its presets exactly where it left them.
- It **never overwrites** a file already present under the new name. A preset you saved after the
  update always wins over an older file of the same name.
- It is idempotent, and on a machine that never had the old folder it costs one filesystem check.

The old folder is never cleaned up automatically. Deleting a user's files to tidy up a folder name
is not a trade this project makes; remove it by hand if you want it gone.

`tests/PresetManagerTests.cpp` pins all of this: a preset written under the legacy layout still
loads, the migration does not overwrite a newer file, an overridden test directory never reaches
the real preset folder, and both folder shapes match the platform convention - the last of those
asserted on macOS and Windows CI, not just on a developer's machine.
