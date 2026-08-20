<p align="center"><img src="assets/icon.png" alt="Silentium icon" width="120"/></p>

# Silentium user manual

*Silence between the storms — a tight lookahead noise gate for palm-muted rhythm.*

## What it is

Silentium is a noise gate purpose-built for high-gain guitar in a heavy-music
mix: the moment between palm-muted chugs where amp hiss, pedalboard hum, and
buzzing strings would otherwise be audible. It is not a general-purpose dynamics
gate borrowed from a mix bus — every default and every internal ballistic is
tuned for the specific problem of "silence a loud, distorted guitar the instant
the player stops picking, without clipping the leading edge of the next pick
attack."

## Where it sits in a heavy production chain

Silentium is a **detection-and-dynamics** stage, not a tone-shaping one. Put it:

- **Before** any drive/distortion/amp-sim plugin (`crypta` and friends in
  this suite) if you want to gate the *clean* signal, which avoids gating
  artifacts being amplified/distorted along with everything else, and lets the
  gate's sidechain high-pass filter see an undistorted signal to key from.
- **After** the amp/cab stage if the noise you're fighting is specifically
  introduced by that stage (amp hiss, cab-sim noise floor) rather than present
  in the DI signal.
- **Before** any time-based effects (reverb, delay) in the chain, so the gate
  closes on the dry guitar and the tail of a reverb/delay doesn't get chopped
  off mid-decay. If you need a gated reverb tail specifically, that is a
  deliberate, different use of a gate than this one.
- In a **layered rhythm-guitar mix** — a heavy-music wall-of-guitars — put
  one instance per DI/amp-sim track rather than gating the whole bus: each
  performance has its own pick-attack timing, and per-track lookahead keeps
  every layer's attacks tight and simultaneous.

For a synced "duck the rhythm guitars under the lead" effect, or a
sidechain-triggered rhythmic gate keyed off a kick or a click track, see
[Duck mode](#duck-mode-ducking-instead-of-gating) and
[external sidechain input](#external-sidechain-input) below — the same engine
covers both use cases.

## Signal flow

```
                    +-- SC HPF (20-500 Hz) --> SC LPF (1-16 kHz) --> stereo-linked max|.| --> peak envelope follower --+
                    |                                                                                                  |
Input --> Lookahead |                                     hysteresis comparator + hold timer + knee blend <-----------+
 (or Sidechain      |                                                    |
  bus, if enabled)  |                        program-dependent attack/release gain ramp (dB domain)
    |               |                                                    |
    +---------------+------------------------------------------------> x (gain), or Listen output -> Output
```

1. A **copy** of the input (or, if you've enabled the external sidechain input
   and your host has routed something into it, that sidechain signal instead)
   is high-passed by **SC HPF** so low-frequency hum/rumble can never falsely
   hold the gate open, then low-passed by **SC LPF** (v0.2.0) so it can
   optionally be narrowed toward the pick-attack transient band instead of
   only having its bottom end rejected. This filtered copy is only ever used
   to *decide* the gain; it never reaches the output directly, unless you
   enable **Listen**.
2. All channels of that filtered copy are combined per-sample via
   `max(|channel|)` (stereo-linked), so a signal panned hard to one side alone
   can still open the gate, and the gate's gain, applied identically to every
   channel, never shifts the stereo image.
3. That mono signal feeds a fast internal peak envelope follower (fixed
   ballistics, not user-exposed) which produces the level the gate reacts to.
4. A **hysteresis comparator** with two thresholds (**Threshold**, and a fixed
   3 dB below it) decides whether the gate is logically open or closed, and a
   **Hold** timer keeps it open across brief dips between transients so
   consecutive palm-muted chugs don't chatter the gate.
5. **Knee** optionally softens the target gain into a smooth blend across a
   band centred on Threshold, instead of an instant on/off snap — Hold still
   guarantees a fully open target throughout its own duration regardless of
   Knee.
6. **Duck**, if enabled, inverts that target: attenuate above Threshold instead
   of opening above it, turning the same engine into a ducker.
7. The result is smoothed into an actual per-sample gain by the **Attack**/
   **Release** ramp (dB domain, program-dependent as of v0.2.0 — a small
   excursion near Threshold ramps in proportionally less time than a full
   Range-floor-to-unity swing, rather than always taking the same wall-clock
   time regardless of how far the gain actually needs to move), then applied
   to the **main** signal — which has meanwhile been delayed by **Lookahead**
   so the gain can start rising just before a transient's leading edge
   actually arrives, avoiding an audible "chirp" on fast picking. Lookahead
   is reported to the host as this plugin's total latency, so plugin-delay
   compensation keeps it phase-aligned with everything else in your session.
8. If **Listen** is enabled, all of the above still runs (so metering/timing
   stays consistent), but the output is the sidechain-filtered detection
   signal itself (step 1, post SC HPF/SC LPF) instead of the gated main
   signal — for auditioning exactly what the detector hears while you dial
   in SC HPF/SC LPF and Threshold.

### Reported latency

Lookahead is Silentium's only source of reported latency. On `prepareToPlay()`
the plugin reports `Lookahead × sample rate`, rounded to samples, via
`setLatencySamples()` — at Lookahead 0 ms that is exactly 0 samples, and
nothing else in the signal path adds any.

*(v0.4.0)* Moving Lookahead while audio is running no longer waits for the
host to re-prepare the plugin: the delay itself crossfades to its new length
immediately (a 10 ms equal-power blend between the old and new tap), but the
*reported* number the host uses for delay compensation only catches up a
moment later, on the next tick of a 25 ms poll — the audio thread never calls
`setLatencySamples()` directly, since that call isn't safe to make from
there. In practice this means a host that keeps its message loop running
picks up the new latency within about 25 ms of the knob move, but a host
that doesn't pump that loop (e.g. mid-bounce) never learns of the change, so
moving Lookahead during a bounce/render is not a good idea.

Turning **Smooth Open** on adds no reported latency at all: with it engaged
or bypassed, the host sees the identical figure at Lookahead 0, 2.5, 5 and
20 ms — the opening ramp is shaped entirely inside the lookahead window
that's already being paid for.

See [`docs/architecture.md`](architecture.md) for the full implementation-level
breakdown (state machine details, real-time-safety notes, the `GateEngine`
class this describes).

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Threshold** | -80 to 0 | -40 | dB | The level the (sidechain-filtered) envelope must reach to open the gate. Lower it to catch quieter pick attacks; raise it to ignore more of the amp's noise floor. The gate's close threshold sits **Hysteresis** dB below this (3 dB by default), so a signal hovering right at Threshold can never chatter the gate open/closed. |
| **Attack** | 0 to 50 | 1 | ms | Time to ramp from the Range floor up to unity once the envelope opens the gate. As of v0.2.0 the floor is 0 ms (down from 0.1 ms) - with Lookahead engaged, 0 ms gives a genuinely instantaneous jump to unity on threshold crossing, for the most percussive picking. Slower values give a more natural swell on sustained chords. The ramp itself is program-dependent (v0.2.0) - a partial excursion converges proportionally faster than a full one; see "How the ramp actually behaves" below. |
| **Hold** | 0 to 250 | 20 | ms | Minimum time the gate stays open once opened, continuously retriggered while the envelope stays above the close threshold. This is what keeps the gate open across the brief silences *between* consecutive palm-muted chugs of a fast rhythm part — set it to roughly the gap between your fastest picking subdivisions. (The ceiling was lowered from 500 ms to 250 ms in v0.2.0, matching the practical range documented for this category of plugin; a v0.1.0 session's Hold value is preserved exactly unless it happened to be set above 250 ms, in which case it now clamps to 250 ms.) |
| **Release** | 5 to 500 | 80 | ms | Time to ramp back down to the Range floor once Hold has fully elapsed. Fast values are tighter/more percussive; slower values let a chord's natural decay breathe a little before the gate closes. Program-dependent as of v0.2.0, same as Attack. |
| **Range** | -80 to 0 | -60 | dB | Floor attenuation applied while the gate is closed. `0 dB` disables gating entirely (an always-open passthrough) — useful as an A/B reference. Values around -40 to -60 dB usually silence amp noise convincingly without sounding like a hard mute; very deep values (-80 dB) are essentially silence. |
| **Lookahead** | 0 to 20 | 5 | ms | Delays the main signal so the gate's gain can start rising just before a transient's leading edge arrives, avoiding an audible attack chirp even with a very fast Attack. Reported to the host as this plugin's total latency (plugin-delay compensation handles the rest automatically). *(v0.4.0)* Changing it now takes effect **immediately**, mid-playback: the delay crossfades to its new length over 10 ms and the new latency is reported to your host a moment later. Before v0.4.0 the knob did nothing until the host happened to re-prepare the plugin. |
| **SC HPF** | 20 to 500 | 80 | Hz | High-pass filter applied *only* to the detection path (sidechain), never to the audio you hear. Raise it to keep low-frequency hum/rumble/proximity effect from falsely holding the gate open on a quiet passage; a typical starting point for a guitar DI is 80-150 Hz. |
| **SC LPF** | 1000 to 16000 | 16000 | Hz | *(v0.2.0)* Low-pass filter applied *only* to the detection path, in series after SC HPF. Defaults fully open (16 kHz) so it doesn't change v0.1.0 behaviour unless you touch it. Lower it, together with SC HPF, to narrow the detector onto the guitar pick-attack transient band (roughly 2-5 kHz) instead of the wideband-above-hum default - useful when sustained low-mid buzz/hum is falsely holding the gate open. |
| **Knee** | 0 to 24 | 0 | dB | Width of a soft-knee band centred on Threshold. `0 dB` (default) is the original hard-knee gate: the target gain snaps instantly between Range and unity at the thresholds. Wider values blend the target smoothly across the band instead, for a gentler, less "switchy" transition on signals that hover near Threshold — Hold still guarantees a fully open target for its whole duration regardless of Knee. |
| **Duck** | off/on | off | — | Inverts the gain computer: instead of opening above Threshold, the output attenuates toward Range above Threshold. Same detection path (SC HPF, SC LPF, hysteresis, Hold, Knee, Lookahead) — useful for ducking a rhythm guitar under a lead, or combined with an external sidechain for a kick-triggered ducking effect. |
| **Listen** | off/on | off | — | Routes the sidechain-filtered detection signal directly to the output, bypassing the gain computer entirely. Use this while dialling in SC HPF/SC LPF and Threshold to hear exactly what the detector is reacting to, then turn it back off. |
| **Ratio** | 1:1 to ∞:1 | ∞:1 (Gate) | — | *(v0.4.0)* How hard the signal is pushed down once it falls below Threshold. At the top of the range — the default, displayed **∞ : 1 (Gate)** — it is a gate: below Threshold the signal goes all the way to the Range floor. Turn it down and it becomes a downward *expander*: for every dB the signal falls below Threshold it is attenuated by (Ratio − 1) dB, so a decaying note is made quieter in proportion instead of being switched off. Around 2:1–4:1 is the setting that cleans up amp noise between phrases without anyone being able to tell a gate is there at all. |
| **Hysteresis** | 0 to 12 | 3 | dB | *(v0.4.0)* How far below Threshold the signal has to fall before the gate closes again. This was a fixed 3 dB internally before v0.4.0, which is why 3 is the default — leave it there and nothing changes. Raise it if the gate flutters on material that sits right around Threshold; lower it (down to 0, where the two thresholds coincide) for the tightest possible close on clean, well-separated material. |
| **Detector** | Peak / RMS | Peak | — | *(v0.4.0)* How the detection path measures level. **Peak** is the original: it reacts to the highest instantaneous excursion, so it catches every pick attack — and also every isolated click. **RMS** measures energy over a 5 ms window instead, so brief spikes that carry almost no energy (fret noise, a crackly DI, bleed from a nearby drum) stop opening the gate. Switching is crossfaded and click-free. |
| **SC Slope** | 12 / 24 dB/oct | 12 dB/oct | — | *(v0.4.0)* The steepness of **both** sidechain filters. 12 dB/oct is the original. 24 dB/oct gives the detection band much harder edges — worth reaching for when the thing falsely holding the gate open sits just outside the band you have dialled in and a gentler slope keeps letting it through. |
| **Smooth Open** | off/on | off | — | *(v0.4.0)* Shapes the gate's opening into a continuous ramp that fits inside the Lookahead window, so even **Attack at 0 ms** opens without a step. It costs no extra latency — the ramp finishes exactly as the delayed transient leaves the lookahead delay. Note that it also smooths the *closing* edge and holds the gate open for up to half the Lookahead time after the signal falls away; that is usually inaudible and occasionally useful, but it is why the tightest presets leave it off. Has no effect when Lookahead is 0. |
| **Release Shape** | Exponential / Linear | Exponential | — | *(v0.4.0)* **Exponential** is the original program-dependent approach described below. **Linear** closes at a constant number of dB per second instead, so a full close takes exactly the Release time and the tail's decay rate never changes as it falls — the behaviour of a dB-linear VCA, and the more predictable choice when you want the gate's close to sit under a note's own decay rather than track it. |

### Duck mode (ducking instead of gating)

Enable **Duck** to invert the gain computer: instead of opening above Threshold,
the output attenuates toward Range above Threshold, turning the same engine
into a ducker. The detection path is unchanged (SC HPF, SC LPF, hysteresis,
Hold, Knee, Lookahead all still apply), so every tool you'd use to shape a
gate's response shapes the ducking response the same way. Typical uses: duck
a rhythm-guitar layer under a lead part, or combine Duck with an
[external sidechain input](#external-sidechain-input) for a kick- or
click-triggered rhythmic ducking effect.

### How the ramp actually behaves (v0.2.0)

Attack/Release still mean "time for a full-scale transition" (Range floor to
unity, or back) at their stated ms values. What's new in v0.2.0: a
transition that only needs to cover *part* of that distance - because a note
only dipped slightly under the close threshold before re-opening, for
example, rather than fully closing - now completes in proportionally *less*
wall-clock time than a full swing would, instead of always taking the same
time regardless of how far the gain actually has to move. In practice this
means sustained, slightly-dynamic playing sounds smoother and less
"pumped," while the gate still snaps fully open/closed at the stated
Attack/Release speed on genuine full transients. This mechanism is inspired
by (not a reproduction of) the "program dependent"/"AutoDynamic" release
behaviour documented for hardware noise gates in this category - see
`docs/design-brief.md`'s honesty section for the full sourcing and
limitations.

## Under the hood

A few mechanisms worth knowing about if you want to understand why v0.4.0
behaves the way it does, not just what the knobs are labelled:

**Ratio is a real downward-expander law, and the default is the literal old
code.** Below Threshold, Ratio applies (Ratio − 1) dB of attenuation for
every dB the signal falls, measured to within 0.25 dB of that law at 2:1,
4:1 and 8:1, with a knee that's continuous in both value and slope at both
edges so widening Knee never introduces a corner. At the top of the range —
∞:1, the default — the gain computer branches to the exact pre-v0.4.0 code
path rather than to a very large-ratio approximation of it, so "unchanged by
default" is a code-level fact, not a numerical coincidence.

**Smooth Open shapes the opening with a moving-max plus two cascaded box
filters**, all running in the dB domain, sized to fit entirely inside the
current Lookahead window. A backward moving-max delays falls but not rises,
so a rising edge passes through it instantly, and the box-filter cascade
then spreads that edge linearly across the window — finishing exactly as the
delayed transient leaves the lookahead delay line. Measured on a
silence-to−6 dBFS burst at 0 ms Attack / 5 ms Lookahead: the steepest
single-sample step drops from 59.5 dB/sample to 0.50 dB/sample (a 119×
reduction), within 0.01% of the theoretical triangular-kernel peak, and the
ramp is monotone throughout. It runs in series with the ballistic gain
rather than as a parallel "whichever is more open" blend because that
parallel version was tried and failed: at 0 ms Attack the raw step wins the
max and the click it exists to remove survives untouched.

**The Lookahead crossfade is real-time-safe by construction.** The 10 ms
tap crossfade and the atomic hand-off to the message-thread poll (see
[Reported latency](#reported-latency) above) were built specifically so that
`processBlock()` never allocates — verified under a replaced allocator
across guarded blocks that include the exact block where Lookahead moves.

**Two detectors and two sidechain slopes run every block, whether selected
or not.** The RMS detector (5 ms mean-square) and the 24 dB/oct sidechain
filter chain are both always warm, so switching Detector or SC Slope is a
crossfade between two live signals rather than a jump to a cold filter with
its own settling transient. Measured rejection one octave below cutoff:
12.30 dB at 12 dB/oct and 24.11 dB at 24 dB/oct, against a theoretical
12.3/24.1.

**Engineering hygiene:** 114 test cases and 9156 assertions run on macOS and
Windows on every push, plus pluginval at strictness 10 and `auval -strict`.
Zero heap allocations on the audio thread under a replaced allocator with
every v0.4.0 feature engaged (including live Lookahead changes), renders
independent of host block size to within 1e-6 RMS, and an open gate that
still measures as a pure delay to better than −120 dBFS with every new stage
switched on at once.

## External sidechain input

Silentium exposes an optional second input bus, **Sidechain**, disabled by
default in every host (enable it in your DAW's routing/input matrix, the same
way you would for a sidechain compressor). When enabled and something is
actually routed into it, the detection path (SC HPF → envelope → hysteresis →
Knee) is keyed from that sidechain signal instead of the main input, while the
main input is still what gets delayed, gated/ducked, and sent to the output.

Typical uses:

- Key a rhythm guitar's gate off a **kick drum or click track** for a tight,
  rhythmically-locked chug pattern independent of the guitar's own pick
  dynamics.
- Key one guitar layer's gate off **another guitar layer** (or a reference DI)
  so a doubled/quad-tracked part gates in lockstep rather than each layer
  making its own independent (and therefore slightly different) gating
  decisions.
- Combine with **Duck** for a sidechain-triggered ducking effect, e.g. ducking
  rhythm guitars under a kick or a lead vocal.

If the sidechain bus is disabled, or enabled but nothing is actually connected
to it, Silentium falls back to keying off the main input automatically — there
is no special "no sidechain" mode to configure.

## Presets

A preset bar sits at the top of the plugin window: `[<] [Name] [>] [Save]
[Save As...] [Delete] [Import...] [Export...]`, plus a menu (click the
preset name) listing Factory and User presets and a "Set current as
default" action. Nine factory presets ship with v0.2.0 - see
[`docs/presets.md`](presets.md) for what each one is for. User presets are
stored per-user (`~/Library/Audio/Presets/Yves Vogl/Silentium/` on macOS,
`%APPDATA%/Yves Vogl/Silentium/Presets/` on Windows) and can be exported as
single `.basilicapreset` files or imported (single files or `.zip` banks)
via the Import/Export buttons.

## Language

The preset bar's labels, menus, and dialogs automatically switch to German
if your system language is German; every other language falls back to
English. This only affects that frame text - parameter names, units, and
all other technical terminology stay in English regardless of system
language, since they are not translated.

## Tips

- **Setting Threshold by ear, not just by eye**: solo the track, play the
  quietest passage you still want to hear (usually the loudest palm mutes
  right before a rest), and lower Threshold until the gate just barely stays
  open through it. Then check the noise floor between phrases actually
  disappears; if it doesn't, Range needs to go lower, not Threshold.
- **Hold vs. Release, don't conflate them**: Hold is about bridging *gaps
  between* transients (rhythmic spacing); Release is about how the *tail* of
  the last transient fades once the gate does decide to close. A choppy-
  sounding fast rhythm part is almost always a Hold problem, not a Release
  problem.
- **SC HPF and Threshold interact**: raising SC HPF removes more low end from
  what the detector sees, which can make Threshold feel like it needs to come
  down slightly to keep catching the same pick attacks (since the sidechain
  signal now carries less energy). Use Listen to check what the detector
  actually hears whenever you change SC HPF.
- **Zero-latency mixing**: set Lookahead to 0 ms if you're tracking live
  through the plugin and latency matters more than a perfectly clean attack;
  restore it for mixing once you're not monitoring in real time.
- **Layered rhythm guitars**: if per-track gating still leaves layers
  drifting slightly out of sync at transients, route all layers' sidechains
  from the same source track (via the external sidechain input) instead of
  self-detecting on each layer independently.
- **Knee for a "less gated" character**: if the hard on/off snap of the
  default (0 dB Knee) sounds too aggressive/switchy on a sustained, dynamic
  performance, widen Knee to 6-12 dB for a smoother transition, then re-check
  that the noise floor between phrases is still adequately attenuated.

## Known limitations (v0.4.0)

- **No published CPU figure.** There is no CPU benchmark or CI performance
  gate in this project, so treat any CPU-usage number you see elsewhere as
  unverified — this manual doesn't claim one. Several detection paths (both
  detectors, both sidechain slopes, the Smooth Open smoother) run
  unconditionally every block, whether selected or not, specifically so that
  switching between them is always a click-free crossfade rather than a jump
  to a cold filter; that is a deliberate fixed cost, not an oversight.
- **RMS isn't universally "steadier" than Peak.** On a sustained low tone
  (around 70 Hz) the 5 ms RMS window doesn't smooth the mean-square ripple as
  well as Peak's own release does, so Peak is actually the steadier detector
  there. RMS's real advantage is high-crest-factor material — isolated
  spikes (fret noise, a crackly DI) that carry almost no energy and shouldn't
  open the gate.
- **Smooth Open also shapes the closing edge**, not just the opening one: it
  holds the open target for up to half the Lookahead time after the signal
  falls away before the close ramp starts. Usually inaudible, occasionally
  useful, but it's why the tightest, most surgical presets leave it off.
- **The six v0.4.0 parameters live in an expansion bay below the faceplate**
  (added after v0.4.2 — earlier v0.4.x builds exposed them via host
  automation and the generic parameter view only). Ratio and Hysteresis are
  brass knobs; Detector, SC Slope, Smooth Open and Release Shape are
  two-position switches whose active option is lit in gold next to the
  lever. All six are keyboard-operable like every other control (Tab to
  focus, arrows/Page/Home/End on knobs, Space/Return on switches). The
  main faceplate itself is unchanged — its nine knobs and two toggles are
  part of the approved v0.3.x artwork.
- **Detection is stereo-linked by construction**, with no user-exposed
  control over it: all channels are combined via `max(|channel|)` before
  detection, and one gain is applied identically to every channel. There is
  no stereo-link percentage, dual-mono mode, or M/S detection option.
- **No oversampling.** The only nonlinearity in Silentium's signal path is a
  multiplicative gain whose bandwidth is bounded by the ballistics, so
  antiderivative anti-aliasing doesn't apply here — a deliberate design
  decision, not something left off the roadmap.
- **Pre-1.0, AGPLv3.** Breaking changes remain possible until v1.0.0.
