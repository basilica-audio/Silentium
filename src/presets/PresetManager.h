#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

// Basilica Audio suite-wide M2 preset system
// (.scaffold/specs/preset-system-m2.md, binding across all 13 plugins).
//
// This file (and PresetManager.cpp) is written to have NO Nave-specific
// coupling beyond the small PresetManagerConfig/FactoryPresetAsset surface
// below - a sibling plugin can copy src/presets/PresetManager.{h,cpp} and
// src/presets/PresetBar.{h,cpp} verbatim (see docs/preset-system-notes.md
// for the exact replication recipe: what to copy unmodified, what small
// per-plugin glue to write, and the CMake/BinaryData wiring it needs).
namespace basilica::presets
{
    // A single embedded factory preset asset: the raw JSON bytes (+ size) of
    // one presets/factory/*.json file, as produced by JUCE's
    // juce_add_binary_data. PresetManager itself has zero BinaryData.h
    // dependency - the owning plugin builds this list from its own
    // BinaryData:: symbols (see PluginProcessor.cpp) and passes it into the
    // constructor, which is what keeps this header copy-paste-portable.
    struct FactoryPresetAsset
    {
        const char* data = nullptr;
        int dataSize = 0;
    };

    // The small, per-plugin configuration surface PresetManager needs.
    // Everything else (file format, dirty tracking, prev/next ordering,
    // import/export, default resolution) is fully generic.
    struct PresetManagerConfig
    {
        // Must match the "plugin" field factory/user preset JSON files
        // carry (see the format table in PresetManager.cpp) - e.g.
        // "com.yvesvogl.nave". A preset file whose "plugin" field doesn't
        // match this is refused on import (see importPresetFile()).
        juce::String pluginId;

        // Used to build the user-presets folder path (see
        // getUserPresetsDirectory()): .../<manufacturerName>/<pluginName>/
        // on macOS, .../<manufacturerName>/<pluginName>/Presets/ on
        // Windows.
        juce::String pluginName;

        // The suite's shared manufacturer folder name: "Basilica Audio" for
        // every plugin in the suite.
        juce::String manufacturerName;

        // The manufacturer folder this suite used before it adopted its
        // trading name - "Yves Vogl". Non-empty turns on a one-time,
        // copy-only adoption of any presets still sitting there, run once
        // from PresetManager's constructor; empty disables the migration
        // entirely. See basilica-audio/.github docs/adr/0001 and
        // docs/branding.md.
        juce::String legacyManufacturerName;

        // Stamped into newly saved/exported presets' "pluginVersion" field.
        // Purely informational - never checked on import (only "plugin"
        // and "format" are).
        juce::String pluginVersion;

        // Optional override for getUserPresetsDirectory(): if this is a
        // non-null juce::File, it is returned verbatim instead of computing
        // the platform-standard location. Exists purely for test isolation
        // (see tests/PresetManagerTests.cpp) - a real plugin build always
        // leaves this default-constructed (empty), so production instances
        // always use the real per-user preset location.
        juce::File userPresetsDirectoryOverrideForTests;

        // Test-only counterpart to the above for the legacy location. Note the
        // deliberate asymmetry documented on getLegacyUserPresetsDirectory():
        // overriding the current directory without also overriding this one
        // *disables* the migration rather than letting a test read (and copy
        // from) the real presets of whoever is running it.
        juce::File legacyUserPresetsDirectoryOverrideForTests;
    };

    // Owns preset discovery (factory presets, embedded via BinaryData at
    // construction time; user presets, scanned from disk on demand),
    // load/save/rename/delete, default resolution, import/export (single
    // files and zip banks), and a dirty-state flag - the model half of the
    // PresetBar UI (PresetBar.h).
    //
    // AudioProcessorValueTreeState is the single source of truth for
    // parameter values; every operation here reads/writes it only through
    // its public API (setValueNotifyingHost/getParameter/etc.) - this class
    // owns no parallel copy of parameter state.
    //
    // Real-time safety: every *public* method on this class is message-
    // thread-only (JSON parsing, juce::String/juce::var allocation, file
    // I/O - none of it is safe to call from processBlock, and nothing in
    // NaveAudioProcessor::processBlock/CabConvolutionEngine ever calls into
    // this class - see docs/preset-system-notes.md's real-time-safety
    // section). The one exception is the private
    // AudioProcessorValueTreeState::Listener::parameterChanged() override
    // used for dirty-flag tracking: JUCE does not document that callback as
    // guaranteed message-thread-only (host automation could in principle
    // deliver it from another thread), so it is implemented as a single
    // lock-free std::atomic<bool> store and nothing else - real-time safe
    // regardless of which thread invokes it.
    class PresetManager : private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        PresetManager (juce::AudioProcessorValueTreeState& stateToControl,
                        PresetManagerConfig configToUse,
                        std::vector<FactoryPresetAsset> factoryAssets);
        ~PresetManager() override;

        // Applies the default-resolution order (user "Default" preset >
        // factory "Default" preset > the ParameterLayout defaults the APVTS
        // was already constructed with, i.e. do nothing further). Intended
        // to be called exactly once, from the owning AudioProcessor's
        // constructor, after the APVTS itself has been fully constructed.
        void applyStartupDefault();

        //======================================================================
        struct PresetEntry
        {
            juce::String name;
            juce::String category;
            bool isFactory = false;
        };

        // Factory presets first (in the order their BinaryData assets were
        // passed to the constructor... sorted alphabetically), then user
        // presets, also alphabetically - matches nextPreset()/
        // previousPreset()'s traversal order.
        std::vector<PresetEntry> getAllPresets() const;

        juce::String getCurrentPresetName() const noexcept { return currentPresetName; }
        bool isCurrentPresetFactory() const noexcept { return currentPresetIsFactory; }

        // True as soon as any parameter changes after a preset finishes
        // loading (or after construction, before any preset has ever been
        // explicitly loaded); false immediately after a successful
        // load/save/import. Safe to call from the message thread at any
        // time (lock-free atomic read).
        bool isDirty() const noexcept { return dirty.load (std::memory_order_relaxed); }

        //======================================================================
        // Loading = every APVTS parameter is first reset to its
        // ParameterLayout default, then any values the preset's JSON does
        // carry are applied on top via setValueNotifyingHost() - so a
        // preset load is always a complete, deterministic snapshot
        // regardless of what was dialled in before loading it. This is also
        // what makes "missing IDs keep their default" true for forward-
        // compat imports (see importPresetFile()). Message-thread-only.
        // Returns false (state left untouched) if no preset with that exact
        // name exists (user presets are checked first, then factory - see
        // the class-level docs).
        bool loadPreset (const juce::String& name);

        void nextPreset();
        void previousPreset();

        //======================================================================
        // Writes the current APVTS parameter values as a new user preset
        // file (see getUserPresetsDirectory()), creating the directory on
        // demand. Refuses (returns false, nothing written) to shadow an
        // existing factory preset name - use a different name, or
        // setCurrentAsDefault() for the one deliberate "Default" exception.
        bool saveUserPreset (const juce::String& name, const juce::String& category);

        // Overwrites the currently loaded preset's own file, preserving its
        // stored category. No-op (returns false) if the current preset is a
        // factory preset (read-only in the UI) or if nothing is currently
        // loaded - use saveUserPreset() with a new name ("Save As...")
        // instead.
        bool saveCurrentUserPreset();

        bool renameUserPreset (const juce::String& oldName, const juce::String& newName);
        bool deleteUserPreset (const juce::String& name);

        // "Set current as default" / "Reset default" from the spec: writes/
        // deletes a user preset file literally named "Default", which
        // loadPreset("Default")/applyStartupDefault() will always find
        // before falling back to a factory "Default" preset. Deliberately
        // bypasses saveUserPreset()'s "can't shadow a factory name" guard
        // (shadowing the factory Default is the entire point) and does not
        // touch getCurrentPresetName()/isDirty() - this records what loads
        // next time, it doesn't change what's loaded right now.
        bool setCurrentAsDefault();
        bool resetDefault();

        //======================================================================
        // Forward/backward-tolerant import: unknown parameter IDs in the
        // file are silently ignored; missing IDs keep their (just-reset-to-)
        // default value (see loadPreset()'s docs above); a `plugin`
        // mismatch or `format` mismatch refuses the import (state left
        // untouched) and reports a human-readable, already-TRANS()'d reason
        // via `errorMessage`, safe to show directly in a dialog.
        bool importPresetFile (const juce::File& file, juce::String& errorMessage);

        // Pretty-printed JSON. Returns false if `name` isn't a known preset
        // or `destination` couldn't be written.
        bool exportPreset (const juce::String& name, const juce::File& destination) const;

        // Bank export: writes the named user presets (or, if
        // `userPresetNamesOnly` is empty, every user preset currently on
        // disk) into a single zip at `destination`. Factory presets are
        // intentionally never included (they already ship with every
        // installation). Returns false if there was nothing to export or
        // the zip couldn't be written.
        bool exportBank (const juce::File& destination, const std::vector<juce::String>& userPresetNamesOnly = {}) const;

        // Bank import: extracts every entry from the zip and imports each
        // individually through the same tolerance/validation rules as
        // importPresetFile() (entries belonging to a different plugin, or
        // that fail to parse, are silently skipped rather than aborting the
        // whole bank). Persists successfully-validated entries directly to
        // the user presets directory - unlike loadPreset()/
        // importPresetFile(), a bank import populates the user library
        // without changing what's currently loaded or the dirty flag.
        // Returns the number of presets successfully imported.
        int importBank (const juce::File& zipFile);

        static juce::File getUserPresetsDirectory (const PresetManagerConfig& presetManagerConfig);

        // The pre-rename location the constructor adopts presets from, or a
        // default-constructed juce::File when there is nothing to adopt: no
        // legacyManufacturerName, both names equal, or a test that overrode
        // the current directory without naming a legacy one (see the
        // implementation for why that last case must not fall through to the
        // real per-user folder).
        static juce::File getLegacyUserPresetsDirectory (const PresetManagerConfig& presetManagerConfig);

        static constexpr const char* presetFileExtension = ".basilicapreset";
        static constexpr const char* presetFormatTag = "basilica-preset-1";

    private:
        void parameterChanged (const juce::String&, float) override;

        void resetAllParametersToDefault();
        void applyPlainValues (const juce::var& parametersObject);
        void applyParsedPreset (const juce::var& parsed, const juce::String& name, bool isFactory);
        juce::var buildPresetVar (const juce::String& name, const juce::String& category) const;
        bool writePresetVarToFile (const juce::var& presetVar, const juce::File& destination) const;
        juce::File userPresetFileFor (const juce::String& name) const;

        // The platform-standard per-user preset folder for one manufacturer
        // name. Shared by getUserPresetsDirectory() and
        // getLegacyUserPresetsDirectory() so the two can only ever differ in
        // the manufacturer component, never in the path shape around it.
        static juce::File userPresetsDirectoryFor (const juce::String& manufacturer,
                                                    const juce::String& plugin);

        // Copies every preset file in getLegacyUserPresetsDirectory() into
        // getUserPresetsDirectory(), skipping any name already present there,
        // and returns how many files were copied. Copies rather than moves, so
        // an older build of this plugin still finds its presets where it left
        // them and a downgrade stays survivable. Idempotent; costs a single
        // isDirectory() call on a machine that never had the legacy folder.
        // Message-thread-only, called once from the constructor.
        int migrateLegacyUserPresets();

        // Returns a void var if `jsonText` doesn't parse as a well-formed,
        // format/plugin-matching basilica-preset-1 object; errorMessage is
        // set to a TRANS()'d, dialog-safe reason whenever that happens.
        juce::var parseAndValidate (const juce::String& jsonText, juce::String& errorMessage) const;

        struct FactoryPresetRecord
        {
            juce::String name;
            juce::String category;
            juce::var parsed;
        };

        juce::AudioProcessorValueTreeState& apvts;
        PresetManagerConfig config;
        std::vector<FactoryPresetRecord> factoryPresets;

        // Message-thread-only state - see the class-level real-time-safety
        // note for why `dirty` alone is the exception (an atomic, safely
        // writable from parameterChanged() regardless of its calling
        // thread).
        juce::String currentPresetName;
        bool currentPresetIsFactory = false;
        std::atomic<bool> dirty { false };
        std::atomic<bool> applyingPreset { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
    };
}
