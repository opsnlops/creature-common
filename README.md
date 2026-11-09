# creature-common

Shared libraries for [April's Creature Workshop](https://github.com/opsnlops).

These started life inside
[creature-controller](https://github.com/opsnlops/creature-controller), where the
controller and the audio router already shared them. They live here so
[creature-server](https://github.com/opsnlops/creature-server) can use them too,
instead of growing its own copy — which is how the server ended up carrying a
diverged fork of the audio output backends.

## Libraries

| target | what |
|---|---|
| `creature::logging` | The `Logger` interface, so a library can log without knowing who is listening. Header-only. |
| `creature::mdns` | DNS-SD service advertising through `avahi-daemon`. |
| `creature::rtp` | The RTP/RTCP wire contract: ports, payload type, framing, channel groups, and the RTCP Sender Report encode/decode pair. |
| `creature::thread` | `setThreadName()`, so threads show up by name in `htop` and Instruments. |

### `creature::logging`

An abstract `creatures::Logger`. Implementations live in the consumers — the
controller's `SpdlogLogger`, the server's adapter onto its spdlog free functions
— which keeps each project's observability stack out of the shared code and
makes the libraries testable without a global logger.

### `creature::mdns`

`MdnsService` is the record: instance name, service type, port, and TXT pairs.
`mdnsInstanceName()` lower-cases a name for publishing and `fitTxtRecord()`
trims any value that would overrun the 255-byte DNS-SD entry limit, returning
the whole record's size so a caller can check it still fits one packet.

`AvahiPublisher` advertises a service for as long as it lives. It waits for
`avahi-daemon` if the daemon isn't up yet, re-registers if the daemon restarts,
picks an alternative name if another device is already using one, and can
replace its TXT records while running via `updateTxt()`.

Advertising is a convenience, so the publisher never takes its host process
down: a publisher that cannot start says so and is otherwise ignored.

Publishing goes through `avahi-daemon`, so **`AvahiPublisher` is only built on
Linux**. Elsewhere the record types still compile, `CREATURE_HAS_AVAHI` is
undefined, and nothing is advertised — which is what you want for a macOS dev
build.

### `creature::thread`

`creatures::setThreadName()` names the calling thread. Linux only accepts 15
bytes and rejects anything longer outright, leaving the thread with its
parent's name, so names are shortened to `MAX_THREAD_NAME_LENGTH` (15 on Linux,
63 on macOS) rather than lost. `fitThreadName()` shows what a name becomes. On
other platforms it does nothing and says so at compile time.

## Using it

Consume by tag with `FetchContent`, *after* your project makes `fmt` available
so this one uses yours rather than fetching a second copy:

```cmake
FetchContent_Declare(
        creature_common
        GIT_REPOSITORY https://github.com/opsnlops/creature-common.git
        GIT_TAG         v0.1.0
)
FetchContent_MakeAvailable(creature_common)

target_link_libraries(my-thing PRIVATE creature::mdns)
```

Include paths are rooted at `creature/`, so nothing inherits a bare `logging/`
or `network/` include root:

```cpp
#include "creature/logging/Logger.h"
#include "creature/network/AvahiPublisher.h"
#include "creature/network/MdnsService.h"
```

Each consumer builds its own TXT records. The shape of a record is a local
decision — the controller describes a creature, the router describes its routing
matrix, the server describes its API — so the builders stay in their own repos
and only the plumbing is shared.

## Building standalone

```bash
cmake -B build -G Ninja .
cmake --build build
ctest --test-dir build
```

Tests are built only when this is the top-level project, so pulling the library
into another build does not drag its test targets along.

On Debian/Ubuntu the publisher needs `libavahi-client-dev` to build and
`avahi-daemon` to run.

## License

MIT. See [LICENSE](LICENSE).
