# What belongs in creature-common

A joint survey of creature-controller and creature-server, done 2026-11-08 after
the mDNS library moved here. Each side was surveyed by someone who knew that
tree, then reconciled.

## The test for whether something belongs here

Not "is this code duplicated." Duplication is cheap to live with when the copies
cannot hurt each other.

The thing worth paying to remove is duplication **that can drift silently and
break the show**. A multicast group defined twice under two different constant
names can be changed on one side, compile clean on both, and take the audio down
with nothing in any log to say why. A `Result<T>` written twice cannot do that —
it is just typing.

Everything below is ranked by that test, not by line count.

## Tier 1 — wire contracts

Small code, highest consequence. Both sides already agree on these values; the
problem is that nothing *makes* them agree.

### RTP/RTCP protocol constants

Defined independently in `controller/src/audio/audio-config.h` and
`server/src/server/config.h`:

| | controller | server |
|---|---|---|
| RTP port | `RTP_PORT = 5004` | `RTP_PORT = 5004` |
| RTCP port | `RTP_PORT + 1` | `RTP_PORT + 1` |
| Opus payload type | `RTP_OPUS_PAYLOAD_TYPE = 96` | `RTP_OPUS_PAYLOAD_PT = 96` |
| frame | `FRAME_MS = 10` | `RTP_FRAME_MS = 10` |
| rate | `SAMPLE_RATE = 48'000` | `RTP_SRATE` |
| groups | `DIALOG_GROUP_BASE` + n, BGM at `.17` | `.1`–`.17` listed explicitly |

The values match today. Note that **the names don't**, and the multicast groups
aren't even the same shape — so there is no circumstance in which a compiler
catches one side moving. The controller's header carries the comment
`// Network (match server)`, which is the honest admission that this is
hand-maintained.

A header-only `creature::rtp_protocol` with the ports, payload type, frame
geometry and a `dialogGroup(n)` / `bgmGroup()` pair would end it. Perhaps fifteen
lines.

**Keep local:** buffer sizes, jitter targets, playout delays, bitrates. Those are
tuning, not contract, and the two sides legitimately differ.

### RTCP Sender Report encode/decode

The server builds SRs (`rtp/RtcpPacket.h`, `buildRtcpSenderReport` over
`RtcpSenderReportData`) and the controller parses them (`audio/RtcpPacket.h`,
`parseRtcpSenderReport` → `optional<RtcpSenderReport>`).

This is easy to mistake for a diverged fork — the two files have a 241-line diff
— but it isn't one. They are opposite halves: one encoder, one decoder, two
independent struct definitions of the same wire format, and no test anywhere that
puts them back to back.

One encode/decode pair here with a round-trip test makes sender/receiver drift
impossible to commit. This is the strongest single item on the list.

**Not shareable:** RTP header framing. uvgRTP builds the RTP headers on the
server (`uvgrtp::media_stream`, `RCC_DYN_PAYLOAD_TYPE`, `RCC_SSRC`), so there is
no hand-rolled builder to pair with the controller's `RtpPacket` parser. RTCP SR
is hand-rolled only because `RtcpSender` runs its own socket and thread rather
than using uvgRTP's RTCP.

**Keep local:** playout logic — the controller's `RtcpTiming`, the server's
`RtpClockMapping`. One per side, legitimately different.

## Tier 2 — a capability the server is missing

### creature_timesync

~550 lines: `TimeSync`, `TimeSyncMonitor`/`TimeSyncWatcher`, `PtpManagement`.
Reads `ptp4l` over its read-only UDS (`/var/run/ptp4lro`) and the kernel clock via
`adjtimex`; reports "unknown" off Linux. Already a self-contained static library
target, and its only dependencies are `Logger` (already here) and a
`vector<pair>` TXT helper that already fits `MdnsService::txt`.

This one is not deduplication at all — the server has **no** PTP code. It is the
same shape as mDNS was: a capability one side has and the other needs.

The argument is specific. `RtpClockMapping.h` declares
`using WallClock = std::chrono::system_clock`, and `wallClockToNtp()` stamps the
SR's NTP field from it. If `phc2sys` stops disciplining the server's clock, every
receiver schedules playout against a bad timestamp and **nothing on the server
notices**. A `TimeSyncWatcher` there buys a log line when sync is lost and a
`sync=` entry in the mDNS TXT record.

One caveat to settle on the way in: `PTP_MAX_OFFSET_NS = 50 µs` is justified as
"two samples at 48 kHz". That is a receiver-shaped number; the sender likely wants
its own threshold.

## Tier 3 — cheap and safe

### setThreadName

`controller/src/util/thread_name.cpp` and `server/src/util/threadName.cpp`. Same
function, byte-identical `#if defined(__linux__)` / `__APPLE__` ladder, different
filename. Ten lines, used in 16 controller files.

Both copies carry the same bug: `#elif` with no condition before the `#warning`,
so the intended "I don't know this platform" warning would be a hard error
instead. Moving it fixes that once rather than twice.

## Tier 4 — the big one

### The audio output sink

`AudioOutput` + `AlsaAudioOutput` + `CoreAudioOutput` + `AlsaMixerControl` +
`AudioOutputKeepalive`. This *is* a diverged fork, and the most expensive one:

| file | controller | server |
|---|---|---|
| `AlsaAudioOutput.cpp` | 462 | 267 |
| `CoreAudioOutput.cpp` | 479 | 355 |
| `AlsaMixerControl.cpp` | 141 | 123 |
| `AudioOutput.h` | 37 | 31 |

**Scope:** the sink only. Not `AudioEngine`, `PacketReceiver`, `RoutingMixer` or
`RtpJitterBuffer` — that is the receive path, and the server is the sender.

**Why it is a real refactor and not a merge.** The forks disagree on purpose. The
controller's backends are richer (device enumeration by index, ALSA channel maps
for named `SpeakerChannel`s, much better diagnostics — the ENOTSUPP/HDMI advice is
genuinely good) but take rate and channel count as compile-time constants from
`audio-config.h`, hardcoding 48 kHz and rejecting more than 8 channels. The
server's are simplified but generalized to a runtime sample rate and arbitrary
channel count, which it needs for 17-channel work.

A merged config needs a channel **count** plus an optional speaker **map**, with
the map driving `configureChannelMap()` only when present. Making rate and
channels runtime on the controller side is the bulk of the work — more than
reconciling the two files.

## No, and why

A short no-list is worth as much as the yes-list, so these don't get relitigated.

- **`Result<T>`.** Same skeleton on both sides (`variant<T, Error>`,
  `optional<T> getValue()` by value), differing only in the error type —
  `ControllerError` vs `ServerError` plus `serverErrorToStatusCode()`. Sharing
  means `Result<T, E>` with each repo keeping its own `E`. The call-site count
  (2424 server, 146 controller) is **not** the blocker: an alias template
  (`template <typename T> using Result = common::Result<T, ServerError>;`) leaves
  every one of them untouched. The real blockers are that `Result<void>` exists
  only on the server as a specialization, and that the constructors are `explicit`
  on the controller and implicit on the server, which changes call syntax. It
  removes no bug. Not now.
- **`StoppableThread`.** Same name, *opposite* semantics: the controller's
  `shutdown()` and destructor `detach()`, the server's `join()`. Merging means
  picking a winner, and the controller's shutdown path (serial threads, 2 s
  timeouts, ESTOP) was tuned around detach. If anything this is a controller bug
  to fix locally — detach lets the thread outlive the object — not a sharing
  opportunity.
- **`MessageQueue`.** The controller's has bounded shedding via a `canDrop`
  predicate, `request_shutdown()` and `pop_timeout()`, all built for the serial
  path. The server's is 39 lines and it also has `BoundedCommandQueue`. Nothing
  to gain.
- **`string_utils`.** Controller-shaped — includes `controller-config.h` for
  `u32`/`u64` and parses the Pico protocol's POS/LIMIT tokens.
- **`ranges` (`convertRange`), `fast_hsv2rgb`, `CountingThread`.** Controller-only,
  and the latter two may be dead (zero includes each).
- **`http_utils`.** One user: the controller's registration POST. The server is
  the endpoint.
- **`Configuration`, `CommandLine`, `main`, `Creature`, `Input`,
  `MessageProcessor`, `IMessageHandler`.** Matching filenames, unrelated
  contents. False positives from the basename sweep.

## Later, deliberately

**The registration and WebSocket JSON contract.** The controller POSTs
`/api/v1/creature/register` and sends sensor/estop/watchdog messages the server
parses. Sharing those structs would protect that contract the way the RTCP item
protects the wire. It is bigger and more opinionated, and the server already has a
strict contract layer of its own (`src/api/` with `xFromJson`/`xToJson` rejecting
unknown fields, plus a frozen route manifest) — so sharing here means the
controller consuming the server's contract discipline, which is an architectural
decision rather than a move. A later phase.

## Not creature-common, but found on the way

**`opsnlops/libe131` is pinned at different commits in the two repos** — server
`78ab65a`, controller `c4eb1d7`, with an uncommitted local bump to `8fbf3f5` in
the server worktree. Three states of the E1.31 library between the sender and the
receiver. It is an external submodule so it does not belong here, but it is the
same class of risk as the wire constants, and the fix is to pin both repos to one
tag.

## Suggested order

1. **CI first.** Done — this repo is green on amd64 Linux, arm64 Linux and macOS,
   with a symbol check asserting the Linux-only publisher is really in the
   archive. This was a precondition, not a nicety: a broken shared encoder fails
   silently on both ends.
2. **Wire constants + RTCP SR together.** Small, header-heavy, highest value.
3. **Timesync.** Self-contained, and closes a real unmonitored failure mode.
4. **The audio sink.** Largest, and needs the runtime-config refactor first.
