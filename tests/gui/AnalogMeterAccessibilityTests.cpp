#include "gui/AnalogMeter.h"

#include <catch2/catch_test_macros.hpp>

// A-07 fix (M3 a11y review): AnalogMeter must expose an on-demand, read-only
// accessibility value (the current ballistic-smoothed dB reading) rather
// than being a pure vision-only display with no value interface at all.
// Deliberately does NOT test "does it announce automatically on every
// repaint" - the whole point of the fix is that it must NOT do that (see
// AnalogMeter.cpp's MeterValueInterface docs) - only that the value is
// queryable on demand, in the right units, and reports itself read-only.
TEST_CASE ("AnalogMeter exposes a read-only, unit-suffixed accessible value", "[gui][a11y]")
{
    basilica::gui::AnalogMeter::Assets assets; // deliberately default/invalid images - fine, this test never calls paint()
    basilica::gui::AnalogMeter meter (assets, "Gain Reduction meter");

    // createAccessibilityHandler() (not getAccessibilityHandler()) - the
    // latter only returns non-null once the component has a live native
    // window peer (JUCE 8.0.14 juce_Component.cpp:3323-3326), which this
    // headless, no-message-loop test binary never has. See
    // EditorAccessibilityTests.cpp's top-of-file docs for the full rationale.
    const auto handler = meter.createAccessibilityHandler();
    REQUIRE (handler != nullptr);

    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    CHECK (valueInterface->isReadOnly());

    const auto valueText = valueInterface->getCurrentValueAsString();
    INFO ("AnalogMeter accessible value = \"" << valueText.toStdString() << "\"");
    CHECK (valueText.endsWith ("dB"));
}
