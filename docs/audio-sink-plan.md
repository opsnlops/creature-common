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

**Status on the controller side: signed off, pending confirmation that the
include table below is verbatim.** The session that owns creature-controller
reviewed this document three times — at `beec572`, `a18d98c` and `25d7d5d`,
against controller `a102c79` — asked to be read as the owner of code that runs on
the creatures every day, and declined each time. Its objections are incorporated
throughout and listed under
[What sign-off took](#what-sign-off-took).

**Sign-off on the plan is not sign-off on step 4**, which keeps its gate, its
hardware checks and April's go-ahead when the time comes.

A note on where the facts here come from, because an earlier version of this line
overstated it. That session checked four controller-side claims in the *first*
draft of this plan and answered two questions. It had not reviewed this version
when the line said it had. Claims about the controller below are either its words
or marked as unverified; claims about the server were checked in this repo.

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

| moves here | server keeps | controller keeps |
|---|---|---|
| `AudioOutput` + both backends | `DecodedAudioStream` | `AudioEngine` |
| *`AlsaMixerControl`, conditionally* | `MonoWavDownmixer` | `PacketReceiver` |
| | `LocalAudioPlaybackCoordinator` | `RoutingMixer` |
| | `NativeAudioPlaybackService` | `RtpJitterBuffer` |
| | | `AudioOutputKeepalive` |

**The move is deliberately smaller than #5's scope**, at the controller session's
insistence, and it is right.

`AudioOutputKeepalive` **stays on the controller.** It is sized from the
controller engine's idle queue (`rtcpIdleQueueFrames`, `AudioEngine.cpp:75`), and
the server's bounded silence queue inside `NativeAudioPlaybackService` already
works. Move it only if the server commits to using it verbatim, which it has no
reason to.

`AlsaMixerControl` is a genuine candidate rather than a no: the server **does**
use hardware volume — `applyConfiguredVolume()` is called from
`NativeAudioPlaybackService.cpp:54` and `outputVolumePercent` is wired through
`main.cpp:340`. But it is where the silent failure in
[The ALSA guard](#the-alsa-guard-and-why-the-no-op-check-is-weak) lives, so it
moves only behind that build-time guard, and only after the sink itself has
soaked.

So the smallest honest move is `AudioOutput` and the two backends.

### The interfaces are already the same

Eight of the nine `AudioOutput` members are identical in name and signature
across the two copies. The only differences are the config type and that the
controller injects a `creatures::Logger`. Take the controller's, logger included:
the server already adapts that interface via `SharedLibraryLogger` for mDNS and
timesync, so it costs an adapter already written.

```cpp
std::unique_ptr<AudioOutput> createAudioOutput(std::shared_ptr<creatures::Logger> log);
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
- **The field keeps its name, `commonPlayoutDelayMs`.** An earlier version of
  this plan wanted it renamed to `bufferCapacityMs`, and the controller session
  retracted that suggestion — its own — because renaming means editing
  `config.commonPlayoutDelayMs` in both backends, which breaks "unchanged" for
  no behavioural gain. The explanation that it is capacity rather than a target
  belongs in this document, not in the code.

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

### What "unchanged" actually costs

The first version of this plan said the move would be "same code, different
include path". The controller session says that cannot be done as written, and
named what has to change. This is its account, not mine.

The two backend `.cpp` files include only `audio/AudioOutput.h`. But
`AudioOutput.h`, `AlsaMixerControl.h` and `AudioOutputKeepalive.h` all include
`audio/audio-config.h` — the controller's whole audio configuration, also
included by `AudioEngine`, `RoutingMixer`, `RtcpTiming`, `CommandLine`,
`Configuration` and the router. From it the sink uses:

- **`AudioConfig`**, which mixes sink fields (`channels`, `deviceNumber`,
  `deviceName`, `outputVolumePercent`, `alsaMixerCard`, `alsaMixerElement`,
  `commonPlayoutDelayMs`) with engine fields (`dialogGainDb`, `bgmGainDb`,
  `limiterCeilingDb`, `audioDeviceCompensationMs`).
- **`SpeakerChannel`**, whose name and parse functions live in `AudioConfig.cpp`
  beside `validateAudioEngineConfig`.
- **Constants**: `AUDIO_DEVICE_PERIOD_FRAMES`, `AUDIO_OUTPUT_RING_FRAMES`,
  `DEFAULT_SOUND_DEVICE_NUMBER`. And `TARGET_PLAYOUT_FRAMES` and
  `PACKET_WAIT_MS`, which the **engine also uses** (`AudioEngine.cpp:523`,
  `AlsaAudioOutput.cpp:261`).

So step 1 forces a split of `AudioConfig` — which is withdrawn step 3 coming
back, in a smaller form. And once it happens, two constants that govern the
controller's playout live in a repo the server edits. That is a real cost and
this document should name it rather than imply the move is free.

**The least invasive split**, as that session describes it: a shared
`AudioOutputConfig` holding exactly those seven sink fields with the same names,
types and defaults, and the controller's `AudioConfig` deriving from it with the
engine fields added. No brace-initialisation of `AudioConfig` exists anywhere in
its `src` or `tests`, so no call site changes. It is still a change to controller
code, and step 1 owns it.

### The ALSA guard, and why the no-op check is weak

The first version leaned entirely on "step 2 should be a no-op for the
controller". That check is weaker than it sounds, for a reason worth stating
plainly: **nothing in CI exercises the real backends.** `AudioEngine_test` runs
against a fake output, and there is no ALSA backend or mixer test anywhere. So
"nothing changed" can only be established by ear.

And there is a silent failure mode — **but only if `AlsaMixerControl` moves.**
Being precise about this matters in both directions, so nobody thinks the guard
covers the backends, and nobody skips it because the backends do not need it.

`CREATURE_HAS_ALSA` appears in `AlsaMixerControl.cpp` and nowhere else: lines 8,
14 and 78 on the controller, and 10, 16 and 77 on the server, where it was
checked in this repo. **Both backends use it zero times.** Without it the mixer
compiles to a do-nothing branch.

It is a **`PRIVATE`** compile definition on the consumer's own target, set under
`if(ALSA_FOUND)` — controller `CMakeLists.txt:207-210`, server `CMakeLists.txt:463`
and `728-729`, the identical pattern. And a `PRIVATE` definition on a consumer
target **cannot reach a library target**. So a moved `AlsaMixerControl.cpp`
compiles with the guard undefined *by default, not by anyone's mistake*:
everything builds, every test passes, audio plays, and the hardware volume
setting silently stops being applied — on both consumers.

**The gate that makes the check real**, in three parts:

1. **A mechanical diff gate, with every allowed difference enumerated in
   advance.** "Include lines only" will not do: it fails the first time it runs,
   and a gate that fails on day one gets loosened on the spot, which is the one
   thing it exists to prevent. The allowed differences are exactly:

   - **each include line, listed before → after, per file.** "Include lines" is
     an open category and must not stay one: an *added* include passes a
     category-level gate and can change behaviour through a macro or an overload.

     The list is fixed **here and now**, not at step 1. An earlier version of
     this plan deferred it, on the reasoning that whoever does the move should
     read it off the real files rather than trust a transcription. The controller
     session overruled that, correctly: if the list is written by the person
     making the change, then the gate certifies itself. The "before" side below
     was read off controller `a102c79` by that session, with `git show`.

     | file (`a102c79`) | line | before | after |
     |---|---|---|---|
     | `AudioOutput.h` | 9 | `#include "audio/audio-config.h"` | `#include "creature/audio/AudioOutputConfig.h"` |
     | `AlsaAudioOutput.cpp` | 1 | `#include "audio/AudioOutput.h"` | `#include "creature/audio/AudioOutput.h"` |
     | `CoreAudioOutput.cpp` | 1 | `#include "audio/AudioOutput.h"` | `#include "creature/audio/AudioOutput.h"` |

     **Only those three change.** Every other include in those files stays
     byte-identical, system and platform headers included —
     `<alsa/asoundlib.h>` and the three Apple frameworks among them.
     `AudioOutput.h:10` (`#include "creature/logging/Logger.h"`) is already a
     creature-common path and does not change.

     Step 1 verifies against this table. If it needs different target paths, the
     step-1 PR amends the table, and **that amendment needs controller review** —
     which is the whole point of fixing it in advance.
   - `AudioConfig` → `AudioOutputConfig` at `AudioOutput.h:18`,
     `AlsaAudioOutput.cpp:85` and `CoreAudioOutput.cpp:112` — the `open()`
     signature, three sites
   - nothing else

   **The one file the gate cannot cover is `AudioOutputConfig.h`**, because it is
   new: there is nothing to diff it against. The constant-and-default value check
   covers it instead. It holds `AudioOutputConfig`, `SpeakerChannel` and the six
   constants, and **it must include `creature/rtp/Protocol.h`** — today the
   backends receive `rtp::SAMPLE_RATE` and `rtp::SAMPLES_PER_FRAME` through
   `audio-config.h`. Omitting it is a compile error rather than a silent failure,
   but it is written here to save step 1 the surprise.

   **Every line number in this section is anchored to controller `a102c79`.**
   They will drift as the controller changes, and the step-4 run needs to know
   what they referred to. Re-anchor and re-record them if step 1 happens against
   a later commit.

   **The namespace stays `creatures::audio`.** Changing it would add diff lines
   and force call-site edits in both consumers.

   It is a **script, run at two moments**, not CI: at step 1, that the copy is
   faithful; at step 4, that the shared copy still matches the controller's last
   local copy. After step 4 there is nothing local left to compare and the
   ownership rule takes over. Paste its output into each PR. CI would be
   cross-repo machinery for a check that runs twice.

2. **A build-time guard.** `find_package(ALSA REQUIRED)` on Linux in
   creature-common (the controller already requires it at `CMakeLists.txt:159`),
   plus a hard build error if `CREATURE_HAS_ALSA` is undefined on Linux. A
   missing definition must break the build rather than the volume. **This also
   means creature-common's Linux CI runners need `libasound2-dev`** — without it
   every Linux job fails the moment `find_package(ALSA REQUIRED)` lands. Part of
   step 1.

3. **Hardware checks, per consumer**: `outputVolumePercent` is applied at
   startup; the ALSA channel map puts each speaker in the right place on the
   router's 8-channel AVR setup; the underrun count stays at zero; RTCP sync
   still holds.

## Order of work

Revised at the controller session's insistence, and its order is better than
mine. The first version had the controller switch second, immediately after the
move. That put the birds at risk before anything had been learned.

1. **Copy the sink into creature-common.** `AudioOutput` and the two backends,
   plus the minimal `AudioOutputConfig` split that B forces. No improvements, no
   tidying — the value of this code is that it is already trusted, and editing it
   on the way past forfeits that.
2. **The server adopts it**, deletes its own sink files, and the config narrowing
   lands in the same commit. Prove it in travel mode on real speakers.
3. **Soak — and April decides when it ends, not either session.** The server is
   the consumer that gains, so let it carry the shared code before anything else
   depends on it. The bar to clear: travel mode used for real playback, with zero
   underruns. "A while" is not an exit criterion and neither session should be
   the one declaring it met.

   **A soak only counts for the code that soaked.** The step-1 gate — including
   the check that the constant values match the controller's — must pass *before*
   the soak starts. Otherwise the server is soaking a different sink from the one
   the creatures run, and the result says nothing about step 4. Any change to the
   shared sink during the soak restarts it for step 4's purposes, and falls under
   the ownership rule.
4. **The controller switches — optional, last, and gated.** Only after the diff
   gate and the build-time guard are in place, with hardware checks on a bird and
   on the router, and with April's go-ahead. **If this step never happens, the
   controller loses nothing**, and that is the point: the risk sits with the party
   that benefits until the benefit is demonstrated.

   **This step has a handover the earlier plan missed entirely.** After step 1,
   six constants exist in two headers in the same namespace:

   - `TARGET_PLAYOUT_FRAMES`
   - `PACKET_WAIT_MS`
   - `AUDIO_DEVICE_PERIOD_FRAMES`
   - `AUDIO_OUTPUT_RING_FRAMES`
   - `DEFAULT_SOUND_DEVICE_NUMBER`
   - `DEFAULT_COMMON_PLAYOUT_DELAY_MS`

   Two `inline constexpr` definitions of one name in one translation unit will not
   compile, so at step 4 the controller must delete its own definitions — and the
   diff gate must check that the **shared values equal the controller's** before
   it does. The same goes for the struct defaults carried across: `deviceNumber`,
   `alsaMixerCard{"default"}`, and `commonPlayoutDelayMs`, which defaults to
   `DEFAULT_COMMON_PLAYOUT_DELAY_MS`.

   That sixth constant was missing from the controller session's own first list of
   them, and it caught the omission itself on re-review. Recorded because a
   silently different default is exactly the class of thing this gate exists to
   catch, and the list being nearly right is how it would have got through.

   **A seventh shared definition: `SpeakerChannel`.** Same problem as the
   constants — at step 4 the controller deletes its `enum class SpeakerChannel`,
   and the gate confirms the shared enum has the same enumerators *in the same
   order*. `speakerChannelName` and `parseSpeakerChannel` stay in the
   controller's `AudioConfig.cpp`.

   **And the split's own silent failure, which is the worst thing in this plan.**
   When the controller's `AudioConfig` derives from `AudioOutputConfig`, its own
   seven sink fields must be **deleted**, not merely joined by a base class. If
   they stay, *it compiles*: a derived member legally hides a base member, and
   `-Wshadow` does not flag it. `Configuration` and `CommandLine` would then write
   the derived `channels`, `deviceName` and `commonPlayoutDelayMs`, while
   `open(const AudioOutputConfig &)` reads the base's **defaults** — Mono, the
   default device, a 20 ms buffer. A creature that plays and sounds wrong, with
   every existing test green, because the engine test runs against a fake output.

   So the handover requires both: delete the seven fields, **and** add a
   controller unit test that sets each field through `AudioConfig` and reads it
   back through `const AudioOutputConfig &`. That test fails if any field is
   hidden, and it is the only thing in this plan that would catch this
   automatically.

Hardware verification gates each consumer's own release. It is not a final step
after everything is wired together.

## Verification

A passing test suite means little here. The sink's job is to make a speaker move.

- **Unit, here:** almost nothing, and that is correct. An earlier version of
  this plan listed "zero-rate rejection" and gain clamping, which describe code
  that does not exist — the moved sink has no rate parameter, and the
  controller's validation is engine-side in `validateAudioEngineConfig`. **New
  validation running in the sink path would change the controller's behaviour, so
  it must not be part of step 1.** A round-trip test of a config struct is not
  evidence of anything.
- **Controller, on a bird:** audio plays and the ALSA channel map still puts the
  right speaker in the right place. A wrong map is audible instantly and
  invisible to every test. Lip sync is worth checking too — read `mouth_slot` out
  of the animation data, where roughly 11 moves/sec is healthy — though that
  figure is from this session's own earlier work and the controller session has
  not vouched for it.
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

Two of the three questions the first version asked are already answered, by the
session that knows the code:

- ~~Does `deviceNumber` belong in a shared config?~~ **Yes, keep both.**
  Precedence is already defined: `deviceName` wins, `deviceNumber` is the
  fallback (`AlsaAudioOutput.cpp:93-97`, and the same in CoreAudio). Resolving
  index to name in the caller would change controller code for no gain.
- ~~Does anything still need `OUTPUT_CH = 1`?~~ **Yes**, the engine's Opus
  decoder (`AudioEngine.cpp:150`). Not a sink matter; stays local.

Both of the questions this plan raised have now been answered by the controller
session, as the owner of the behaviour:

1. **Does `AlsaMixerControl` move? Default no.** It moves only after the sink has
   soaked, behind the build-time guard, and with a hardware check that
   `outputVolumePercent` takes effect on a bird. Until all three hold, it stays
   local and the server keeps its own copy.
2. **Diff gate: a script, not CI.** It runs at exactly two moments — step 1 and
   step 4 — and after step 4 there is nothing local left to compare. Its output
   gets pasted into each PR.

Nothing is open on the controller side. What is open is everything in
[Order of work](#order-of-work) that has not been done, which is all of it.

## What sign-off took

Two rounds of review by the session that owns creature-controller, both of which
it conducted as the owner of code running on the creatures every day.

**Round one** (declined): qualify the provenance line; name the `AudioConfig`
split as part of step 1; drop the `bufferCapacityMs` rename; add the three-part
gate; put the server first and the controller's switch optional and last; write
the ownership rule into [what-belongs-here.md](what-belongs-here.md); shrink the
move. All done.

**Round two** (declined, three must-fixes): the survey's old Tier 4 text still
contradicted this plan; the diff gate as written would have failed the first time
it ran; the step-4 constant handover was missing entirely. All three are fixed
above, along with its four additions — the soak's owner and exit criterion, the
guard's precise scope, creature-common's `libasound2-dev` dependency, and its
answers to both open questions.

**Round three** (declined, four narrow fixes): the `AudioConfig` split's member
hiding, which compiles and misconfigures the device silently; `SpeakerChannel`
missing from the step-4 handover; "include lines" left as an open category in the
gate, with its line numbers unanchored; and the soak counting only for code that
passed the step-1 gate first. All four are above.

**Round three's fixes were verified by diff** (`25d7d5d..e233874`), and sign-off
granted on one condition: that the gate's include table be written into this
document now rather than filled in at step 1, verbatim as that session read it
off `a102c79`. Done above. It is confirming the paste.

The argument for fixing it in advance is worth keeping, because it generalises:
**if the list is written by the person making the change, the gate certifies
itself.** This plan had deferred it for a defensible-sounding reason — not
wanting a transcription of someone else's files to become the reference — and
that reason was worse.

Three rounds, nineteen separate objections, and the three most valuable were all
silent failures: a `PRIVATE` compile definition that cannot reach a library
target, a derived member legally hiding a base member past `-Wshadow`, and a soak
that would have measured code the creatures do not run. None was reachable by
reasoning from this side; each came from the party holding the code.

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

**One of the three partly returns, and not by choice.** Splitting `AudioConfig`
is forced by the header dependency described in
[What "unchanged" actually costs](#what-unchanged-actually-costs) — the sink's
headers pull in the controller's whole audio config, so the split is the price of
the move rather than an improvement anyone wanted. It returns in the smallest
form that works: seven sink fields in a shared struct the controller's config
derives from. The other two stay withdrawn.
