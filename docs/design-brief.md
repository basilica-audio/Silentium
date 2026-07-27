# Silentium — Design Brief v2 (draft)

Target version: **v0.2.0**. Pre-1.0: breaking parameter changes are allowed.
State migration policy: **tolerant import** — a v0.1.0 state tree missing v2
parameters loads with v2 defaults filled in for the new IDs; a v0.1.0 state
tree's existing parameter IDs are preserved as-is (no silent value
remapping) since none of the changes below rename or rescale an existing ID.

> Template note: the brief was asked to follow the structure of a sibling
> `miserere-design-brief-v2.md`, but no such file exists on this machine (see
> the provenance note at the top of `silentium-research-notes.md`). This
> document instead follows the six-section shape given directly in the task
> description: why v1 falls short → topology → module specs with sourced
> defaults → test guarantees → honesty → versioning.

## 1. Why v1 falls short

Silentium v1 (M1, v0.1.0) is a correctly engineered hysteresis noise gate:
real-time-safe, well-tested (~42 Catch2 tests), with a sound state machine
(open threshold / close threshold / hold countdown) and a defensible set of
M1 additions (knee, duck, listen, external sidechain). But measured against
the reference class — ISP Decimator (hardware pedal), dbx 166-series (rack
hardware), FabFilter Pro-G (software) — it is an **"engineered core"**: the
architecture is right, but several specific behaviours are generic where the
category is opinionated. Concretely (full sourcing in
`silentium-research-notes.md` §2-3):

1. **The gain computer has no program-dependence.** v1's attack/release is a
   fixed dB/sample ramp regardless of overshoot or note duration. Both
   ISP ("Time Vector Integration" / Integration Release Window — eliminates
   modulation of sustained notes) and dbx ("AutoDynamic attack and release
   circuitry that automatically adapts to program material") make adaptive
   release their headline feature; FabFilter Pro-G's manual independently
   states attack and release "are program dependent." A fixed-slope ramp is
   the single biggest gap between v1 and what the category actually sounds
   like on real playing (staccato palm mutes vs. sustained legato notes).
2. **The sidechain path only rejects noise (HPF), it never emphasizes the
   signal the category cares about.** Practitioner consensus (Nail The Mix)
   names 2-5 kHz — the pick-attack transient band — as the frequency range a
   metal-guitar gate's key signal should be focused on. v1's SC HPF (20-500
   Hz) removes hum/rumble but has no mechanism to narrow the top end.
3. **Range's single default straddles two documented use cases instead of
   picking one.** The reference workflow bifurcates into "surgical/tight"
   (max attenuation, toward -∞ dB) and "natural" (-12 to -20 dB partial
   reduction, avoiding an audibly gated feel) camps. v1's -60 dB default is
   neither — a compromise that serves no one perfectly. This is best resolved
   by presets (M2), not a changed single default.
4. **No curve differentiation by use case.** FabFilter ships distinct
   "Guitar"/"Vocal"/"Clean"/"Classic" style curves; v1 has one hysteresis
   value and one knee shape for everything. (v1's fixed 3 dB hysteresis
   itself is independently *validated* by the reference class, not a gap —
   see honesty section.)
5. **Attack cannot reach 0 ms even with lookahead active**, though the
   category norm (Nail The Mix) explicitly allows 0 ms attack "if your gate
   allows lookahead."
6. **The external sidechain / DI-key workflow, already shipped in M1, is
   under-surfaced.** Two independent sources (Nail The Mix, Fortin Amps)
   describe DI-keyed gating as the *only* physically correct way to gate a
   high-gain signal without chatter or cut-off sustain — not a niche feature.
   v2 should make this the headline workflow for at least one preset and in
   parameter grouping/naming, not just a background bus toggle.

## 2. Topology (v2)

```
                                   +-- SC HPF (20-500 Hz) --+
                                   |                        |
                    +-- SC filter band -- SC LPF (1-16 kHz, off by default) --+
                    |                                                        |
                    |            stereo-linked max|.| --> peak envelope -----+
                    |                                          |
Input --> Lookahead |         hysteresis comparator + hold timer + knee blend
 (or Sidechain      |                    |                      |
  bus, if enabled)  |     adaptive attack/release gain ramp (dB domain,
    |               |     program-dependent: ramp rate scales with how far
    |               |     the target is from the current gain, not a fixed
    |               |     dB/sample slope)
    +---------------+------------------------------------------------> x (gain), or Listen output --> Output
```

Everything from v1's topology is retained (SC-only filtering, stereo-linked
detection, lookahead-delayed main path, knee/duck/listen, external sidechain
bus). Two additions:

- **SC LPF** — a second, independent sidechain-only filter stage, in series
  after the existing SC HPF, so the detection path can be narrowed toward the
  documented 2-5 kHz pick-attack band instead of just having its bottom end
  rejected. Off by default (full range above the HPF corner) so v1 behaviour
  is reproducible exactly by leaving it at its default.
- **Program-dependent gain ramp** — attack/release ramp rate is no longer a
  fixed `(0 - RangeDb) / timeMs` slope; the rate is derived per-transition
  from how far the current gain sits from the new target (see §3, Attack/
  Release module spec) so a small overshoot near threshold ramps
  proportionally faster/gentler than a full-scale transition, matching the
  "adapts to program material" behaviour documented for both hardware
  references. This changes the *shape* of the ramp, not the user-facing
  Attack/Release units (still ms) or their meaning ("time to fully open/close
  a full-scale transition") — a v0.1.0 session's Attack/Release values still
  mean approximately the same thing at full-scale transitions, only partial
  transitions now scale correctly instead of taking the same wall-clock time
  as a full transition would.

## 3. Module specs — parameters, ranges, sourced defaults

Generic descriptors only (no brand/person names) — matches v1's existing
naming convention.

### Threshold
- Range -80 to 0 dB, default **-40 dB** (unchanged from v1).
- Reasoning: v1's default already matches practitioner guidance ("above the
  noise floor but below the quietest signal you want to keep" — a by-ear
  control, not a number to source precisely); ISP's manual explicitly frames
  Threshold as a by-ear ritual control too, confirming it should stay
  user-set rather than algorithmically defaulted tighter.

### Hysteresis (close threshold offset)
- Fixed at **3 dB below Threshold**, still not user-exposed. Unchanged.
- Reasoning: FabFilter's Style presets are described as containing "built-in,
  carefully tuned hysteresis to prevent flutter" — i.e., the reference class
  also treats hysteresis amount as a tuned constant, not a raw user knob.
  v1's choice is independently validated; no change.

### Attack
- Range widened to **0 - 50 ms** (floor lowered from 0.1 ms to 0 ms),
  default **1 ms** (unchanged).
- Reasoning: Nail The Mix — "0.1ms to 1ms, sometimes even 0ms if your gate
  allows lookahead." v1 has lookahead but couldn't reach 0 ms; this closes
  that gap. 1 ms default unchanged since it already sits inside the
  documented practitioner range.
- **Program-dependent ramp** (new): ramp rate for a given Attack/Release
  setting is computed from the *actual* dB distance to the new target this
  transition, not from the full Range span, so `Attack = 1 ms` means "a
  full-scale transition takes ~1 ms," while a small overshoot near Threshold
  ramps in proportionally less wall-clock time rather than taking the same
  1 ms regardless of how small the gain change is. This directly encodes the
  "program dependent" behaviour cited for both ISP and dbx without adding a
  new user-facing parameter.

### Hold
- Range **0 - 250 ms** (ceiling lowered from 500 ms), default **20 ms**
  (unchanged).
- Reasoning: Nail The Mix cites 10-50 ms as the practical band; FabFilter
  Pro-G's own ceiling is 250 ms ("adjustable hold time is up to 250 ms").
  v1's 500 ms ceiling had no source and no practical use case found in
  research; halving it to match the best-documented software reference
  removes unsourced headroom without touching the well-supported 20 ms
  default.

### Release
- Range 5 - 500 ms, default **80 ms** (unchanged).
- **Program-dependent ramp** (new, same mechanism as Attack): a full close
  from unity to Range floor takes ~80 ms at default, but a signal that only
  dipped slightly under the close threshold before re-opening ramps back
  proportionally faster, matching BOSS NS-2 Decay guidance ("slower DECAY...
  avoid the end of long notes being chopped off") and ISP's Time Vector
  Integration goal (no audible modulation on sustained notes) without
  requiring a second "sustain-aware" toggle.

### Range
- Range unchanged: -80 to 0 dB, default **-60 dB**.
- Reasoning: kept as-is because no single number in the research resolves the
  "surgical vs. natural" split cleanly (§1.3) — solved via presets instead
  (§5) rather than moving the default toward either camp and under-serving
  the other.

### Lookahead
- Range unchanged: 0 - 20 ms, default **5 ms**.
- Reasoning: FabFilter Pro-G's lookahead ceiling is 10 ms; v1's 20 ms ceiling
  is double that with no found justification, but no evidence it's harmful
  either (it's a ceiling, not a default) — left unchanged pending real
  measurement (see honesty section) rather than cut on research-alone
  confidence.

### SC HPF
- Range unchanged: 20 - 500 Hz, default **80 Hz**.
- Reasoning: matches the "100 Hz to eliminate 60/50 Hz hum while leaving
  low-mids intact" guidance closely enough (80 Hz sits just below that);
  unchanged.

### SC LPF (new)
- Range **1,000 - 16,000 Hz**, default **16,000 Hz (effectively off)**.
- Reasoning: Nail The Mix names 2-5 kHz as the pick-attack band a gate's key
  signal should focus on. A full band-pass (HPF *and* LPF, both SC-only) lets
  an engineer narrow onto that band without a dedicated "pick-attack" preset
  parameter; defaulting to fully open (16 kHz, i.e. no attenuation below
  Nyquist at typical sample rates) preserves exact v1 behaviour for anyone
  who doesn't touch it, satisfying the tolerant-import requirement.

### Knee
- Range unchanged: 0 - 24 dB, default **0 dB**.
- Reasoning: FabFilter documents a knee control with no published numeric
  range; v1's 0-24 dB and hard-knee-at-zero default already match the
  "0 dB = classic hard-knee snap" framing found nowhere contradicted in
  research. Unchanged.

### Duck / Listen / External sidechain
- Unchanged from v1. Reasoning: Duck and Listen have no direct reference-class
  precedent to source against (Duck is a Silentium-specific mode-invert;
  Listen matches the generic "key monitor" feature common across the whole
  software-gate category, e.g. Pro-G's own listen-style audition, so no
  numeric change applies). External sidechain is validated as the
  category-correct workflow by two independent sources (§1.6) — no DSP
  change needed, only preset/naming emphasis (§5).

## 4. Test guarantees (Catch2, v0.2.0)

All existing v1 test categories are retained (null/reference, hysteresis,
latency, state round-trip, sample-rate sweep 44.1-192 kHz, mono/stereo bus
configs, long-run NaN/Inf stability, bus-layout negotiation). New/changed
guarantees for v0.2.0:

1. **SC LPF null test**: SC LPF at its default (16 kHz) must reproduce the
   exact v1 null-test result (Range=0dB/Knee=0/Duck=off collapsing to a pure
   delay) bit-for-bit, proving the new filter stage is inert unless engaged.
2. **SC band-pass curve test**: with SC HPF=80 Hz and SC LPF=5 kHz, feed
   swept-sine or multi-tone probes through the detection path in isolation
   (bypassing the gain computer) and assert the measured magnitude response
   has passband ripple within a defined tolerance and >12 dB attenuation by
   one octave outside each corner — a real measured-curve proof, not just
   "the filter exists."
3. **Program-dependent ramp proof**: for a fixed Attack/Release setting,
   assert that (a) a full-scale transition (Range floor <-> unity) completes
   in the time implied by the ms value (within a defined tolerance, as v1's
   existing ballistics tests likely already do), AND (b) a partial-scale
   transition (e.g. 6 dB overshoot near Threshold with wide Knee) completes
   in proportionally less time than the full-scale case — the two cases must
   differ, or the "program-dependent" claim is untested. This is the
   single most important new test: it is what proves v2 actually changed
   gain-computer behaviour rather than just adding an inert filter.
4. **Attack floor test**: Attack = 0 ms must produce an audible/measurable
   instantaneous (within one sample, given lookahead) gain jump to unity on
   threshold crossing, distinct from Attack = 0.1 ms's still-visible ramp —
   proves the floor actually moved, not just that the parameter accepts 0.
5. **Hold range test**: Hold parameter clamps correctly at the new 250 ms
   ceiling; a state tree imported from v0.1.0 with Hold > 250 ms (impossible
   in practice since v1's own ceiling was 500 ms, but a hand-edited/future
   state could set it) clamps on load rather than asserting or silently
   wrapping.
6. **Tolerant state import test**: a serialized v0.1.0 `AudioProcessorValueTreeState`
   XML (all current v0.1.0 parameter IDs, none of the v2-new ones) loads
   into v0.2.0 without error, with SC LPF and any other new parameter IDs
   populated at their v2 defaults and all pre-existing IDs' values preserved
   exactly.
7. **Spectral proof for "surgical" vs "natural" preset pair** (see §5):
   process an identical staccato-riff test signal through both presets and
   assert the measured inter-note silence duration/depth differs
   measurably between them (surgical: closer to full Range floor and longer
   silence; natural: shallower floor), so the preset pair is provably
   distinct, not just differently labelled.

## 5. Factory Presets (proposed, for the M2 preset system)

Generic names, no brand/person references. Intent-first naming, matching the
"surgical vs. natural" and "DI-keyed workflow" findings directly.

1. **Surgical Mute** — Threshold -45 dB, Attack 0.5 ms, Hold 15 ms, Release
   60 ms, Range -80 dB (near-full mute), Knee 0 dB, SC HPF 100 Hz, SC LPF off.
   Intent: maximum inter-note silence for tightly palm-muted rhythm parts,
   the "surgical/tight" camp from research.
2. **Natural Decay** — Threshold -38 dB, Attack 2 ms, Hold 30 ms, Release
   150 ms, Range -16 dB (partial reduction), Knee 6 dB, SC HPF 80 Hz.
   Intent: the "-12 to -20 dB partial reduction sounds more natural" camp —
   avoids the audibly-gated pump on sustained chords.
3. **Pick Attack Focus** — same core ballistics as Surgical Mute but SC HPF
   1.5 kHz, SC LPF 5 kHz (narrow band-pass onto the 2-5 kHz pick-transient
   band cited by research). Intent: detection keyed specifically off
   attack transients, less sensitive to sustained low-mid buzz/hum.
4. **DI-Keyed Workflow** — moderate ballistics (Attack 1 ms, Hold 20 ms,
   Release 80 ms — v1/v2 shared defaults), Range -60 dB, but documented/
   labelled explicitly for use with External Sidechain enabled and a clean DI
   feeding the sidechain bus. Intent: surfaces the "gold standard" workflow
   (§1.6) as a first-class preset rather than leaving external sidechain
   undiscovered.
5. **Ambient Sustain** — Threshold -50 dB, Attack 5 ms, Hold 200 ms, Release
   400 ms, Range -24 dB, Knee 12 dB. Intent: clean/ambient high-headroom
   parts where a hard gate would sound obviously gated; long hold/release
   plus partial range and wide knee for a soft, program-material-following
   close.
6. **Chug Lock** — Threshold -42 dB, Attack 0 ms, Hold 10 ms, Release 40 ms,
   Range -80 dB, Knee 0 dB. Intent: fast palm-mute 8th/16th-note chugging at
   high tempo where any audible ramp reads as smear; exercises the new 0 ms
   Attack floor directly.
7. **Duck Under Lead** — Duck enabled, Threshold -20 dB, Attack 10 ms, Hold
   0 ms, Release 200 ms, Range -10 dB. Intent: demonstrates Duck mode as a
   rhythm-ducks-under-lead effect rather than pure noise reduction — keeps
   the existing Duck feature discoverable via presets.
8. **Listen Check** — Listen enabled, all other parameters at v0.2.0
   defaults. Intent: not a mixing preset but a diagnostic/onboarding preset —
   loading it immediately demonstrates what the detection path hears,
   answering "why isn't my gate opening" without reading the manual.

## 6. Honesty section

- **All defaults and ranges above are research-derived from manuals, help
  documentation, and practitioner articles — none are from direct hardware
  measurement, hardware ownership, or listening comparison against ISP/dbx/
  FabFilter units.** Where a source gave a number ("2-5 kHz," "10-50 ms,"
  "-12 to -20 dB," FabFilter's published 10 ms/250 ms ceilings), it is cited
  inline in `silentium-research-notes.md`; where no number existed
  (Threshold, Hysteresis, Knee, SC HPF corner, Duck/Listen), v1's existing
  value was kept unchanged and the reasoning states explicitly that it is
  unchanged rather than newly sourced.
- **The "program-dependent ramp" is the riskiest change in this brief.** Both
  hardware references gate this behind patented/branded processing (ISP's
  "Time Vector Integration," dbx's "AutoDynamic") whose exact algorithms are
  not public; this brief proposes a plausible, testable mechanism (ramp rate
  scaled by actual transition distance rather than full-span distance) that
  is *inspired by* the documented goal ("no modulation on sustained notes,"
  "adapts to program material") but is not a reproduction of either vendor's
  proprietary algorithm. It should be validated against real playing during
  implementation, not just against the Catch2 curve tests in §4.
- **The 20 ms Lookahead ceiling and the 20-500 Hz SC HPF range are left
  unchanged on absence-of-evidence, not confirmed-correct evidence** — no
  source contradicted them, but none actively validated the ceiling numbers
  either (FabFilter's ceiling, at 10 ms, is lower; this brief did not find
  grounds strong enough to force a breaking range cut).
- **No hardware unit was purchased, borrowed, or measured for this brief.**
  All ISP/dbx claims are manual/product-page text, not independently verified
  circuit behaviour. If Yves or a collaborator has hands-on time with a
  Decimator or a 166-series unit, that should supersede this brief's
  inferences before v0.2.0 ships, particularly for the program-dependent
  ramp shape.
- **The continuous-ratio expansion mode found in dbx's 166 series (§3, gap 7
  in the research notes) is explicitly out of scope for v0.2.0** — it would
  be a genuine topology addition (partial gain reduction proportional to
  distance under threshold, not just a fixed Range floor), not a parameter
  tweak, and deserves its own design pass rather than being folded into this
  brief opportunistically.

## 7. Versioning

- Target: **v0.2.0**.
- Pre-1.0 breaking changes allowed and used here: Attack floor 0.1→0 ms, Hold
  ceiling 500→250 ms, new SC LPF parameter, changed (non-parameter-visible)
  internal ramp-rate computation for Attack/Release.
- **State migration = tolerant import**: a v0.1.0 `AudioProcessorValueTreeState`
  loads into v0.2.0 with all pre-existing parameter IDs preserved exactly and
  any new v0.2.0-only IDs (currently: SC LPF) filled with their v0.2.0
  defaults. No parameter ID is renamed or rescaled by this brief, so no value
  remapping logic is required beyond JUCE's own tolerant-XML-attribute
  handling — this should be covered by test guarantee #6 in §4.
- No GUI changes are in scope here (M3). No preset *system* implementation is
  in scope here either — §5 is content for M2's preset system to consume,
  not a spec for how presets are stored/browsed.

---

# v0.4.0 addendum — expander, click-free opening, live reconfiguration

This section records what v0.4.0 added on top of the v0.2.0 brief above, and —
more importantly — the two things it deliberately did **not** ship.

## What shipped

Six parameters (`ratio`, `hysteresis`, `detector`, `scSlope`, `smoothOpen`,
`releaseShape`), live Lookahead, and gain-reduction telemetry. The algorithms
and their measured behaviour are documented in `CHANGELOG.md`; the topology and
the design decisions behind it are in `docs/architecture.md`; the user-facing
descriptions are in `docs/manual.md`.

The governing constraint was that **nothing about an existing session may
change**. Every new parameter defaults to the value that reproduces v0.3.x, and
each legacy behaviour is reached through a literal code branch rather than by
re-deriving it as a special case of the new law — at Ratio's maximum the gain
computer runs the old binary path verbatim, and every crossfade weight parked
at its default multiplies the new path by exactly zero.

That claim is *verified*, not asserted. `tests/data/` holds renders captured
from a build of the v0.3.0 engine (commit `e9bceb9`), and the neutrality tests
compare against those artifacts rather than against a second render from the
same binary — which would move both sides of the comparison together and stay
green through a regression. See `tests/data/README.md`.

## Honesty section (continued)

The v0.2.0 honesty section above applies unchanged: none of this is calibrated
against measured hardware. Two additions specific to this release:

- **The expander law is textbook, not proprietary.** The hard/soft-knee
  downward-expander equations are the standard formulation from the dynamic
  range compression literature (Giannoulis, Massberg & Reiss). There is nothing
  novel in them and nothing reverse-engineered.
- **The RMS detector is not universally steadier than the peak detector**, and
  the tests say so. On a sustained low-frequency tone its 5 ms window does not
  smooth the mean-square ripple at twice the fundamental, while the peak
  follower's 15 ms release comfortably bridges the period — so at 70 Hz, peak
  is the steadier of the two. RMS's real advantage is material with a high
  crest factor, where it ignores spikes that carry almost no energy. The
  documentation and the test suite both describe it that way rather than
  repeating the more flattering general claim.

## Deliberately not shipped

### Program-adaptive auto-release

A dual-envelope, decay-tracking auto-release was specified for this release. It
is not implemented, and no parameter ID was reserved for it.

US 11,641,183 (granted 2023, presumed in force) claims an
integration-release-window auto-release mechanism. The specified design differs
from a naive digitisation of that mechanism, but **nobody on this project has
performed a claim-by-claim comparison**, so no non-infringement conclusion is
recorded here — and none should be inferred from this document.

It is worth stating plainly why an earlier draft's reasoning was wrong: it
argued that implementing from the published patent text was safe because that
text is a public disclosure. Public disclosure and clean-room provenance are
*copyright* concepts. They have no bearing on patent infringement, which is
what independently building an in-force patent's claimed mechanism is.

Shipping this feature requires one of the following to be recorded first:

1. a design-around verified against the independent claims by qualified **US
   patent counsel** (note that the project's standing legal-review resource
   covers DE/EU civil and data-protection law, not US patent analysis — this
   has to be routed externally);
2. an explicit, written acceptance of the risk; or
3. replacement of the mechanism with documented pre-existing program-dependent
   release art — classic dual-time-constant release has decades of hardware
   precedent — together with removal of every reference to the patent from
   code, documentation and marketing.

Shipping a parameter that does nothing would have been worse than shipping
nothing: parameter IDs are a frozen contract, and a dead control in a saved
session is permanent. The feature can be added cleanly later.

*This is a defensive engineering note, not legal advice.*

### The Surgical Mute re-voicing

Planned, implemented, measured, reverted. Smooth Open shapes the closing edge
as well as the opening one, and on the preset whose entire purpose is the
tightest achievable inter-note silence that cost it the "quieter between notes
than Natural Decay" guarantee this brief makes in §4. The measurement is in
`tests/DesignBriefTests.cpp`; the rationale is in `docs/presets.md`.
