# Giving the server the audio sink the controller already runs

**Issue:** [#5](https://github.com/opsnlops/creature-common/issues/5) — Tier 4 in
[what-belongs-here.md](what-belongs-here.md), the last item from the sharing survey.

**Status:** planned, not started. Written 2026-11-10, then rewritten the same
night after April set the direction.

**The direction, in her words:** don't rewrite the audio engine. Use the one the
controller has — it runs every day, both the controller and the audio-router
depend on it, and it is battle tested. The server should run that same code when
it is in local audio mode, which is mostly travel mode.

That makes this an adoption, not a refactor, and it is much smaller than the
first version of this plan assumed. **The earlier plan proposed changing the
controller's sink to be more general. That was wrong and is withdrawn** — see
[Withdrawn](#withdrawn) at the end for what and why, because the reasoning is
worth not repeating.

Controller-side facts below were checked against creature-controller `af1efd8`
by the session that works that repo.

---

## Why no refactor is needed

The first plan's whole premise was that the controller's sink is too specific to
serve the server: 48 kHz fixed, maximum 8 channels. Both are true. Neither
matters.

**Travel mode is already stereo.** `CommandLine.cpp:504-508` ignores
`--sound-channels` in travel mode and sets 2, with a warning if you passed
anything else. The default rate is 48000. So the server's real local-audio case
is stereo at 48 kHz, which the controller's sink takes unchanged.

Travel mode drives a pair of powered speakers, which is what that stereo is for.
Worth knowing that the force is a choice rather than a limit: if an AVR over HDMI
ever travels — April's assessment is "too much to carry", so not a common path —
the shared sink already allows up to 8 channels, because that ceiling *is* the
HDMI ceiling. The only obstacle would be the hard `setSoundChannels(2)` in travel
mode, which would become "up to 8, defaulting to 2". **Narrowing the config to
1-8 does not foreclose that case; it is exactly the range that case needs.**

**The 8-channel cap is a hardware ceiling, not an accident.** It arrived with
`965ce38` and the multichannel HDMI router, and the audio-router delivers digital
audio over HDMI to an AVR — 8 is HDMI's LPCM maximum. The cap documents the
hardware. Raising it would describe a device nobody has.

**48 kHz is the wire rate and the router's only rate.** The router has no
sample-rate setting at all; it opens devices through the same `AlsaAudioOutput`,
which refuses anything that is not exactly 48 kHz
(`AlsaAudioOutput.cpp:142-147`). The sink never resamples — it consumes what its
producer writes.

## What is shared, and what is not

The server already owns a complete file-to-PCM pipeline. The controller has no
counterpart for any of it, because every producer over there is RTP receive →
Opus decode → `RoutingMixer` → sink. Nothing in the controller or the router
reads audio from disk.

| shared (moves here) | server keeps | controller keeps |
|---|---|---|
| `AudioOutput` + both backends | `DecodedAudioStream` | `AudioEngine` |
| `AlsaMixerControl` | `MonoWavDownmixer` | `PacketReceiver` |
| `AudioOutputKeepalive` | `LocalAudioPlaybackCoordinator` | `RoutingMixer` |
| | `NativeAudioPlaybackService` | `RtpJitterBuffer` |

The sink is the whole overlap, exactly as #5 scoped it.

### The interfaces are already the same

Eight of the nine `AudioOutput` members are identical in name and signature
across the two copies. The only differences are the config type and that the
controller injects a `creature::Logger`. Take the controller's, logger included:
the server already adapts that interface via `SharedLibraryLogger` for mDNS and
timesync, so it costs an adapter already written.

```cpp
std::unique_ptr<AudioOutput> createAudioOutput(std::shared_ptr<creature::Logger> log);
```

## What the server has to do

The sink's contract, as the controller's code actually defines it:

- `write()` takes interleaved `int16` at exactly the wire rate. **The feeder owns
  all conversion and the 17-to-2 downmix.** The server already does this —
  `MonoWavDownmixer` and `readLocalOutputFrames` — so nothing new is needed.
- `open()` reads `commonPlayoutDelayMs` to size the device buffer and ring. That
  is **capacity**, not a level the sink tries to hold; how much is actually
  queued is the producer's decision. The server must pass something that fits
  its own queue depth, or a deep queue will not fit in the device buffer.
- The config needs one field named for that: **`bufferCapacityMs`**. Calling it
  `queueTargetMs` would name the producer's job and invite someone to implement
  it in the sink.

### Narrow the server's config to match — in the same change

The server's validation is wider than any sink it would be using:

| knob | server accepts today | the shared sink accepts |
|---|---|---|
| `--sound-frequency` | 8000–384000 | 48000 only |
| `--sound-channels` (non-travel Local) | 1–64, default 6 | 1–8 |
| `--sound-channels` (travel) | forced to 2 | fine |

April's call: narrow it. 48 kHz and 1–8 channels is what the router has run
every day, so this makes the server's knobs describe reality rather than fail
later at device-open.

**Do it in the same commit as the sink swap, not before.** The server's *current*
sink really does handle 1–64 channels and 8k–384k. Narrowing first would remove
working capability for no reason and leave a window where the config is a lie in
the other direction.

### One genuine decision: the keepalive

The two sides keep the device warm differently. The controller has
`AudioOutputKeepalive` as its own class. The server does it inside
`NativeAudioPlaybackService`, as a bounded silence queue between clips. Same
idea, two shapes, and adopting the controller's reaches further into the server
than swapping the sink does. Worth deciding deliberately rather than by
whichever file gets edited first.

## Order of work

1. **Move the sink here unchanged**, from the controller: `AudioOutput`, both
   backends, `AlsaMixerControl`, `AudioOutputKeepalive`. No behaviour changes, no
   improvements, no tidying. The point of this item is that this code is already
   trusted; editing it on the way past forfeits that.
2. **Controller consumes it**, deleting its local copies. It should be a no-op
   for the controller — same code, different include path — and that is the test:
   if anything about the controller's audio changes, step 1 did too much.
3. **Server consumes it.** Its feeder adapts to the shared interface, its own
   sink files are deleted, and the config narrowing lands in the same commit.
4. **Verify on hardware, both sides.**

Note the asymmetry: steps 1–2 risk a system that works every day, for no gain to
the controller at all. That argues for doing them as literally as possible and
for leaning on step 2 as the check.

## Verification

A passing test suite means little here. The sink's job is to make a speaker move.

- **Unit, here:** config validation only — channel count against the speaker
  map, zero-rate and zero-channel rejection, gain clamping.
- **Controller, on a bird:** audio plays, lip sync still lands (read `mouth_slot`
  out of the animation data; ~11 moves/sec is healthy), and the ALSA channel map
  still puts the right speaker in the right place. A wrong map is audible
  instantly and invisible to every test.
- **Audio-router, into the AVR:** all channels still arrive on the right
  speakers over HDMI.
- **Server, travel mode:** stereo out of the local device still works. This is
  the only path that gains anything, and the one least likely to be exercised by
  accident.
- **Server, mainstage:** unchanged by construction — mainstage is RTP and never
  touches this sink. Worth confirming rather than assuming.

## Considered and declined

**`RoutingMixer` for the server's downmix.** It does 17-to-N with gains,
crossover and limiter, which sounds like exactly what travel mode wants. But it
is built from an `AudioEngineConfig` and takes RTP-shaped `DecodedAudioFrame`
inputs, and the server already has a working downmix. Reusing it would be new
work for no gain. (Flagged as a header-level guess by the controller session,
unverified — and not worth verifying, given the above.)

## Open questions

1. **The keepalive shape**, above.
2. **Does `deviceNumber` belong in a shared config at all,** or should the
   controller resolve index to name before calling? Two selectors in one struct
   invites the question of which wins.
3. **Does anything still need `OUTPUT_CH = 1`?** It is in `audio-config.h` and
   appears in no sink file.
4. **Should travel mode's stereo force become a ceiling instead?** Not now —
   powered speakers are the travelling case. But the shared sink would already
   support an AVR over HDMI, so the day that travels, the change is one line in
   `CommandLine.cpp` rather than anything in the audio code.

## Withdrawn

The first version of this plan proposed three controller-side steps: make the
sample rate runtime (twelve `rtp::SAMPLE_RATE` uses, six per backend), raise or
drop the 8-channel cap, and split `AudioConfig` into playout plus sink config.

All three were aimed at making the controller's sink general enough for the
server. The server does not need it to be: travel mode is stereo at 48 kHz by
force. And two of the three would have made the code *describe the hardware less
accurately* — 8 is HDMI's LPCM maximum for the AVR the router feeds, and 48 kHz
is the only rate the router has ever opened.

The reasoning error was treating "the server's config surface is wider" as
evidence that the server needs the width. It does not; the width was never
exercised. The narrowing above is the opposite move, and the right one.
