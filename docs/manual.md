# Silentium user manual

*Silence between the storms — a tight lookahead noise gate for palm-muted rhythm.*

## What it is

Silentium is a noise gate purpose-built for high-gain guitar in a symphonic-metal
mix: the moment between palm-muted chugs where amp hiss, pedalboard hum, and
buzzing strings would otherwise be audible. It is not a general-purpose dynamics
gate borrowed from a mix bus — every default and every internal ballistic is
tuned for the specific problem of "silence a loud, distorted guitar the instant
the player stops picking, without clipping the leading edge of the next pick
attack."

## Where it sits in a symphonic-metal chain

Silentium is a **detection-and-dynamics** stage, not a tone-shaping one. Put it:

- **Before** any drive/distortion/amp-sim plugin (`twist-your-guts` and friends in
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
- In a **layered rhythm-guitar mix** — a symphonic-metal wall-of-guitars — put
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
                    +-- SC HPF (20-500 Hz) --> stereo-linked max|.| --> peak envelope follower --+
                    |                                                                             |
Input --> Lookahead |                        hysteresis comparator + hold timer + knee blend <----+
 (or Sidechain      |                                                    |
  bus, if enabled)  |                                        attack/release gain ramp (dB domain)
    |               |                                                    |
    +---------------+------------------------------------------------> x (gain), or Listen output -> Output
```

1. A **copy** of the input (or, if you've enabled the external sidechain input
   and your host has routed something into it, that sidechain signal instead)
   is high-passed by **SC HPF** so low-frequency hum/rumble can never falsely
   hold the gate open. This filtered copy is only ever used to *decide* the
   gain; it never reaches the output directly, unless you enable **Listen**.
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
   **Release** ramp (dB domain), then applied to the **main** signal — which
   has meanwhile been delayed by **Lookahead** so the gain can start rising
   just before a transient's leading edge actually arrives, avoiding an
   audible "chirp" on fast picking. Lookahead is reported to the host as this
   plugin's total latency, so plugin-delay compensation keeps it phase-aligned
   with everything else in your session.
8. If **Listen** is enabled, all of the above still runs (so metering/timing
   stays consistent), but the output is the sidechain-filtered detection
   signal itself (step 1, post SC HPF) instead of the gated main signal — for
   auditioning exactly what the detector hears while you dial in SC HPF and
   Threshold.

See [`docs/architecture.md`](architecture.md) for the full implementation-level
breakdown (state machine details, real-time-safety notes, the `GateEngine`
class this describes).

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Threshold** | -80 to 0 | -40 | dB | The level the (sidechain-filtered) envelope must reach to open the gate. Lower it to catch quieter pick attacks; raise it to ignore more of the amp's noise floor. The gate's close threshold always sits a fixed 3 dB below this, so a signal hovering right at Threshold can never chatter the gate open/closed. |
| **Attack** | 0.1 to 50 | 1 | ms | Time to ramp from the Range floor up to unity once the envelope opens the gate. Very fast (close to 0.1 ms) for percussive picking so the leading edge isn't audibly softened; slower for a more natural swell on sustained chords. |
| **Hold** | 0 to 500 | 20 | ms | Minimum time the gate stays open once opened, continuously retriggered while the envelope stays above the close threshold. This is what keeps the gate open across the brief silences *between* consecutive palm-muted chugs of a fast rhythm part — set it to roughly the gap between your fastest picking subdivisions. |
| **Release** | 5 to 500 | 80 | ms | Time to ramp back down to the Range floor once Hold has fully elapsed. Fast values are tighter/more percussive; slower values let a chord's natural decay breathe a little before the gate closes. |
| **Range** | -80 to 0 | -60 | dB | Floor attenuation applied while the gate is closed. `0 dB` disables gating entirely (an always-open passthrough) — useful as an A/B reference. Values around -40 to -60 dB usually silence amp noise convincingly without sounding like a hard mute; very deep values (-80 dB) are essentially silence. |
| **Lookahead** | 0 to 20 | 5 | ms | Delays the main signal so the gate's gain can start rising just before a transient's leading edge arrives, avoiding an audible attack chirp even with a very fast Attack. Reported to the host as this plugin's total latency (plugin-delay compensation handles the rest automatically). Changing it takes effect the next time your host re-prepares the plugin (e.g. on playback start/stop), not instantly mid-playback. |
| **SC HPF** | 20 to 500 | 80 | Hz | High-pass filter applied *only* to the detection path (sidechain), never to the audio you hear. Raise it to keep low-frequency hum/rumble/proximity effect from falsely holding the gate open on a quiet passage; a typical starting point for a guitar DI is 80-150 Hz. |
| **Knee** | 0 to 24 | 0 | dB | Width of a soft-knee band centred on Threshold. `0 dB` (default) is the original hard-knee gate: the target gain snaps instantly between Range and unity at the thresholds. Wider values blend the target smoothly across the band instead, for a gentler, less "switchy" transition on signals that hover near Threshold — Hold still guarantees a fully open target for its whole duration regardless of Knee. |
| **Duck** | off/on | off | — | Inverts the gain computer: instead of opening above Threshold, the output attenuates toward Range above Threshold. Same detection path (SC HPF, hysteresis, Hold, Knee, Lookahead) — useful for ducking a rhythm guitar under a lead, or combined with an external sidechain for a kick-triggered ducking effect. |
| **Listen** | off/on | off | — | Routes the sidechain-filtered detection signal directly to the output, bypassing the gain computer entirely. Use this while dialling in SC HPF and Threshold to hear exactly what the detector is reacting to, then turn it back off. |

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
