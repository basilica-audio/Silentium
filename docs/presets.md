# Factory presets

Ten factory presets ship with Silentium v0.4.0, embedded via BinaryData from
`presets/factory/*.json`. All are sourced from `docs/design-brief.md`'s
"Factory Presets" section - see that document's own Honesty section for what
these numbers are and aren't calibrated against (research/manual/forum-
derived, not measured hardware).

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | The v0.2.0 out-of-the-box parameter layout defaults (Threshold -40 dB, Attack 1 ms, Hold 20 ms, Release 80 ms, Range -60 dB, SC HPF 80 Hz, SC LPF 16 kHz/off) - the certified starting point and this plugin's default-resolution target (see the M2 default-resolution order in `basilica-audio/nave`'s `docs/preset-system-notes.md`, the pilot's replication recipe). |
| **Surgical Mute** | Guitar | Maximum inter-note silence for tightly palm-muted rhythm parts (Range -80 dB, fast 0.5 ms attack, 15 ms hold) - the "surgical/tight" camp from research (Nail The Mix's "-∞ dB or its maximum setting"). |
| **Natural Decay** | Guitar | Partial-reduction gating (Range -16 dB, 6 dB knee, slower 150 ms release) that avoids the audibly-gated pump on sustained chords - the "-12 to -20 dB partial reduction sounds more natural" camp. |
| **Pick Attack Focus** | Guitar | Same tight ballistics as Surgical Mute, but SC HPF/LPF narrowed to 1.5-5 kHz to key detection specifically off the guitar pick-attack transient band cited by research, less sensitive to sustained low-mid buzz/hum. |
| **DI-Keyed Workflow** | Guitar | Moderate, v0.1/v0.2-shared-default ballistics, documented for use with a clean DI feeding Silentium's External Sidechain input bus (bus routing is a host-level setting outside APVTS, so this preset ships the ballistics side of the "gold standard" DI-keyed workflow; enable External Sidechain in your host's routing matrix separately). |
| **Ambient Sustain** | Guitar | Long hold/release (200/400 ms) plus partial Range (-24 dB) and a wide 12 dB knee for a soft, program-material-following close on clean/ambient high-headroom parts where a hard gate would sound obviously gated. |
| **Chug Lock** | Guitar | Fast palm-mute 8th/16th-note chugging at high tempo: 0 ms Attack (the new v0.2.0 floor) so no audible ramp reads as smear, tight 10/40 ms hold/release, full -80 dB Range. |
| **Duck Under Lead** | Guitar | Demonstrates Duck mode as a rhythm-ducks-under-lead effect (Duck on, moderate -10 dB Range, slow 200 ms release) rather than pure noise reduction, keeping the M1 Duck feature discoverable. |
| **Listen Check** | Guitar | Diagnostic/onboarding preset (Listen on, everything else at Default): loading it immediately auditions what the detection path hears, answering "why isn't my gate opening" without reading the manual. |
| **Expander Glue** | Guitar | *(v0.4.0)* Downward expansion rather than gating: Ratio 2.5:1 with a shallow -18 dB floor, RMS detection, linear release and Smooth Open. Reduces amp noise between phrases in proportion to how quiet things get, so nothing is ever switched off and no one can tell a gate is in the chain. The starting point when a gate sounds too obviously like a gate. |


## v0.4.0 preset changes

Factory presets are not silently re-voiced. This release changes exactly one,
deliberately, and both the change and the one that was reverted are recorded
here.

- **Chug Lock gains Smooth Open.** It is a 0 ms-attack preset, which is
  precisely the case Smooth Open exists for: the opening ramp now fits inside
  the preset's 5 ms lookahead window instead of stepping. Nothing else about
  it changed.
- **Surgical Mute was deliberately left alone**, although the plan had been to
  give it Smooth Open too. Smooth Open shapes the *closing* edge as well as
  the opening one - the moving-max window holds the open target for up to half
  the Lookahead time after the signal falls away, and the box cascade then
  spreads the close over the rest of it. On a preset whose entire purpose is
  the tightest achievable inter-note silence that is the wrong trade: with it
  enabled, Surgical Mute measurably stopped being quieter between notes than
  Natural Decay, which is one of the guarantees `docs/design-brief.md` makes
  and `tests/DesignBriefTests.cpp` asserts. If you want that preset with a
  softer opening, turn Smooth Open on yourself and lower Lookahead to taste.

The other seven presets are byte-identical apart from their `pluginVersion`
stamp, and `tests/PresetManagerTests.cpp` checks that each still *renders*
identically to a capture taken from a v0.3.0 build - a stronger statement than
"the file was not edited", since it also covers every engine change this
release made.
