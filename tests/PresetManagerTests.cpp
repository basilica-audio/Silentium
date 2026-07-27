#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/PresetManager.h"

#include <BinaryData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

// M2 preset system tests (.scaffold/specs/preset-system-m2.md's "Tests"
// section - each TEST_CASE below maps to one of that section's numbered
// items, called out in the test names/comments). Mirrors
// basilica-audio/nave's tests/PresetManagerTests.cpp (the M2 pilot).
namespace
{
    using basilica::presets::FactoryPresetAsset;
    using basilica::presets::PresetManager;
    using basilica::presets::PresetManagerConfig;

    // Mirrors PluginProcessor.cpp's own makeFactoryPresetAssets() - kept as
    // an independent copy here rather than exported from PluginProcessor.cpp
    // so this test file can construct its own, fully isolated PresetManager
    // instances (see makeIsolatedConfig() below) without depending on
    // production wiring internals.
    std::vector<FactoryPresetAsset> makeTestFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::surgicalMute_json, BinaryData::surgicalMute_jsonSize },
            { BinaryData::naturalDecay_json, BinaryData::naturalDecay_jsonSize },
            { BinaryData::pickAttackFocus_json, BinaryData::pickAttackFocus_jsonSize },
            { BinaryData::diKeyedWorkflow_json, BinaryData::diKeyedWorkflow_jsonSize },
            { BinaryData::ambientSustain_json, BinaryData::ambientSustain_jsonSize },
            { BinaryData::chugLock_json, BinaryData::chugLock_jsonSize },
            { BinaryData::duckUnderLead_json, BinaryData::duckUnderLead_jsonSize },
            { BinaryData::listenCheck_json, BinaryData::listenCheck_jsonSize },
            { BinaryData::expanderGlue_json, BinaryData::expanderGlue_jsonSize },
        };
    }

    // A fresh, isolated scratch directory per test case, so this file never
    // reads or writes the real ~/Library/Audio/Presets/... (or Windows
    // equivalent) location on the machine running the tests - see
    // PresetManagerConfig::userPresetsDirectoryOverrideForTests. Deleted on
    // destruction.
    struct ScopedTestDirectory
    {
        ScopedTestDirectory()
            : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("SilentiumPresetManagerTests")
                       .getChildFile (juce::String (juce::Time::getHighResolutionTicks())
                                       + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
        {
            dir.createDirectory();
        }

        ~ScopedTestDirectory()
        {
            dir.deleteRecursively();
        }

        JUCE_DECLARE_NON_COPYABLE (ScopedTestDirectory)

        juce::File dir;
    };

    PresetManagerConfig makeIsolatedConfig (const juce::File& userDir)
    {
        PresetManagerConfig config;
        config.pluginId = "com.yvesvogl.silentium";
        config.pluginName = "Silentium";
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = "0.2.0-test";
        config.userPresetsDirectoryOverrideForTests = userDir;
        return config;
    }

    void setParam (SilentiumAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (SilentiumAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}

//==============================================================================
// 1. Save -> load round-trip restores every parameter exactly.
TEST_CASE ("PresetManager: save -> load round-trip restores every parameter exactly", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::threshold, -33.0f);
    setParam (processor, ParamIDs::attack, 12.0f);
    setParam (processor, ParamIDs::hold, 88.0f);
    setParam (processor, ParamIDs::release, 250.0f);
    setParam (processor, ParamIDs::range, -30.0f);
    setParam (processor, ParamIDs::lookahead, 8.0f);
    setParam (processor, ParamIDs::scHighpass, 220.0f);
    setParam (processor, ParamIDs::scLowpass, 4000.0f);
    setParam (processor, ParamIDs::knee, 9.0f);
    setParam (processor, ParamIDs::duck, 1.0f);
    setParam (processor, ParamIDs::listen, 1.0f);

    REQUIRE (manager.saveUserPreset ("Round Trip", "Init"));

    // Perturb every parameter away from the saved values before reloading,
    // so the assertions below can't pass by accident.
    setParam (processor, ParamIDs::threshold, -5.0f);
    setParam (processor, ParamIDs::attack, 0.0f);
    setParam (processor, ParamIDs::hold, 0.0f);
    setParam (processor, ParamIDs::release, 5.0f);
    setParam (processor, ParamIDs::range, 0.0f);
    setParam (processor, ParamIDs::lookahead, 0.0f);
    setParam (processor, ParamIDs::scHighpass, 500.0f);
    setParam (processor, ParamIDs::scLowpass, 1000.0f);
    setParam (processor, ParamIDs::knee, 0.0f);
    setParam (processor, ParamIDs::duck, 0.0f);
    setParam (processor, ParamIDs::listen, 0.0f);

    REQUIRE (manager.loadPreset ("Round Trip"));

    CHECK (getParam (processor, ParamIDs::threshold) == Catch::Approx (-33.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::attack) == Catch::Approx (12.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::hold) == Catch::Approx (88.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::release) == Catch::Approx (250.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::range) == Catch::Approx (-30.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::lookahead) == Catch::Approx (8.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::scHighpass) == Catch::Approx (220.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::scLowpass) == Catch::Approx (4000.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::knee) == Catch::Approx (9.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::duck) == Catch::Approx (1.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::listen) == Catch::Approx (1.0f).margin (1.0e-3));
}

//==============================================================================
// 2. Import ignores unknown IDs, keeps defaults for missing IDs.
TEST_CASE ("PresetManager: import ignores unknown parameter IDs and keeps defaults for missing ones", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    // Move knee and range away from their defaults so it's meaningful when
    // the import below leaves them untouched (they're absent from
    // "parameters").
    setParam (processor, ParamIDs::knee, 18.0f);
    setParam (processor, ParamIDs::range, -5.0f);

    // A fixture JSON generated inline (not committed under tests/fixtures/)
    // to avoid brittle relative-path resolution across CI runners with
    // different working directories (macOS vs Windows ctest invocations) -
    // this is the forward/backward-compat scenario the spec's "fixture
    // JSONs in tests/" line calls for: an unknown ID ("futureParameter",
    // simulating a newer plugin version's preset) and two known IDs
    // (threshold/attack), deliberately omitting knee/range/duck/listen/etc.
    const juce::String fixtureJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.silentium",
        "pluginVersion": "9.9.9",
        "name": "Forward Compat Fixture",
        "category": "Init",
        "parameters": { "threshold": -22.0, "attack": 3.0, "futureParameter": 42.0 }
    })";

    const auto fixtureFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (fixtureFile.replaceWithText (fixtureJson));

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (fixtureFile, errorMessage));
    CHECK (errorMessage.isEmpty());

    // Known IDs present in the fixture were applied...
    CHECK (getParam (processor, ParamIDs::threshold) == Catch::Approx (-22.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::attack) == Catch::Approx (3.0f).margin (1.0e-3));

    // ...IDs absent from the fixture were reset to their ParameterLayout
    // defaults (loadPreset()/importPresetFile() always reset-then-apply -
    // see PresetManager.h), not left at the pre-import 18dB/-5dB values.
    auto* kneeParam = processor.apvts.getParameter (ParamIDs::knee);
    auto* rangeParam = processor.apvts.getParameter (ParamIDs::range);
    CHECK (getParam (processor, ParamIDs::knee) == Catch::Approx (kneeParam->convertFrom0to1 (kneeParam->getDefaultValue())).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::range) == Catch::Approx (rangeParam->convertFrom0to1 (rangeParam->getDefaultValue())).margin (1.0e-3));

    fixtureFile.deleteFile();
}

//==============================================================================
// 3. Import refuses wrong-plugin and wrong-format files.
TEST_CASE ("PresetManager: import refuses a preset belonging to a different plugin", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongPluginJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.overture",
        "pluginVersion": "0.2.0",
        "name": "Not Silentium's",
        "category": "Init",
        "parameters": { "threshold": -1.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongPluginJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    // State must be left untouched - threshold must NOT have picked up -1
    // (it wouldn't be out-of-range here, but the point is nothing changed).
    CHECK (getParam (processor, ParamIDs::threshold) != Catch::Approx (-1.0f));

    file.deleteFile();
}

TEST_CASE ("PresetManager: import refuses a file with an incompatible format tag", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongFormatJson = R"({
        "format": "some-other-format-2",
        "plugin": "com.yvesvogl.silentium",
        "pluginVersion": "0.2.0",
        "name": "Wrong Format",
        "category": "Init",
        "parameters": { "threshold": -1.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongFormatJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    file.deleteFile();
}

//==============================================================================
// 4. Factory presets all parse and load.
TEST_CASE ("PresetManager: every factory preset parses and loads without error", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    const auto factoryCount = std::count_if (all.begin(), all.end(), [] (auto& e) { return e.isFactory; });

    REQUIRE (factoryCount == 10); // docs/presets.md's factory list (9) + Default

    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        CAPTURE (entry.name);
        CHECK (manager.loadPreset (entry.name));
        CHECK (manager.isCurrentPresetFactory());
        CHECK (manager.getCurrentPresetName() == entry.name);
    }
}

TEST_CASE ("PresetManager: factory preset content is plausible (Default is Init category, all parameters in range)", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    const auto defaultEntry = std::find_if (all.begin(), all.end(), [] (auto& e) { return e.name == "Default"; });

    REQUIRE (defaultEntry != all.end());
    CHECK (defaultEntry->category == "Init");
    CHECK (defaultEntry->isFactory);

    // Loading every factory preset must leave every parameter's live value
    // inside its own ParameterLayout range - APVTS's setValueNotifyingHost()
    // clamps out-of-range normalised input, so an out-of-range preset value
    // wouldn't crash, but it would silently mean the JSON doesn't say what
    // the plugin actually does - worth catching explicitly.
    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        REQUIRE (manager.loadPreset (entry.name));

        CHECK (getParam (processor, ParamIDs::threshold) >= -80.0f);
        CHECK (getParam (processor, ParamIDs::threshold) <= 0.0f);
        CHECK (getParam (processor, ParamIDs::attack) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::attack) <= 50.0f);
        CHECK (getParam (processor, ParamIDs::hold) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::hold) <= 250.0f);
        CHECK (getParam (processor, ParamIDs::range) >= -80.0f);
        CHECK (getParam (processor, ParamIDs::range) <= 0.0f);
        CHECK (getParam (processor, ParamIDs::scLowpass) >= 1000.0f);
        CHECK (getParam (processor, ParamIDs::scLowpass) <= 16000.0f);
    }
}

//==============================================================================
// 5. Default resolution order (user Default > factory Default > plain defaults).
TEST_CASE ("PresetManager: applyStartupDefault() loads the factory Default when no user Default exists", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::threshold, -12.0f); // perturb first

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName() == "Default");
    CHECK (manager.isCurrentPresetFactory());
    CHECK (getParam (processor, ParamIDs::threshold) == Catch::Approx (-40.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: a user Default preset wins over the factory Default", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::threshold, -15.0f);
    REQUIRE (manager.setCurrentAsDefault()); // writes a user preset literally named "Default"

    setParam (processor, ParamIDs::threshold, -70.0f); // perturb away before the resolution check

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName() == "Default");
    CHECK_FALSE (manager.isCurrentPresetFactory()); // resolved to the *user* Default, not the factory one
    CHECK (getParam (processor, ParamIDs::threshold) == Catch::Approx (-15.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: resetDefault() removes the user Default so the factory Default resolves again", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::threshold, -15.0f);
    REQUIRE (manager.setCurrentAsDefault());
    REQUIRE (manager.resetDefault());

    manager.applyStartupDefault();

    CHECK (manager.isCurrentPresetFactory());
    CHECK (getParam (processor, ParamIDs::threshold) == Catch::Approx (-40.0f).margin (1.0e-3));
}

//==============================================================================
// 6. Dirty flag: clean after load, dirty after any param change, clean after save.
TEST_CASE ("PresetManager: dirty flag lifecycle - clean after load, dirty after a change, clean after save", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    setParam (processor, ParamIDs::threshold, -30.0f);
    CHECK (manager.isDirty());

    REQUIRE (manager.saveUserPreset ("Dirty Flag Preset", "Init"));
    CHECK_FALSE (manager.isDirty());
}

//==============================================================================
// 7. prev/next ordering and wrap-around.
TEST_CASE ("PresetManager: nextPreset()/previousPreset() traverse alphabetically and wrap around", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    REQUIRE (all.size() >= 2);

    REQUIRE (manager.loadPreset (all.front().name));

    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all[1].name);

    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);

    // Wrap backward from the first entry to the last.
    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.back().name);

    // Wrap forward from the last entry back to the first.
    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);
}

//==============================================================================
// Additional coverage beyond the spec's minimum list: save/rename/delete
// guards, single-file export round-trip, and bank import/export.

TEST_CASE ("PresetManager: saveUserPreset() refuses to shadow a factory preset name", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    CHECK_FALSE (manager.saveUserPreset ("Default", "Init")); // "Default" already exists as a factory preset
    CHECK_FALSE (manager.saveUserPreset ("Chug Lock", "Guitar"));
}

TEST_CASE ("PresetManager: renameUserPreset() moves a user preset to a new name and preserves its parameters", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::release, 321.0f);
    REQUIRE (manager.saveUserPreset ("Old Name", "Init"));

    REQUIRE (manager.renameUserPreset ("Old Name", "New Name"));

    setParam (processor, ParamIDs::release, 5.0f); // perturb before reloading

    CHECK_FALSE (manager.loadPreset ("Old Name")); // gone
    REQUIRE (manager.loadPreset ("New Name"));
    CHECK (getParam (processor, ParamIDs::release) == Catch::Approx (321.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: deleteUserPreset() removes a user preset but never a factory preset", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.saveUserPreset ("Temporary", "Init"));
    REQUIRE (manager.deleteUserPreset ("Temporary"));
    CHECK_FALSE (manager.loadPreset ("Temporary"));

    // A factory preset name isn't a file on disk in the user directory, so
    // there's nothing to delete - deleteUserPreset() must return false, and
    // the factory preset must still load afterwards.
    CHECK_FALSE (manager.deleteUserPreset ("Default"));
    CHECK (manager.loadPreset ("Default"));
}

TEST_CASE ("PresetManager: exportPreset()/importPresetFile() single-file round-trip", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::scLowpass, 3300.0f);
    REQUIRE (manager.saveUserPreset ("Exportable", "Init"));

    const auto exportFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (manager.exportPreset ("Exportable", exportFile));
    REQUIRE (exportFile.existsAsFile());

    REQUIRE (manager.deleteUserPreset ("Exportable")); // remove the original before reimporting

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (exportFile, errorMessage));
    CHECK (getParam (processor, ParamIDs::scLowpass) == Catch::Approx (3300.0f).margin (1.0e-3));

    exportFile.deleteFile();
}

TEST_CASE ("PresetManager: exportBank()/importBank() round-trips every user preset through a zip", "[presets]")
{
    ScopedTestDirectory sourceScratch;
    ScopedTestDirectory destScratch;

    SilentiumAudioProcessor sourceProcessor;
    sourceProcessor.prepareToPlay (48000.0, 512);
    PresetManager sourceManager (sourceProcessor.apvts, makeIsolatedConfig (sourceScratch.dir), makeTestFactoryPresetAssets());

    setParam (sourceProcessor, ParamIDs::threshold, -11.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset A", "Init"));

    setParam (sourceProcessor, ParamIDs::threshold, -22.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset B", "Init"));

    const auto bankFile = juce::File::createTempFile (".zip");
    REQUIRE (sourceManager.exportBank (bankFile));
    REQUIRE (bankFile.existsAsFile());

    SilentiumAudioProcessor destProcessor;
    destProcessor.prepareToPlay (48000.0, 512);
    PresetManager destManager (destProcessor.apvts, makeIsolatedConfig (destScratch.dir), makeTestFactoryPresetAssets());

    const auto importedCount = destManager.importBank (bankFile);
    CHECK (importedCount == 2);

    REQUIRE (destManager.loadPreset ("Bank Preset A"));
    CHECK (getParam (destProcessor, ParamIDs::threshold) == Catch::Approx (-11.0f).margin (1.0e-3));

    REQUIRE (destManager.loadPreset ("Bank Preset B"));
    CHECK (getParam (destProcessor, ParamIDs::threshold) == Catch::Approx (-22.0f).margin (1.0e-3));

    bankFile.deleteFile();
}

//==============================================================================
// 8. PresetManager never allocates or locks on the audio thread.
//
// Verified primarily *by design*: nothing in SilentiumAudioProcessor::
// processBlock()/GateEngine ever calls into PresetManager (see
// PluginProcessor.cpp - presetManager is only touched from the constructor
// and from PresetBar's message-thread-only UI callbacks), so there is no
// code path for this test to exercise in the first place. The one nuance is
// PresetManager::parameterChanged() (an AudioProcessorValueTreeState::
// Listener callback that JUCE does not document as guaranteed message-
// thread-only) - it is implemented as a single lock-free std::atomic<bool>
// store and nothing else (see PresetManager.h/.cpp), which this test
// exercises indirectly by driving parameter changes and processBlock() back
// to back and confirming nothing misbehaves.
TEST_CASE ("PresetManager: parameter-driven dirty tracking coexists safely with real-time audio processing", "[presets]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        // Every parameterChanged() callback below happens interleaved with
        // real audio processing - if it ever became audio-thread-unsafe
        // (e.g. someone later added a lock or allocation to it), a helgrind/
        // TSan CI run would be the real detector; this test's job is just to
        // confirm normal operation isn't disrupted by the two coexisting.
        setParam (processor, ParamIDs::threshold, -80.0f + static_cast<float> (block) * 10.0f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
    }

    CHECK (manager.isDirty());
}

//==============================================================================
// v0.4.0 - T20: factory preset integrity across the version boundary.
//==============================================================================

#include "GoldenFixture.h"
#include "data/GoldenRenders.h"

#include <algorithm>
#include <cmath>

namespace
{
    // One golden per untouched factory preset, in the same order as
    // GoldenFixture::untouchedFactoryPresetNames().
    struct PresetGolden
    {
        const char* name;
        const char* sha256;
        const double* rms;
        const double* peak;
    };

    const std::vector<PresetGolden>& presetGoldens()
    {
        static const std::vector<PresetGolden> goldens {
            { "Default",           GoldenRenders::presetDefaultSha256,         GoldenRenders::presetDefaultRms,         GoldenRenders::presetDefaultPeak },
            { "Natural Decay",     GoldenRenders::presetNaturalDecaySha256,    GoldenRenders::presetNaturalDecayRms,    GoldenRenders::presetNaturalDecayPeak },
            { "Pick Attack Focus", GoldenRenders::presetPickAttackFocusSha256, GoldenRenders::presetPickAttackFocusRms, GoldenRenders::presetPickAttackFocusPeak },
            { "DI-Keyed Workflow", GoldenRenders::presetDIKeyedWorkflowSha256, GoldenRenders::presetDIKeyedWorkflowRms, GoldenRenders::presetDIKeyedWorkflowPeak },
            { "Ambient Sustain",   GoldenRenders::presetAmbientSustainSha256,  GoldenRenders::presetAmbientSustainRms,  GoldenRenders::presetAmbientSustainPeak },
            { "Duck Under Lead",   GoldenRenders::presetDuckUnderLeadSha256,   GoldenRenders::presetDuckUnderLeadRms,   GoldenRenders::presetDuckUnderLeadPeak },
            { "Listen Check",      GoldenRenders::presetListenCheckSha256,     GoldenRenders::presetListenCheckRms,     GoldenRenders::presetListenCheckPeak },
        };

        return goldens;
    }
}

TEST_CASE ("The factory presets v0.4.0 does not re-voice still render exactly as they did", "[presets][golden][neutrality]")
{
    // Anyone whose mix uses one of these presets should hear no difference
    // whatsoever after updating. That is a stronger claim than "the JSON files
    // were not edited" - it also covers every engine change v0.4.0 made - and
    // it is checked against renders captured from a pre-v0.4.0 build.
    REQUIRE (presetGoldens().size() == GoldenFixture::untouchedFactoryPresetNames().size());

    for (const auto& golden : presetGoldens())
    {
        SilentiumAudioProcessor processor;
        GoldenFixture::IsolatedFactoryPresetLoader loader (processor);

        INFO ("factory preset \"" << golden.name << "\"");
        REQUIRE (loader.load (golden.name));

        const auto fingerprint = GoldenFixture::fingerprintOf (GoldenFixture::render (processor));
        REQUIRE (static_cast<int> (fingerprint.windowRmsDb.size()) == GoldenRenders::numWindows);

        const auto comparison = GoldenFixture::compareToGolden (fingerprint, golden.rms, golden.peak,
                                                                 GoldenRenders::numWindows);

        INFO ("deviation from the pre-v0.4.0 render: " << comparison.describe());
        CHECK (comparison.withinPolicy());

        if (GoldenFixture::toolchainTag() == juce::String (GoldenRenders::toolchainTag))
            CHECK (fingerprint.sha256 == juce::String (golden.sha256));
    }
}

TEST_CASE ("The v0.4.0 factory presets carry the values they are documented with", "[presets][v040]")
{
    SilentiumAudioProcessor processor;
    GoldenFixture::IsolatedFactoryPresetLoader loader (processor);

    auto plainValue = [&processor] (const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    };

    SECTION ("Expander Glue is a gentle expander, not a gate")
    {
        REQUIRE (loader.load ("Expander Glue"));

        // The point of this preset: a finite ratio and a shallow floor, so
        // tails are reduced rather than switched off.
        CHECK (plainValue (ParamIDs::ratio) == Catch::Approx (2.5f).margin (0.01f));
        CHECK (plainValue (ParamIDs::range) == Catch::Approx (-18.0f).margin (0.05f));
        CHECK (plainValue (ParamIDs::detector) == Catch::Approx (static_cast<float> (ParamConstants::detectorRms)));
        CHECK (plainValue (ParamIDs::releaseShape) == Catch::Approx (static_cast<float> (ParamConstants::releaseShapeLinear)));
        CHECK (plainValue (ParamIDs::smoothOpen) == Catch::Approx (1.0f));
    }

    SECTION ("Chug Lock is the one deliberately re-voiced preset, and it gained exactly one control")
    {
        REQUIRE (loader.load ("Chug Lock"));

        CHECK (plainValue (ParamIDs::smoothOpen) == Catch::Approx (1.0f));

        // Still a gate, still on the legacy detector and filter slope - the
        // re-voicing is exactly one control, which is what the CHANGELOG says.
        CHECK (plainValue (ParamIDs::ratio) == Catch::Approx (ParamConstants::maxRatio).margin (0.01f));
        CHECK (plainValue (ParamIDs::detector) == Catch::Approx (static_cast<float> (ParamConstants::detectorPeak)));
        CHECK (plainValue (ParamIDs::scSlope) == Catch::Approx (static_cast<float> (ParamConstants::scSlope12)));
        CHECK (plainValue (ParamIDs::releaseShape) == Catch::Approx (static_cast<float> (ParamConstants::releaseShapeExponential)));
        CHECK (plainValue (ParamIDs::hysteresis) == Catch::Approx (3.0f).margin (0.05f));
    }

    SECTION ("Surgical Mute was deliberately NOT given Smooth Open")
    {
        // Smooth Open shapes the closing edge as well as the opening one (the
        // moving-max window holds the open target for up to half the lookahead
        // on falls, and the box cascade then spreads the close). That is a
        // good trade almost everywhere, and the wrong one for the preset whose
        // entire purpose is the tightest possible inter-note silence - with it
        // enabled, Surgical Mute stopped being quieter between notes than
        // Natural Decay, which is a guarantee tests/DesignBriefTests.cpp
        // asserts. Documented in the CHANGELOG and docs/presets.md.
        REQUIRE (loader.load ("Surgical Mute"));
        CHECK (plainValue (ParamIDs::smoothOpen) == Catch::Approx (0.0f));
    }

    SECTION ("every factory preset loads")
    {
        for (const auto& entry : loader.getAllPresetNames())
        {
            INFO ("preset " << entry);
            CHECK (loader.load (entry));
        }
    }
}
