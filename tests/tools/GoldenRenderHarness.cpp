// Offline golden-render harness (brief §4 "Golden-render procedure", §5 file
// plan). NOT part of the Catch2 Tests binary - it is a standalone executable
// whose whole job is to render tests/GoldenFixture.h's fixed stimulus through
// the plugin and print tests/data/GoldenRenders.h to stdout.
//
// WHY IT EXISTS
// -------------
// The v0.4.0 neutrality claim is "a v0.3.x session renders identically under
// v0.4.0". Comparing two renders produced by the *same* v0.4.0 binary cannot
// prove that: a regression in the legacy code path shifts both sides of such
// a comparison identically and the test stays green. The only artifact that
// can prove it is a render captured from the PREVIOUS version's code and
// checked in. This harness captures it.
//
// HOW THE CHECKED-IN ARTIFACT WAS GENERATED
// -----------------------------------------
// Build and run it at a checkout whose src/ tree is the v0.3.x engine (for
// v0.4.0 that was origin/main @ e9bceb9, before a single line of engine work
// landed on the feature branch), then commit the output verbatim:
//
//   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
//   cmake --build build --target GoldenRenderHarness
//   ./build/GoldenRenderHarness > tests/data/GoldenRenders.h
//
// Regenerating it at any later commit defeats its entire purpose. See
// tests/data/README.md.

#include "../GoldenFixture.h"

#include <iostream>

namespace
{
    juce::String quoted (const juce::String& text)
    {
        return "\"" + text + "\"";
    }

    // %.17g round-trips an IEEE-754 double exactly, so the checked-in header
    // reproduces the generating run's fingerprint values bit-for-bit rather
    // than to some arbitrary printed precision.
    juce::String formatDouble (double value)
    {
        return juce::String (juce::String::formatted ("%.17g", value));
    }

    void printFingerprintArrays (const juce::String& symbol, const GoldenFixture::Fingerprint& fingerprint)
    {
        std::cout << "    inline constexpr double " << symbol << "Rms[] = {\n";

        for (size_t i = 0; i < fingerprint.windowRmsDb.size(); ++i)
            std::cout << "        " << formatDouble (fingerprint.windowRmsDb[i]) << ",\n";

        std::cout << "    };\n\n";

        std::cout << "    inline constexpr double " << symbol << "Peak[] = {\n";

        for (size_t i = 0; i < fingerprint.windowPeakDb.size(); ++i)
            std::cout << "        " << formatDouble (fingerprint.windowPeakDb[i]) << ",\n";

        std::cout << "    };\n\n";
    }

    // Turns a preset name into a valid C++ identifier fragment
    // ("Pick Attack Focus" -> "pickAttackFocus").
    juce::String toSymbol (const juce::String& name)
    {
        juce::String symbol;
        auto capitaliseNext = false;

        for (auto character : name)
        {
            if (! juce::CharacterFunctions::isLetterOrDigit (character))
            {
                capitaliseNext = true;
                continue;
            }

            if (symbol.isEmpty())
                symbol << juce::String::charToString (character).toLowerCase();
            else if (capitaliseNext)
                symbol << juce::String::charToString (character).toUpperCase();
            else
                symbol << juce::String::charToString (character);

            capitaliseNext = false;
        }

        return symbol;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    struct Entry
    {
        juce::String symbol;
        juce::String label;
        GoldenFixture::Fingerprint fingerprint;
    };

    std::vector<Entry> entries;
    juce::String legacyStateBase64;

    // ---- 1. The legacy-state render (brief §6-T1) --------------------------
    {
        SilentiumAudioProcessor processor;
        GoldenFixture::applyLegacySettings (processor);

        juce::MemoryBlock stateBlob;
        processor.getStateInformation (stateBlob);
        legacyStateBase64 = juce::Base64::toBase64 (stateBlob.getData(), stateBlob.getSize());

        entries.push_back ({ "legacyState",
                             "v0.3.x state blob (GoldenFixture::LegacySettings)",
                             GoldenFixture::fingerprintOf (GoldenFixture::render (processor)) });
    }

    // ---- 2. The untouched factory presets (brief §6-T20) -------------------
    for (const auto& name : GoldenFixture::untouchedFactoryPresetNames())
    {
        SilentiumAudioProcessor processor;
        GoldenFixture::IsolatedFactoryPresetLoader loader (processor);

        if (! loader.load (name))
        {
            std::cerr << "GoldenRenderHarness: factory preset not found: " << name << std::endl;
            return 1;
        }

        entries.push_back ({ "preset" + toSymbol (name).substring (0, 1).toUpperCase() + toSymbol (name).substring (1),
                             "factory preset \"" + name + "\"",
                             GoldenFixture::fingerprintOf (GoldenFixture::render (processor)) });
    }

    // ---- 3. Emit tests/data/GoldenRenders.h --------------------------------
    std::cout <<
        "// GENERATED FILE - DO NOT EDIT BY HAND.\n"
        "//\n"
        "// Cross-version golden renders for Silentium, produced by\n"
        "// tests/tools/GoldenRenderHarness.cpp built at the PREVIOUS release's\n"
        "// engine sources. See tests/data/README.md for the generating commit,\n"
        "// toolchain, flags, and the regeneration policy (short version: do not\n"
        "// regenerate to make a failing test pass - a failure here means the\n"
        "// legacy signal path changed).\n"
        "\n"
        "#pragma once\n"
        "\n"
        "namespace GoldenRenders\n"
        "{\n";

    std::cout << "    // Build configuration that produced the SHA-256 values below. The\n"
                 "    // exact-hash (Tier B) assertions only run when the test binary's own\n"
                 "    // GoldenFixture::toolchainTag() matches this string; the windowed\n"
                 "    // fingerprint (Tier A) assertions always run. See GoldenFixture.h.\n";
    std::cout << "    inline constexpr const char* toolchainTag = " << quoted (GoldenFixture::toolchainTag()) << ";\n\n";

    std::cout << "    // Base64 of a real getStateInformation() blob captured from the\n"
                 "    // previous version - i.e. exactly what a v0.3.x host session hands to\n"
                 "    // setStateInformation(). Carries no stateVersion attribute, which is\n"
                 "    // what makes it a genuine v1-schema migration input.\n";
    std::cout << "    inline constexpr const char* legacyStateBlobBase64 =\n        " << quoted (legacyStateBase64) << ";\n\n";

    std::cout << "    inline constexpr int numWindows = " << GoldenFixture::numWindows << ";\n\n";

    for (const auto& entry : entries)
    {
        std::cout << "    // " << entry.label << "\n";
        std::cout << "    inline constexpr const char* " << entry.symbol << "Sha256 = "
                  << quoted (entry.fingerprint.sha256) << ";\n\n";
        printFingerprintArrays (entry.symbol, entry.fingerprint);
    }

    std::cout << "}\n";

    return 0;
}
