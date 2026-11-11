# Sharing the audio output sink

**Issue:** [#5](https://github.com/opsnlops/creature-common/issues/5) — Tier 4 in
[what-belongs-here.md](what-belongs-here.md), the last item from the sharing survey.

**Status:** planned, not started. Written 2026-11-10 from a read of both
codebases.

The controller-side readings below were checked against creature-controller
`af1efd8` by the session working that repo. Three of the four needed reframing;
what follows is the reframed version, and the reframings matter more than the
original claims did.

This plan exists because #5 is the one item on the list where the survey's
summary is not enough to work from — and, as it turns out, where the summary is
wrong in several specifics. Those corrections are most of this document's value.

---

## What the survey got wrong

The survey calls this "a genuine refactor rather than a move" and "the largest
item". The second is true by line count. The first is true, but for narrower
reasons than it claims, and the refactor is smaller than it sounds.

### The interfaces are already the same

The survey's table compares file sizes and leaves the impression of two
divergent designs. They aren't. Here are both `AudioOutput` interfaces:

| | controller | server |
|---|---|---|
| `open(…)` | `const AudioConfig &` | `const NativeAudioConfig &` |
| `start()` | same | same |
| `stopAndClear()` | same | same |
| `close()` | same | same |
| `write(std::span<const int16_t>)` | same | same |
| `queuedFrames()` | same | same |
| `pipelineLatencyFrames()` | same | same |
| `underruns()` | same | same |
| factory | `createAudioOutput(shared_ptr<Logger>)` | `createAudioOutput()` |

Eight of nine members are identical in name and signature. The only real
differences are the config type and the injected logger. **The interface does
not need designing; it needs picking.**

### The channel count is already runtime on both sides

The survey says the controller "takes rate and channels as compile-time
constants from `audio-config.h`, hardcoding 48 kHz and rejecting more than 8
channels". Half right.

Channels are already runtime: `AudioConfig::channels` is a
`std::vector<SpeakerChannel>`, so the count is `channels.size()`. What exists is
a **hard cap** of 8 — in four places, not the two a first read suggests:

```
AlsaAudioOutput.cpp:88
CoreAudioOutput.cpp:115
AudioConfig.cpp:65          validateAudioEngineConfig, "between one and eight channels"
AudioOutputKeepalive.cpp:11
```

Nothing depends on it. ALSA's `configureChannelMap` sizes its `snd_pcm_chmap_t`
storage from `channels.size()`, CoreAudio sizes its `AudioChannelLayout` the same
way, the rings scale by `channels_`, and there is no fixed-size per-channel
storage anywhere in `src/audio`. The cap arrived with `965ce38` (2026-10-09, the
multichannel HDMI router), and 8 is HDMI's LPCM maximum — so it describes the
hardware that router was built for rather than a limit of the code.

A cap derived from the map would be **10, not 11**. `SpeakerChannel` has eleven
values, but `Mono` cannot be combined with others (`AudioConfig.cpp` validation),
so the largest legal map holds ten. And a device that cannot open ten channels
already fails at `snd_pcm_hw_params` without a cap's help.

The server has no cap at all: both its backends check only for zero.

### The sample rate is the real compile-time assumption

This is the part that is genuinely a refactor. `AudioConfig` has no
`sampleRate` field, and both controller backends reach for `rtp::SAMPLE_RATE`
directly — setting the ALSA rate, setting `format.mSampleRate`, sizing the
queue target, and converting frames to milliseconds. Twelve occurrences, evenly
split:

```
AlsaAudioOutput.cpp:138,142,143,145,161,392
CoreAudioOutput.cpp:118,182,430,439,441,445
```

Each needs to become `config.sampleRate`. Mechanical, but it touches the
latency maths, which is where a mistake would be quiet.

**Adding the parameter does not make the sink rate-agnostic, and nothing here
should be read as claiming it does.** The sink never resamples. It consumes
whatever the engine writes, and the engine decodes Opus at `rtp::SAMPLE_RATE`
(`AudioEngine.cpp:150`) in `SAMPLES_PER_FRAME` chunks; ALSA then refuses outright
any device that cannot do exactly that rate (`AlsaAudioOutput.cpp:142-147`). So
`sampleRate` means *the rate the producer writes, which the device must match
exactly* — not *the rate the sink adapts to*. A consumer that wants a different
rate resamples upstream.

The playout side uses `rtp::SAMPLE_RATE` as well (`AudioEngine.cpp:14,150,376`,
`RtcpTiming.cpp:19-27,101`, `AudioSubsystem.cpp:110,112`). That is wire rate, and
it correctly stays compile-time.

### `commonPlayoutDelayMs` crosses the line the survey draws

The survey says to keep playout logic local, which is right, but the controller's
sink **reads `config.commonPlayoutDelayMs` itself** — to size *capacity*, not a
target:

- ALSA's hardware buffer is `max(TARGET_PLAYOUT_FRAMES · SAMPLES_PER_FRAME,
  delay + 1 frame)` (`AlsaAudioOutput.cpp:159-161`)
- CoreAudio's ring is `max(AUDIO_OUTPUT_RING_FRAMES, delay + 2 frames)`
  (`CoreAudioOutput.cpp:117-120`)

How much actually gets queued is the engine's decision (`rtcpIdleQueueFrames`,
`AudioEngine.cpp:36,66`), never the sink's. So the field does have to cross into
the sink config — drop it and a 500 ms delay could no longer fit in the device
buffer — but it must be named for what the sink does with it:
**`bufferCapacityMs`**, or a frame count passed directly. Calling it
`queueTargetMs` would name the *engine's* job and invite someone to implement it
in the wrong place.

The server's nearest equivalent is the constant
`NATIVE_AUDIO_QUEUE_TARGET_MS = 100`, worth renaming on the way past for the same
reason.

### `audioDeviceCompensationMs` does *not* cross it

Checked: it appears nowhere in the sink files. It is playout-only and stays on
the controller.

### `AudioOutputKeepalive` is controller-only

The server has the constant (`NATIVE_AUDIO_KEEPALIVE_FRAMES = 960`) but no
implementation. So this is a move, and it is also the server *gaining* something.

---

## What is actually shared, and what each side gains

**The controller's copy is richer** where the survey says it is, and this is the
part to preserve:

- device enumeration by index (`deviceNumber`)
- ALSA channel-map configuration for named `SpeakerChannel`s (`snd_pcm_set_chmap`
  — controller only, both backends)
- the diagnostics. `AlsaAudioOutput.cpp:107` decodes `ENOTSUPP` (524), which
  neither libc nor ALSA describes; `:116` explains that a Pi HDMI output with no
  audio-capable display attached will fail this way. The server has **zero**
  occurrences of either. This is the single best reason to do the work.

**The server's copy is more general:** runtime rate, no channel cap, 17 channels
in production.

Neither is a superset. That is why it is a refactor.

---

## The merged config

Most fields are already identical in both structs — same names, same types, same
defaults — so this is less invention than it looks.

```cpp
namespace creature::audio {

struct AudioOutputConfig {
    // Device selection. Name wins when both are set; number is how the
    // controller selects, name is how the server does.
    std::optional<std::string> deviceName;
    std::optional<uint8_t> deviceNumber;

    // Format. Runtime on both sides after this change.
    uint32_t sampleRate{48'000};
    uint16_t channelCount{1};

    // Optional, and only meaningful on ALSA: drives configureChannelMap()
    // when present, skipped entirely when empty. The server leaves it empty
    // and gets today's behaviour; the controller fills it and keeps its
    // named-speaker mapping. channelCount must agree with its size when set.
    std::vector<SpeakerChannel> speakerMap;

    // Capacity the sink must provide, not a level it tries to hold: the
    // device buffer and the ring are sized from this. How much is actually
    // queued is the producer's decision. The controller passes its
    // commonPlayoutDelayMs; the server passes 100.
    uint16_t bufferCapacityMs{100};

    // Gains and limiter -- already byte-identical in both structs.
    float dialogGainDb{0.0F};
    float bgmGainDb{0.0F};
    float limiterCeilingDb{-1.0F};

    // Mixer -- also already identical.
    std::optional<uint8_t> outputVolumePercent;
    std::string alsaMixerCard{"default"};
    std::string alsaMixerElement;
};

} // namespace creature::audio
```

Deliberately **not** in it: `commonPlayoutDelayMs` and
`audioDeviceCompensationMs` as such. The controller keeps its own
`AudioConfig` for playout and builds an `AudioOutputConfig` from it. Its current
struct does double duty for sink and playout, and separating those is part of
the work rather than an accident of it.

Period and ring sizes (`256` / `8192`) are identical constants on both sides
already; they stay constants here rather than becoming fields, until something
needs them to differ.

## The interface

Take the controller's, including the injected logger:

```cpp
std::unique_ptr<AudioOutput> createAudioOutput(std::shared_ptr<creature::Logger> log);
```

The server logs through spdlog free functions in its copy, but it already has
`SharedLibraryLogger`, which adapts exactly this interface and is already used
for mDNS and timesync. So this costs the server one adapter it has already
written, and it buys the diagnostics, which are the point.

---

## Order of work

Each step is separately verifiable, and the first three are reversible without
touching the server.

1. **Controller: make the sample rate runtime.** Add `sampleRate` to its
   `AudioConfig`, replace the twelve `rtp::SAMPLE_RATE` uses in the two
   backends, and pass `rtp::SAMPLE_RATE` from the call site. Behaviour-identical by
   construction — the value is the same — so this is the safe half of the
   refactor, done while it can still be tested against the controller alone.
2. **Controller: raise or drop the channel cap.** Four places, not two:
   `AlsaAudioOutput.cpp:88`, `CoreAudioOutput.cpp:115`, `AudioConfig.cpp:65`,
   `AudioOutputKeepalive.cpp:11`. If a cap is kept, the map-derived figure is 10.
3. **Controller: split `AudioConfig`** into playout config plus an
   `AudioOutputConfig`-shaped subset. Still controller-local; still testable
   against a bird.
4. **creature-common: land the sink** — the interface, both backends built from
   the controller's copy as the base (it is the richer one), `AlsaMixerControl`,
   `AudioOutputKeepalive`. Port the server's generality onto it rather than the
   reverse.
5. **Controller: consume it**, deleting its local copies.
6. **Server: consume it**, deleting its local copies. Travel mode must still
   play stereo and mainstage must still do 17 channels.

Steps 1–3 are worth doing even if 4–6 never happen: they remove the compile-time
assumptions that make the controller's sink unshareable, and they leave it
better regardless.

## Verification

This is the one item where a passing test suite means little. The sink's job is
to make a speaker move.

- **Unit, in creature-common:** config validation only — `channelCount` against
  `speakerMap.size()`, zero-rate and zero-channel rejection, gain clamping.
  Round-tripping a config is not evidence.
- **Controller, on a bird:** audio plays, lip sync still lands (read `mouth_slot`
  out of the animation data; ~11 moves/sec is healthy), and the ALSA channel map
  still puts the right speaker in the right place. A wrong channel map is
  audible immediately and invisible to any test.
- **Server, mainstage:** 17-channel output unchanged, per-creature channel
  isolation intact.
- **Server, travel mode:** stereo out of the local device still works. This is
  the path the server's own sink exists for, and the one least likely to be
  exercised by accident.

## Open questions

1. **Is `deviceNumber` worth keeping in the shared struct,** or should the
   controller resolve index → name before calling? Keeping both selectors in one
   struct invites the question of which wins. Resolving early would keep the
   shared config to one way of naming a device.
2. **Should the 8-channel cap become a validated maximum or just go?** It
   records HDMI's LPCM limit from the router it arrived with, not a property of
   this code, and the server runs 17 with no cap at all. Dropping it leaves
   `snd_pcm_hw_params` to say no, which it already does.
3. **Does anything still need `OUTPUT_CH = 1`?** It is in `audio-config.h` and
   did not appear in any sink file during this survey.
