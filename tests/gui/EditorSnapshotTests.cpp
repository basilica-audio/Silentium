#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/AnalogMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <iterator>
#include <vector>

// GUI smoke tests for the master-05 baseline editor (src/PluginEditor.h,
// src/gui/). juce::ScopedJuceInitialiser_GUI is installed once for the
// whole test binary in tests/TestMain.cpp, so Components/Timers are safe to
// construct here even though this is a headless console executable with no
// running message loop (timers simply never fire, which is fine - these
// tests only exercise synchronous construction/paint/destruction).
TEST_CASE ("Editor constructs, lays out, and destroys cleanly", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    {
        SilentiumAudioProcessorEditor editor (processor);

        CHECK (editor.getWidth() > 0);
        CHECK (editor.getHeight() > 0);
    }
    // editor destroyed here - JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // (used throughout src/gui/ and on the editor itself) asserts at process
    // exit in Debug builds if any tagged instance was ever leaked, so a
    // clean run of this whole test binary is itself the leak check.
}

namespace
{
    // Local copy of EditorAccessibilityTests.cpp's findChildByTitle helper -
    // deliberately not shared/exported between test files (each Catch2
    // .cpp here is meant to be readable standalone), see that file's own
    // docs for why a flat, non-recursive scan is sufficient (every control
    // this touches is a direct child of the editor, never nested in a
    // sub-container).
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;

        return nullptr;
    }

    // Configures a deliberately "alive-looking" state before snapshotting -
    // Yves' explicit request for the master-05 baseline revision: Duck OFF
    // (exercises the master-06 toggle-zone crop-swap overlay), Listen ON
    // (stays at master-05's own baked UP artwork), the two VU needles at
    // different, non-zero positions with the Input meter's peak LED lit,
    // the vent-glow mix partway between master-glow-dim and master-05's own
    // ceiling, and the knob grid at varied positions rather than every
    // control sitting at a uniform 12 o'clock rest pose (the knobs
    // themselves never visibly rotate - see PluginEditor.h's docs - but
    // exercising real, varied APVTS values here is still worthwhile: it's
    // what the accessible value-popup text and the SliderAttachment wiring
    // actually reflect).
    //
    // AnalogMeter::setTargetDb() + this component's own ~300ms ballistic
    // ramp, and the editor's own timerCallback()-driven vent-glow ballistics,
    // would need real timer ticks pumped through a running message loop to
    // actually reach these values - this headless test binary has no such
    // loop (see the top-of-file docs), so setImmediateDbForPreview() /
    // setVentGlowMixForPreview() (test/preview-only, see AnalogMeter.h's and
    // PluginEditor.h's docs) seed the ballistic-smoothed readings, the
    // peak-LED state, and the vent-glow mix directly instead.
    void configureLiveLookingState (SilentiumAudioProcessorEditor& editor)
    {
        if (auto* gr = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Gain Reduction meter"))
            gr->setImmediateDbForPreview (-7.0f);

        if (auto* input = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Input Level meter"))
            input->setImmediateDbForPreview (2.0f); // >= AnalogMeter::peakLedThresholdDb - lights the peak LED

        if (auto* duck = findChildByTitle<juce::ToggleButton> (editor, "Duck"))
            duck->setToggleState (false, juce::dontSendNotification); // OFF -> master-06 crop at this toggle

        if (auto* listen = findChildByTitle<juce::ToggleButton> (editor, "Listen"))
            listen->setToggleState (true, juce::dontSendNotification); // ON -> master-05's own baked UP artwork

        editor.setVentGlowMixForPreview (0.6f); // between master-glow-dim and master-05's own ceiling

        struct KnobValue
        {
            const char* label;
            double normalisedValue; // 0..1 proportion of the slider's own range, deliberately varied (not all 0.5)
        };

        const KnobValue knobValues[] = {
            { "Threshold", 0.30 }, { "Attack", 0.70 }, { "Hold", 0.15 },
            { "Release", 0.55 }, { "Range", 0.85 }, { "Lookahead", 0.40 },
            { "SC HPF", 0.60 }, { "SC LPF", 0.25 }, { "Knee", 0.75 },
            // Issue #33 aux bay: varied non-rest values here DO visibly
            // rotate the filmstrip knobs (unlike the plate knobs' baked
            // artwork) - deliberately neither 0.5 nor an extreme.
            { "Ratio", 0.40 }, { "Hysteresis", 0.65 },
        };

        for (const auto& kv : knobValues)
            if (auto* knob = findChildByTitle<juce::Slider> (editor, kv.label))
                knob->setValue (knob->proportionOfLengthToValue (kv.normalisedValue), juce::dontSendNotification);

        // Aux switches: one flipped ON (exercises the on-frame + the
        // re-lit upper legend), the rest at their off defaults - so the
        // committed preview shows both switch poses.
        if (auto* detector = findChildByTitle<juce::ToggleButton> (editor, "Detector"))
            detector->setToggleState (true, juce::dontSendNotification);
    }
}

TEST_CASE ("Editor snapshot at 100% is non-blank and is written for PR review", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    SilentiumAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    configureLiveLookingState (editor);

    // SoftwareImageType (rather than the default NativeImageType) avoids any
    // dependency on an actual native graphics context/window, which keeps
    // this robust on headless CI runners.
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});

    REQUIRE (snapshot.isValid());
    CHECK (snapshot.getWidth() == editor.getWidth());
    CHECK (snapshot.getHeight() == editor.getHeight());

    // Non-blank: sample a small grid of points and confirm they are not all
    // identical to the top-left corner - a completely blank/solid-fill
    // render (e.g. every asset failing to decode) would fail this.
    const auto reference = snapshot.getPixelAt (0, 0);
    bool foundDifference = false;

    for (int y = 0; y < snapshot.getHeight() && ! foundDifference; y += juce::jmax (1, snapshot.getHeight() / 20))
        for (int x = 0; x < snapshot.getWidth() && ! foundDifference; x += juce::jmax (1, snapshot.getWidth() / 20))
            if (snapshot.getPixelAt (x, y) != reference)
                foundDifference = true;

    CHECK (foundDifference);

    // Written for local/PR review (see docs/gui-preview.png, a committed
    // static copy of a run of this test) - path is relative to the test
    // binary's current working directory, which `ctest --test-dir build`
    // sets to the build directory, landing this at build/gui-preview.png.
    juce::PNGImageFormat pngFormat;
    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile ("gui-preview.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (snapshot, *stream));
    }
}

// Item 4 (rotating knobs, INNER-DISC variant) proof: confirms three knobs,
// spanning both rows and both rotation directions, visibly rotate away from
// their construction-time (APVTS-default) pose once set to distinctly
// non-centre proportions, and writes a zoom crop of all three - at their
// rotated positions - to build/knob-rotation-zoom.png for visual review
// (checking for the double-edge/halo seam the INNER-DISC variant is meant
// to structurally rule out - see PluginEditor.cpp's step-2 paint() docs -
// is a visual judgement call this test cannot make on its own, hence the
// written PNG rather than a purely numeric assertion).
TEST_CASE ("Rotating knob discs visibly rotate at distinctly non-centre values (item 4 zoom proof)", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    SilentiumAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    const auto restSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (restSnapshot.isValid());

    // Threshold/Range: row 1, opposite extremes (far CCW / far CW). SC LPF:
    // row 2, a distinct partial value - deliberately none of the three at
    // 0.5 (the baked rest pose every knob starts at).
    struct ZoomKnob
    {
        const char* label;
        double proportion;
    };

    constexpr ZoomKnob zoomKnobs[] = {
        { "Threshold", 0.05 },
        { "Range", 0.95 },
        { "SC LPF", 0.25 },
    };

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);
        knob->setValue (knob->proportionOfLengthToValue (zk.proportion), juce::dontSendNotification);
    }

    const auto movedSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (movedSnapshot.isValid());

    std::vector<juce::Image> movedCrops;
    movedCrops.reserve (std::size (zoomKnobs));

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);

        // Expanded a few px past the slider's own (transparent) hit-area
        // bounds so the crop also shows the baked outer rim right around
        // the rotating inner disc, not just the disc itself.
        const auto cropBounds = knob->getBounds().expanded (8);
        const auto restCrop = restSnapshot.getClippedImage (cropBounds);
        const auto movedCrop = movedSnapshot.getClippedImage (cropBounds);

        REQUIRE (restCrop.isValid());
        REQUIRE (movedCrop.isValid());
        REQUIRE (restCrop.getWidth() == movedCrop.getWidth());
        REQUIRE (restCrop.getHeight() == movedCrop.getHeight());

        // Pointer/knurl must have visibly moved: count pixels whose
        // per-channel sum differs by more than a small AA-noise threshold
        // between the rest and moved crops - a meaningfully large fraction
        // must differ, not just a handful of edge-AA pixels.
        int changedPixels = 0;
        const int totalPixels = restCrop.getWidth() * restCrop.getHeight();

        for (int y = 0; y < restCrop.getHeight(); ++y)
        {
            for (int x = 0; x < restCrop.getWidth(); ++x)
            {
                const auto a = restCrop.getPixelAt (x, y);
                const auto b = movedCrop.getPixelAt (x, y);
                const auto diff = std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                                 + std::abs (a.getBlue() - b.getBlue());
                if (diff > 24)
                    ++changedPixels;
            }
        }

        INFO (zk.label << ": " << changedPixels << "/" << totalPixels << " px changed between rest and moved pose");
        CHECK (changedPixels > totalPixels / 20); // >5% of the crop visibly moved

        movedCrops.push_back (movedCrop);
    }

    REQUIRE (movedCrops.size() == std::size (zoomKnobs));

    const auto cropWidth = movedCrops.front().getWidth();
    const auto cropHeight = movedCrops.front().getHeight();

    constexpr int gap = 10;
    juce::Image zoomImage (juce::Image::RGB,
                           cropWidth * (int) movedCrops.size() + gap * ((int) movedCrops.size() + 1),
                           cropHeight + gap * 2, true);
    {
        juce::Graphics g (zoomImage);
        g.fillAll (juce::Colours::black);
        for (size_t i = 0; i < movedCrops.size(); ++i)
            g.drawImageAt (movedCrops[i], gap + (int) i * (cropWidth + gap), gap);
    }

    juce::PNGImageFormat pngFormat;
    const auto zoomFile = juce::File::getCurrentWorkingDirectory().getChildFile ("knob-rotation-zoom.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (zoomFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (zoomImage, *stream));
    }
    else
    {
        FAIL ("could not open output stream for knob-rotation-zoom.png");
    }
}

// Issue #33 (aux control bay) visual-wiring proof: the aux controls must
// RENDER their state, not just hold it - a filmstrip knob at a different
// value draws a different frame, and flipping a switch both changes the
// lever frame AND re-lights the position legends the editor draws around
// it. Counted as changed pixels between two real editor snapshots, same
// technique as the knob-disc rotation proof above - a decorative stub
// (control present but not painting its state) fails this immediately.
TEST_CASE ("Aux-bay knob and switch visibly render their state changes", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    SilentiumAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    auto* ratio = findChildByTitle<juce::Slider> (editor, "Ratio");
    auto* detector = findChildByTitle<juce::ToggleButton> (editor, "Detector");
    REQUIRE (ratio != nullptr);
    REQUIRE (detector != nullptr);

    const auto before = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (before.isValid());

    // Ratio: default is the top of the range (the ∞:1 gate detent) - move
    // to the far opposite extreme for the largest frame distance.
    ratio->setValue (ratio->proportionOfLengthToValue (0.05), juce::dontSendNotification);
    detector->setToggleState (true, juce::dontSendNotification);

    const auto after = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (after.isValid());
    REQUIRE (after.getWidth() == before.getWidth());
    REQUIRE (after.getHeight() == before.getHeight());

    const auto countChangedPixels = [&] (juce::Rectangle<int> region)
    {
        int changed = 0;

        for (int y = region.getY(); y < region.getBottom(); ++y)
        {
            for (int x = region.getX(); x < region.getRight(); ++x)
            {
                const auto a = before.getPixelAt (x, y);
                const auto b = after.getPixelAt (x, y);
                const auto diff = std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                                 + std::abs (a.getBlue() - b.getBlue());
                if (diff > 24)
                    ++changed;
            }
        }

        return changed;
    };

    // Knob: a meaningfully large fraction of the knob's own bounds must
    // have moved (the pointer notch swept ~250deg of the strip).
    {
        const auto region = ratio->getBounds();
        const auto changed = countChangedPixels (region);
        INFO ("Ratio knob: " << changed << "/" << region.getWidth() * region.getHeight() << " px changed");
        CHECK (changed > region.getWidth() * region.getHeight() / 20);
    }

    // Switch: the lever frame flipped.
    {
        const auto region = detector->getBounds();
        const auto changed = countChangedPixels (region);
        INFO ("Detector switch: " << changed << "/" << region.getWidth() * region.getHeight() << " px changed");
        CHECK (changed > 0);
    }

    // Legends: the active option moved from "Peak" (below) to "RMS"
    // (above) - both legend rows around the Detector column re-lit.
    // Derived from the same layout constants PluginEditor.cpp's paint()
    // draws with, at the editor's default 100% scale step.
    {
        using namespace slnt::layout;

        constexpr auto bayTopY = topStripHeight1x + topStripGap1x + plateHeight1x + auxBayGap1x;
        constexpr auto detectorColumnX = auxColumnX1x[(size_t) auxKnobCount]; // first switch column

        const auto legendRow = [&] (int centreY1x)
        {
            return juce::Rectangle<int> (auxLegendWidth1x, auxLegendHeight1x + 4)
                       .withCentre ({ detectorColumnX, bayTopY + centreY1x });
        };

        const auto changedOn = countChangedPixels (legendRow (auxLegendOnCentreY1x));
        const auto changedOff = countChangedPixels (legendRow (auxLegendOffCentreY1x));
        INFO ("legend px changed: on-row " << changedOn << ", off-row " << changedOff);
        CHECK (changedOn > 0);
        CHECK (changedOff > 0);
    }
}

// Motion proof, from the REAL app render pipeline (not the offline
// needle_diff analysis scripts): sweeps the Input Level meter's needle
// through {-20,-7,-5,0,+2} dB, captures a real editor snapshot at each
// position, crops to that meter's own component bounds, and checks that the
// per-pixel range across the 5 crops (the "needle fan") stays essentially
// zero inside a small disc around PluginEditorLayout.h's meterRPivotX/
// YFraction - i.e. the needle's base doesn't wobble as it sweeps, which is
// exactly the defect the 2026-07-23 per-meter pivot fix (see that file's
// docs) was meant to close. writePngProof() below is a static-write helper
// (no threads/timers), so this stays a synchronous, headless test like the
// rest of this file.
TEST_CASE ("Input meter needle fan pivots cleanly on its own true hub across a dB sweep", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    SilentiumAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    auto* input = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Input Level meter");
    REQUIRE (input != nullptr);

    constexpr std::array<float, 5> sweepDb { -20.0f, -7.0f, -5.0f, 0.0f, 2.0f };

    std::vector<juce::Image> crops;
    crops.reserve (sweepDb.size());

    for (const auto db : sweepDb)
    {
        input->setImmediateDbForPreview (db);

        const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
        REQUIRE (snapshot.isValid());

        // input->getBounds() is this meter's own bounds within the editor -
        // exactly the region the snapshot above was rendered into, so this
        // crop lines up pixel-for-pixel with the component's own coordinate
        // space (which is what PluginEditorLayout.h's meterRPivotXFraction/
        // meterRPivotYFraction are expressed as fractions of).
        crops.push_back (snapshot.getClippedImage (input->getBounds()));
    }

    REQUIRE (crops.size() == sweepDb.size());
    for (const auto& crop : crops)
    {
        REQUIRE (crop.isValid());
        REQUIRE (crop.getWidth() == crops.front().getWidth());
        REQUIRE (crop.getHeight() == crops.front().getHeight());
    }

    const auto cropWidth = crops.front().getWidth();
    const auto cropHeight = crops.front().getHeight();

    // --- (a) build/needle-sweep.png: the 5 crops side by side ---
    constexpr int gap = 10;
    juce::Image sweepImage (juce::Image::RGB, (int) (cropWidth * (int) crops.size() + gap * ((int) crops.size() + 1)),
                             cropHeight + gap * 2, true);
    {
        juce::Graphics g (sweepImage);
        g.fillAll (juce::Colours::black);
        for (size_t i = 0; i < crops.size(); ++i)
            g.drawImageAt (crops[i], gap + (int) i * (cropWidth + gap), gap);
    }

    // --- (b) build/needle-pivot-overlay.png: per-pixel MAX-DIFFERENCE image
    // across the 5 crops (the needle fan) - value at each pixel is the
    // largest per-channel range (max - min across the 5 frames), summed over
    // R/G/B, i.e. exactly zero wherever the pixel never changes across the
    // whole sweep (the static face/bezel/hub) and large wherever the needle
    // blade sweeps through. ---
    juce::Image diffImage (juce::Image::RGB, cropWidth, cropHeight, true);

    // PluginEditorLayout.h's per-meter pivot fraction for THIS (right/input)
    // dial, expressed in this crop's own local pixel space (crop size ==
    // meterComponentSize1x at the editor's default 100% scale step, see
    // PluginEditor.cpp's resized()/scaleStepIndex docs).
    const auto pivotXLocal = slnt::layout::meterRPivotXFraction * (float) slnt::layout::meterComponentSize1x;
    const auto pivotYLocal = slnt::layout::meterRPivotYFraction * (float) slnt::layout::meterComponentSize1x;

    // v0.3.11 (single master-extracted sprite, no hub occluder): the
    // v0.3.7 hub-OCCLUDER cap disc this probe radius used to be calibrated
    // against is gone (see AnalogMeter.h's top-of-file docs - the new
    // needle-from-master.png sprite's own alpha already handles the
    // occlusion, no separate redraw needed). The guaranteed-static zone is
    // now that sprite's own verified-zero-alpha region around its pivot:
    // needle-from-master.json's REVISION 2 alpha mask has its first
    // nonzero pixel at radius 16.12 master px (independently re-measured
    // against the shipped asset, not copied from that .json's own possibly
    // stale prose - see this file's own handoff notes). Converted to @1x
    // by plateWidth1x/masterCanvasWidthPx (900/1264): 16.12 * 900/1264 ~=
    // 11.48px @1x - probed at 9px to keep a deliberate, non-arbitrary
    // margin (~22%) below that boundary, safely clear of the sprite's own
    // antialiased edge.
    constexpr float hubCapRadius1x = 9.0f;

    double totalDiffEnergy = 0.0;
    double diskDiffEnergy = 0.0;

    {
        juce::Image::BitmapData diffWrite (diffImage, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < cropHeight; ++y)
        {
            for (int x = 0; x < cropWidth; ++x)
            {
                float minR = 255.0f, maxR = 0.0f;
                float minG = 255.0f, maxG = 0.0f;
                float minB = 255.0f, maxB = 0.0f;

                for (const auto& crop : crops)
                {
                    const auto c = crop.getPixelAt (x, y);
                    minR = juce::jmin (minR, (float) c.getRed());
                    maxR = juce::jmax (maxR, (float) c.getRed());
                    minG = juce::jmin (minG, (float) c.getGreen());
                    maxG = juce::jmax (maxG, (float) c.getGreen());
                    minB = juce::jmin (minB, (float) c.getBlue());
                    maxB = juce::jmax (maxB, (float) c.getBlue());
                }

                const float diff = (maxR - minR) + (maxG - minG) + (maxB - minB); // 0..765
                const auto vis = (juce::uint8) juce::jlimit (0, 255, (int) std::lround (diff));
                diffWrite.setPixelColour (x, y, juce::Colour::fromRGB (vis, vis, vis));

                totalDiffEnergy += (double) diff;

                const auto dx = (float) x - pivotXLocal;
                const auto dy = (float) y - pivotYLocal;
                if (dx * dx + dy * dy <= hubCapRadius1x * hubCapRadius1x)
                    diskDiffEnergy += (double) diff;
            }
        }
    }

    const auto diskEnergyFraction = totalDiffEnergy > 0.0 ? diskDiffEnergy / totalDiffEnergy : 0.0;

    std::cout << "[needle-fan] hub-disc pivot @ (" << pivotXLocal << ", " << pivotYLocal << ") r=" << hubCapRadius1x
               << "px; disk/total diff energy = " << diskDiffEnergy << "/" << totalDiffEnergy << " = "
               << (diskEnergyFraction * 100.0) << "%\n";

    INFO ("hub-disc pivot @ (" << pivotXLocal << ", " << pivotYLocal << ") r=" << hubCapRadius1x
                                << "; disk/total diff energy = " << diskDiffEnergy << "/" << totalDiffEnergy
                                << " = " << (diskEnergyFraction * 100.0) << "%");

    // v0.3.11: inside this (smaller, sprite-derived) probe disc every pixel
    // is master-05's own static baked hub-cap/anchor-bar/boss art at every
    // sweep angle - the needle sprite itself is verified alpha=0 there, so
    // it never draws anything into this region at all (rather than the old
    // architecture's occluder redraw actively covering it) - so the sweep
    // difference must be essentially zero. This still guards exactly the
    // same regression class as before: a mispositioned pivot would smear
    // difference energy into the disc as soon as any part of the sprite's
    // real (nonzero-alpha) content swept across it.
    //
    // Threshold calibration (measured locally, this revision): correct
    // placement = exactly 0% (the sprite's own verified-transparent zone
    // fully covers the 9px probe disc). 0.5% keeps a wide CI-stable margin
    // for renderer AA differences while still failing hard on a
    // mispositioned pivot or a needle asset whose alpha stops being
    // transparent this close to its own centre.
    CHECK (totalDiffEnergy > 0.0); // sanity: the needle actually moved
    CHECK (diskEnergyFraction < 0.005);

    juce::PNGImageFormat pngFormat;

    const auto writePng = [&pngFormat] (const juce::Image& image, const char* filename)
    {
        const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (filename);
        if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
        {
            stream->setPosition (0);
            stream->truncate();
            CHECK (pngFormat.writeImageToStream (image, *stream));
        }
        else
        {
            FAIL ("could not open output stream for " << filename);
        }
    };

    // Written for local/PR review, same convention as gui-preview.png above
    // - paths are relative to the test binary's cwd, which `ctest
    // --test-dir build` sets to the build directory (build/needle-sweep.png,
    // build/needle-pivot-overlay.png).
    writePng (sweepImage, "needle-sweep.png");
    writePng (diffImage, "needle-pivot-overlay.png");
}

// Item 5 (visible idle flicker) proof: at idle (PluginProcessor's own
// default meter readings - -100dBFS input, -100dB gain reduction (idle-rest
// fix, see PluginProcessor.h's docs - previously 0dB, which incorrectly
// parked the gain-reduction needle on the dial's 0dB tick instead of resting
// at -20 like the input meter) - reached without ever calling processBlock(),
// genuinely the "silence" state item 5's brief is about) captures two editor
// snapshots and writes their
// per-pixel absolute difference (amplified x4) plus the two frames
// themselves, side by side, to build/flicker-diff.png for visual review.
// Both tube-vent glow banks (idle breathing,
// PluginEditor::updateVentGlowMix()) and both VU bulb (incandescent lamp)
// glows (AnalogMeter::currentFlickerMultiplier()) must visibly differ
// between the frames; the needle/peak-LED/knobs must not - none of their
// own state machines run without a pumped message loop this headless test
// binary doesn't have (see AnalogMeter::setImmediateDbForPreview()'s own
// docs for the identical rationale), so this test freezes them explicitly
// rather than relying on that as an accident of the test environment.
//
// Uses the deterministic setFlickerElapsedSecondsForPreview()/
// setVentGlowElapsedSecondsForPreview() hooks (see their own docs) rather
// than a real wall-clock sleep between the two captures: a fixed short
// real-time gap (the brief's own "~0.4s apart" wording) is NOT reliable
// here - the underlying flicker is a weighted SUM of several non-harmonic
// sine layers, which is not monotonic, so a short, arbitrary real-time gap
// can occasionally land two samples on a near-zero delta purely by chance
// (this was observed directly during development: a real 400ms gap
// produced a tube-vent diff clearly visible, but a VU-bulb-glow diff of
// only ~3/765, an unlucky partial cancellation across AnalogMeter's own 3
// flicker layers, nowhere near the amplitude actually shipped - and even a
// large RELATIVE clock offset turned out not to be reliable either, for
// the same underlying reason, since it still depends on whatever the real
// "now" happens to be when the test runs). Setting each element's own
// simulated ABSOLUTE elapsed time directly removes real "now" from the
// equation entirely - frame 1 uses elapsed=0.0 (each layer's own phase
// origin) for all three flickering elements, frame 2 uses a per-element
// elapsed value independently pre-verified (by offline simulation of the
// exact same weighted-sine formula, see each constant's own comment below)
// to land near that element's own maximum achievable delta from elapsed=0.
TEST_CASE ("Vent-glow idle breathing and VU bulb glow flicker are visibly time-varying at idle (item 5 proof)", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    // meterInputLevelDb/meterGainReductionDb default to -100dB/0dB
    // (PluginProcessor.h) without ever calling processBlock() - genuinely
    // idle/silent already, but see below for the explicit freeze anyway.

    SilentiumAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    // Freeze the needle/peak-LED state explicitly - well below
    // AnalogMeter::peakLedThresholdDb, so the peak LED stays fully off (no
    // draw call at all, see PluginEditor::paint()'s own alpha<=0.001f skip)
    // and cannot contaminate the diff with its own hold/fade animation.
    auto* grMeter = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Gain Reduction meter");
    auto* inputMeter = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Input Level meter");
    REQUIRE (grMeter != nullptr);
    REQUIRE (inputMeter != nullptr);

    grMeter->setImmediateDbForPreview (-100.0f);
    inputMeter->setImmediateDbForPreview (-100.0f);

    // Per-element (elapsed1, elapsed2) pairs - an offline Python Monte-Carlo
    // search (300k random (t1,t2) pairs in [0,500)s) over the identical
    // weighted-sine formula this file's production code uses
    // (Flicker.h's flickerMultiplier(), AnalogMeter::
    // currentFlickerMultiplier()'s fast+slow combination, and
    // PluginEditor::updateVentGlowMix()'s idle-breathing term), independently
    // per element's own phase seed (0.0 / 1.0 / 5.0 respectively), kept
    // whichever pair maximised |value(t2)-value(t1)|. Each element's own
    // theoretical ceiling is 2*(sum of its own layers' amplitudes) - the
    // search landed within ~85% (meters) / ~88% (vent) of that ceiling, so
    // these are close to the largest delta these amplitudes can ever
    // produce, not an arbitrary/lucky pair.
    grMeter->setFlickerElapsedSecondsForPreview (317.2);
    inputMeter->setFlickerElapsedSecondsForPreview (25.6);
    editor.setVentGlowElapsedSecondsForPreview (413.2);

    const auto frame1 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame1.isValid());

    grMeter->setFlickerElapsedSecondsForPreview (267.2);
    inputMeter->setFlickerElapsedSecondsForPreview (217.0);
    editor.setVentGlowElapsedSecondsForPreview (138.6);

    const auto frame2 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame2.isValid());

    REQUIRE (frame1.getWidth() == frame2.getWidth());
    REQUIRE (frame1.getHeight() == frame2.getHeight());

    const auto width = frame1.getWidth();
    const auto height = frame1.getHeight();

    // Screen-space regions expected to legitimately differ: both tube-vent
    // banks (idle breathing) and both AnalogMeter component bounds (VU
    // bulb glow - the needle/LED drawn inside that same box are frozen
    // above, so any diff inside these bounds is attributable to the glow
    // alone). Derived the same way PluginEditor::resized()/paint() derive
    // them at the editor's own default 100% scale step (PluginEditorLayout.h).
    using namespace slnt::layout;

    constexpr auto plateOriginY = topStripHeight1x + topStripGap1x;
    const auto toScreenRect = [] (juce::Rectangle<int> local1x)
    {
        return juce::Rectangle<int> (local1x.getX(), plateOriginY + local1x.getY(),
                                     local1x.getWidth(), local1x.getHeight());
    };

    const auto ventLScreen = toScreenRect (ventLBankBounds1x);
    const auto ventRScreen = toScreenRect (ventRBankBounds1x);
    const auto meterLScreen = toScreenRect ({ meterLTopLeft1x.x, meterLTopLeft1x.y, meterComponentSize1x, meterComponentSize1x });
    const auto meterRScreen = toScreenRect ({ meterRTopLeft1x.x, meterRTopLeft1x.y, meterComponentSize1x, meterComponentSize1x });

    juce::Image diffImage (juce::Image::RGB, width, height, true);

    double ventDiffEnergy = 0.0;
    double meterDiffEnergy = 0.0;
    double outsideDiffEnergy = 0.0;
    int maxVentDiff = 0;
    int maxMeterDiff = 0;

    {
        juce::Image::BitmapData diffWrite (diffImage, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto a = frame1.getPixelAt (x, y);
                const auto b = frame2.getPixelAt (x, y);

                const int diff = std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                                + std::abs (a.getBlue() - b.getBlue()); // 0..765

                const auto amplified = (juce::uint8) juce::jlimit (0, 255, diff * 4);
                diffWrite.setPixelColour (x, y, juce::Colour::fromRGB (amplified, amplified, amplified));

                const bool inVent = ventLScreen.contains (x, y) || ventRScreen.contains (x, y);
                const bool inMeter = meterLScreen.contains (x, y) || meterRScreen.contains (x, y);

                if (inVent)
                {
                    ventDiffEnergy += diff;
                    maxVentDiff = juce::jmax (maxVentDiff, diff);
                }
                else if (inMeter)
                {
                    meterDiffEnergy += diff;
                    maxMeterDiff = juce::jmax (maxMeterDiff, diff);
                }
                else
                {
                    outsideDiffEnergy += diff;
                }
            }
        }
    }

    INFO ("vent diff energy = " << ventDiffEnergy << " (max px diff " << maxVentDiff
                                  << "/765), meter (bulb-glow) diff energy = " << meterDiffEnergy
                                  << " (max px diff " << maxMeterDiff << "/765), outside diff energy = " << outsideDiffEnergy);

    // Both flicker elements must have genuinely, MEANINGFULLY moved - not
    // just a handful of near-zero AA-noise pixels summing to a technically
    // nonzero total. Thresholds set with margin below the near-ceiling
    // deltas the (elapsed1, elapsed2) pairs above were chosen to produce
    // (locally measured: vent max diff 109/765, meter max diff 30/765) -
    // still each a clearly human-visible amount (>=20/765, ~2.6% of
    // full-scale per-pixel colour distance).
    CHECK (maxVentDiff > 40);
    CHECK (maxMeterDiff > 20);

    // ...and nothing else on the plate - preset bar, scale button, rose
    // flourish, org emblem, screws, knobs, toggles - moved at all between
    // the two frames. A small tolerance (rather than a hard ==0.0) absorbs
    // any theoretical software-rasteriser dithering noise without masking
    // a real regression (765 possible per-pixel diff * thousands of pixels
    // dwarfs this bound if anything actually changed).
    CHECK (outsideDiffEnergy < 50.0);

    // Compose: the two frames side by side on top, the amplified diff image
    // below - all three in one PNG for a single-glance visual review.
    constexpr int gap = 10;
    juce::Image composite (juce::Image::RGB, width * 2 + gap * 3, height * 2 + gap * 3, true);
    {
        juce::Graphics g (composite);
        g.fillAll (juce::Colours::black);
        g.drawImageAt (frame1, gap, gap);
        g.drawImageAt (frame2, width + gap * 2, gap);
        g.drawImageAt (diffImage, gap, height + gap * 2);
    }

    juce::PNGImageFormat pngFormat;
    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile ("flicker-diff.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (composite, *stream));
    }
    else
    {
        FAIL ("could not open output stream for flicker-diff.png");
    }
}

// Idle-rest sign-off fix: BOTH VU needles must rest exactly on the -20 tick
// with the plugin idle/no signal - fresh editor open, host-stopped (reset()),
// and true silence during a real processBlock() run. Verifies the actual
// numeric root cause (PluginProcessor's meterGainReductionDb previously
// defaulting to 0.0f, parking the gain-reduction needle on the dial's 0dB
// tick instead of -20 - see PluginProcessor.h's docs) at every one of those
// paths, plus the fresh-constructed editor's own AnalogMeter children before
// this headless test binary's non-existent message loop would ever pump a
// single GUI timer tick (see this file's top-of-file docs on why timers
// never fire here) - i.e. exactly the instant a real host shows the editor
// before its own first ~33ms timer tick.
TEST_CASE ("Both VU meters rest on the -20 tick at every idle path (idle-rest fix)", "[gui]")
{
    using basilica::gui::AnalogMeter;

    const auto restAngle = AnalogMeter::tickAngleDegreesForDb (-20.0f);

    SECTION ("PluginProcessor's own metering getters stay <= -20dB across every idle path")
    {
        SilentiumAudioProcessor processor;

        // Path 1: fresh construction, before prepareToPlay()/processBlock()
        // have ever run - the state a host's editor can observe if it's
        // shown immediately after the processor is created.
        CHECK (processor.getGainReductionDb() <= -20.0f);
        CHECK (processor.getInputLevelDb() <= -20.0f);
        CHECK (AnalogMeter::tickAngleDegreesForDb (processor.getGainReductionDb()) == Catch::Approx (restAngle));
        CHECK (AnalogMeter::tickAngleDegreesForDb (processor.getInputLevelDb()) == Catch::Approx (restAngle));

        // Path 2: prepared, but still before the first processBlock() call -
        // the far more common real-host sequence (prepare, then show the
        // editor, then start the audio callback).
        processor.prepareToPlay (48000.0, 512);
        CHECK (processor.getGainReductionDb() <= -20.0f);
        CHECK (processor.getInputLevelDb() <= -20.0f);

        // Path 3: true silence during a real processBlock() run - the
        // default Range parameter (-60dB, see ParameterLayout.cpp) closes
        // the gate and GateEngine::getCurrentGainDb() converges toward it,
        // well below -20; the input meter reads the actual (silent) signal.
        juce::AudioBuffer<float> silentBuffer (2, 512);
        silentBuffer.clear();
        juce::MidiBuffer midi;

        for (int block = 0; block < 50; ++block) // several blocks, ballistics settle
            processor.processBlock (silentBuffer, midi);

        CHECK (processor.getGainReductionDb() <= -20.0f);
        CHECK (processor.getInputLevelDb() <= -20.0f);

        // Path 4: host-stopped - reset() re-parks both atomics to the idle
        // floor regardless of whatever the engine/meters were doing right
        // before the stop (feed a loud block first, so this genuinely
        // exercises the re-park rather than trivially passing because the
        // atomics were already low).
        juce::AudioBuffer<float> loudBuffer (2, 512);
        for (int ch = 0; ch < loudBuffer.getNumChannels(); ++ch)
        {
            auto* data = loudBuffer.getWritePointer (ch);
            for (int i = 0; i < loudBuffer.getNumSamples(); ++i)
                data[i] = 0.9f;
        }

        for (int block = 0; block < 50; ++block)
            processor.processBlock (loudBuffer, midi);

        // Sanity: the loud signal actually opened the gate/moved the input
        // meter, so reset() below has something real to re-park FROM.
        REQUIRE (processor.getInputLevelDb() > -20.0f);

        processor.reset();
        CHECK (processor.getGainReductionDb() <= -20.0f);
        CHECK (processor.getInputLevelDb() <= -20.0f);
    }

    SECTION ("fresh-constructed editor's own meters read the same resting target, before any GUI timer tick")
    {
        SilentiumAudioProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        SilentiumAudioProcessorEditor editor (processor);
        REQUIRE (editor.getWidth() > 0);
        REQUIRE (editor.getHeight() > 0);

        for (const auto* title : { "Gain Reduction meter", "Input Level meter" })
        {
            auto* meter = findChildByTitle<basilica::gui::AnalogMeter> (editor, title);
            REQUIRE (meter != nullptr);

            // createAccessibilityHandler() directly, not getAccessibilityHandler()
            // (see EditorAccessibilityTests.cpp's top-of-file docs: the latter
            // only returns a handler once the component has a live native
            // window peer, which this headless test binary never has).
            const auto handler = meter->createAccessibilityHandler();
            REQUIRE (handler != nullptr);

            auto* valueInterface = handler->getValueInterface();
            REQUIRE (valueInterface != nullptr);

            const auto valueText = valueInterface->getCurrentValueAsString();
            INFO ("meter '" << title << "' fresh-open accessible value = " << valueText);
            CHECK (valueText.getFloatValue() <= -20.0f);
        }
    }
}

// build/idle-rest-proof.png: both meters cropped to their own component
// bounds, side by side, captured with NEITHER setImmediateDbForPreview() NOR
// any other preview injection - genuinely the fresh-open state a real host
// shows before its first GUI timer tick (see the TEST_CASE above's own docs
// for why that's a faithful stand-in for "no running message loop ever pumps
// a tick here"). Both needles must visibly rest on the dial's -20 tick.
TEST_CASE ("Idle-rest proof: both fresh-open meters render resting on the -20 tick", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    SilentiumAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    auto* grMeter = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Gain Reduction meter");
    auto* inputMeter = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Input Level meter");
    REQUIRE (grMeter != nullptr);
    REQUIRE (inputMeter != nullptr);

    // Deliberately NO setImmediateDbForPreview()/setTargetDb() calls here -
    // this is the true fresh-open state.
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    const auto grCrop = snapshot.getClippedImage (grMeter->getBounds());
    const auto inputCrop = snapshot.getClippedImage (inputMeter->getBounds());
    REQUIRE (grCrop.isValid());
    REQUIRE (inputCrop.isValid());

    constexpr int gap = 10;
    const auto cropWidth = juce::jmax (grCrop.getWidth(), inputCrop.getWidth());
    const auto cropHeight = juce::jmax (grCrop.getHeight(), inputCrop.getHeight());

    juce::Image composite (juce::Image::RGB, cropWidth * 2 + gap * 3, cropHeight + gap * 2, true);
    {
        juce::Graphics g (composite);
        g.fillAll (juce::Colours::black);
        g.drawImageAt (grCrop, gap, gap);
        g.drawImageAt (inputCrop, cropWidth + gap * 2, gap);
    }

    juce::PNGImageFormat pngFormat;
    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile ("idle-rest-proof.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (composite, *stream));
    }
    else
    {
        FAIL ("could not open output stream for idle-rest-proof.png");
    }
}
