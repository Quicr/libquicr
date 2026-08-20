# Sharding picoquic instances with SO_REUSEPORT

Date: 2026-08-08
Status: Design approved, not yet implemented

## Problem

`sendmsg` is the throughput bottleneck on the relay. `PicoQuicTransport` owns
exactly one `picoquic_quic_t` and one network thread, so every connection's
packets are serialised through a single loop on a single core.

The workload that hurts is a many-to-many MoQ meeting: N participants in one
full mesh, each publishing to and subscribing from every other. Relay send load
grows as O(N²) while the loop thread stays pinned to one core. Fan-out (one
publisher, many subscribers) already performs acceptably.

## Approach

Run N picoquic instances inside one `PicoQuicTransport`, each with its own
network thread, all sharing the listen port via `SO_REUSEPORT`. The Linux kernel
hashes incoming 4-tuples across the listening sockets, so distinct client
connections land on distinct shards and their `sendmsg` calls run on N cores.

This is picoquic's supported multi-threaded server model, added in `22b50dcf`
("Initial support of multiple threads per server", April 2026) and described in
`dependencies/picoquic/doc/parallel.md`. No picoquic changes are required.

### Why this fits the workload

Each meeting participant holds one connection to the relay, so N participants
give N distinct 4-tuples for the kernel to spread. Fan-out from a single
publisher still happens on that publisher's shard, but the *sends* to each
subscriber happen on the subscriber's own shard.

### Why the hot path parallelises

The per-packet TX path — `SendStreamBytes`, `SendNextDatagram` — touches only
per-connection state, each `tx_data` queue carrying its own lock. Neither takes
`state_mutex_` in the common case. N loop threads therefore run genuinely
concurrently.

## Constraints on the change

Mergeability is a primary goal, ahead of elegance. The change must read to a
libquicr maintainer as a small, conventional extension of the existing design
rather than a rework of it.

### `quic_shards == 1` must be a no-op

The single strongest reviewability property, and the anchor for the whole
change: at the default setting, behaviour, thread count, socket count and
`picoquic_packet_loop_param_t` contents are all identical to today. A reviewer
can satisfy themselves that existing deployments are unaffected without tracing
the sharded paths. Nothing in the implementation may compromise this.

### Style and naming are CI-enforced

- `.clang-format` — Mozilla base, 120 columns, 4-space indent. A **Format Check**
  job runs in `cmake.yml`, and a `clang-format` pre-commit hook is configured in
  `.pre-commit-config.yaml`. Run it before pushing.
- `.clang-tidy` — `readability-identifier-naming` with
  `WarningsAsErrors: "*"`, applied to `src/` via `HeaderFilterRegex`. CI
  configures with `-DLINT=ON` (`cmake/Lint.cmake`), so a naming slip is a build
  failure, not a nit.

The relevant rules, which every identifier proposed in this document already
satisfies: classes and functions `CamelCase`; members `lower_case`; private
members take a `_` suffix and public members take none; constants are
`k`-prefixed `CamelCase`. Note that `Shard`'s members are public and therefore
carry no suffix, while `shards_` itself is private and does.

### Follow the patterns already in the file

`SafeQueue` for cross-thread queues, the existing `RunPqFunction` indirection for
reaching the picoquic thread, `SPDLOG_LOGGER_*` for logging, and
`TransportException` / `TransportError` for failures. Introduce no new
mechanism where one of these already fits.

### Deliberately not touched

Listed so a reviewer can confirm the blast radius by inspection:

- `include/quicr/` — apart from the one `TransportConfig` field.
- `src/session.cpp`, `src/transport.cpp` — the transport factories are unchanged.
- `PqEventCb`, `PqAlpnSelectCb`, `DefaultWebTransportCallback` — signatures and
  context pointers all unchanged.
- The WebTransport `path_items` / `server_params` / `path_app_ctx` chain.
- Any member not listed as moving into `Shard` keeps its current name.

Every item under "Deferred work" stays out of this PR, including the two known
follow-on bottlenecks. Landing sharding measurably and in isolation is worth
more than bundling.

## Public API

One additive field. No signature changes anywhere.

```cpp
// include/quicr/transport.h, TransportConfig
std::size_t quic_shards{ 1 }; ///< Parallel QUIC instances sharing the listen port (server only)
```

`std::size_t` and the single-line `///<` comment match the immediately
neighbouring `max_connections`. The `quic_` prefix matches the existing
`quic_cwin_minimum`, `quic_wifi_shadow_rtt_us`, `quic_qlog_path` and
`quic_priority_limit`. The field is appended at the end of the struct so no
existing member's position moves.

`ServerConfig` already embeds `TransportConfig`, so relay operators pick this up
with no plumbing.

Effective shard count:

```
shards = is_server_mode ? max(1, quic_shards) : 1
```

Clients are forced to 1: a single connection is a single 4-tuple and cannot be
split across sockets.

### Platform behaviour

`quic_shards > 1` is permitted on every platform. It is not gated, because
allowing it means the sharded code path is exercised during local development.

Measured on Darwin 25.3.0 (4 sockets with `SO_REUSEPORT`, 40 datagrams from
distinct source ports): all 40 datagrams were delivered to the last-bound
socket. Darwin does not load-balance; that is FreeBSD's `SO_REUSEPORT_LB`, which
Darwin does not have. On macOS, N shards therefore behave as one active shard
plus N-1 idle ones — correct, but with no parallelism.

The effective shard count is logged at startup.

## Socket configuration

picoquic applies `is_port_shared` only to the `public_port` socket, never the
`local_port` one (`sockloop.c:493` vs `:505`). This is deliberate, not a defect:
per `doc/parallel.md`, *"The 'local port' is used for outgoing connections, and
is not shared with any other thread. The 'public port' is used for incoming
connections."* The configuration below matches `picoquicdemo.c`'s multi-thread
server path (`sockloop.c:3291`).

| `picoquic_packet_loop_param_t` field | `shards == 1` | `shards > 1` |
|---|---|---|
| `local_port` | `serverInfo_.port` | `0` |
| `public_port` | `0` | `serverInfo_.port` |
| `is_port_shared` | `0` | `1` |
| `prefer_extra_socket` | `0` | `1` |

With `shards == 1` these are byte-identical to today's values, so the default
configuration is unchanged.

`prefer_extra_socket` is load-bearing. Send-socket selection scans for a socket
matching the address family and, if `send_port == 0` and `prefer_extra_socket`
is unset, breaks on the *first* match — which would be the ephemeral socket
(`sockloop.c:2737`). Setting it makes the scan continue and settle on the shared
socket. It has no other effect: its only other use, `alt_sock`, is reached only
when `options.provide_alt_port` is set, which we do not set.

### local_port choice

`local_port = 0` (ephemeral). No new ports to open on the relay firewall and
nothing new to configure.

The consequence is recorded under "Deferred work": preferred-address migration
needs a *known* port, because picoquic reads `config->local_port` when the
context is created (`config.c:930`). Adopting it later means introducing a port
base — a config and ops change at that point, not a code redesign.

### Known cosmetic consequence

With `local_port = 0`, the `picoquic_packet_loop_port_update` callback reports
the *ephemeral* port rather than the listen port, because picoquic derives it
from `s_ctx[0]` (`sockloop.c:2467`). `PqLoopCb` only logs this value, so it is
harmless — but nothing may come to depend on it.

## Internal structure

A private `Shard` absorbs everything currently singular in `PicoQuicTransport`:

```cpp
struct Shard
{
    PicoQuicTransport* transport{ nullptr };
    std::size_t index{ 0 };
    picoquic_quic_t* quic_ctx{ nullptr };
    picoquic_network_thread_ctx_t* thread_ctx{ nullptr };
    picoquic_packet_loop_param_t loop_params{};
    int loop_return_value{ 0 };
    SafeQueue<std::function<int()>> runner_queue;
    uint64_t loop_prev_time{ 0 };
    uint64_t loop_metrics_prev_time{ 0 };
};

std::vector<std::unique_ptr<Shard>> shards_;
```

`unique_ptr` because each `Shard*` is handed to picoquic as a callback context
and must not move when the vector grows.

These members are removed, their roles moving into `Shard`: `quic_ctx_`,
`quic_network_thread_ctx_`, `quic_network_thread_params_`,
`quic_loop_return_value_`, `picoquic_runner_queue_`, `pq_loop_prev_time`,
`pq_loop_metrics_prev_time`.

`conn_context_` stays a single map guarded by `state_mutex_`. `ConnectionContext`
gains one field:

```cpp
std::size_t shard_idx{ 0 };  /// Shard whose thread owns this connection
```

set in `CreateConnContext` from `picoquic_get_quic_ctx(pq_cnx)` (`picoquic.h:1260`)
via a linear scan of `shards_`. The scan runs once per connection over a vector
sized to the core count.

### Callback contexts are almost unchanged

Connection callbacks keep receiving `PicoQuicTransport*`. `PqEventCb`,
`PqAlpnSelectCb`, `DefaultWebTransportCallback` and the entire WebTransport
`path_app_ctx` chain are untouched, because the shard is recoverable from the
connection rather than carried in the context pointer.

Only `PqLoopCb`'s `loop_callback_ctx` changes, from `this` to `Shard*`. It reads
`shard->transport` for everything it currently reaches through `transport`, and
`shard->loop_prev_time` / `shard->loop_metrics_prev_time` for its timers.

## Routing work to the owning thread

`doc/parallel.md` states the rule plainly: *"perform all API calls from within
the same network thread that manages the relevant connection or the QUIC
context"* — and names our exact hazard:

> "we can imagine 'relay' style application receiving data on a connection, and
> immediately queuing that data on a different connection. The first call is
> executed on the first connection's thread, but the second connection cannot be
> accessed safely in that thread."

That is MoQ relay fan-out. The existing `RunPqFunction` indirection already has
the right shape; sharding makes correct *targeting* mandatory.

`RunPqFunction` becomes shard-targeted:

```cpp
void RunPqFunction(std::size_t shard_idx, std::function<int()>&& function);
```

pushing to `shards_[shard_idx]->runner_queue` and waking
`shards_[shard_idx]->thread_ctx`.

There are nine call sites today. Each falls into one of three cases:

- **Already holding `conn_ctx` under `state_mutex_`** — `Enqueue` (both the
  stream and datagram branches) and `SendStreamBytes`' reset-wait branch. These
  pass `conn_ctx.shard_idx` directly, adding nothing to the path that runs per
  object.
- **Holding only a `conn_id`** — `Close`, `CreateStream`, the public
  `CloseStream`, and the data-context teardown paths. These take a short
  `state_mutex_`-guarded lookup of `shard_idx` before posting. All are
  per-stream or per-connection control operations rather than per-object, so the
  extra acquisition is off the hot path.
- **No connection yet** — `StartClient` uses shard 0. Clients only ever have one
  shard.

The second case needs a small helper:

```cpp
std::optional<std::size_t> GetConnShardIdx(const std::uint64_t& conn_id);
```

which takes `state_mutex_` internally.

> **`state_mutex_` is a plain `std::mutex`, not recursive.** `GetConnShardIdx`
> must never be called while holding it. That is exactly why this is an explicit
> helper rather than a convenience overload of `RunPqFunction` taking a
> `conn_id` — such an overload would look correct at the `Enqueue` call sites
> and self-deadlock, since those already hold the lock.

## Per-shard periodic work

The three `picoquic_packet_loop_time_check` jobs each take a shard index and
skip connections whose `shard_idx` does not match.

- `CheckConnsForCongestion` — **required for correctness**. It calls
  `picoquic_get_path_quality(conn_ctx.pq_cnx, ...)` (`transport_picoquic.cpp:2262`),
  which would otherwise touch another instance's connection from the wrong
  thread.
- `RemoveClosedStreams` — filtered to keep each shard's work proportional to its
  own connections.
- `EmitMetrics` — filtered to avoid emitting each connection's metrics N times.

### Pre-existing bug fixed en route

`EmitMetrics` iterates `conn_context_` while taking no lock at all
(`transport_picoquic.cpp:2196`). It races with `state_mutex_`-guarded mutation
from application threads today; with N shard threads it becomes far more likely
to bite. It must take `state_mutex_`.

## Lifecycle

### Start

The per-instance picoquic setup currently inline in `Start()` — the calls that
take `quic_ctx_`, from `picoquic_create_and_configure` through
`picoquic_set_qlog` — is extracted into `CreateQuicInstance()` returning a
`picoquic_quic_t*`, and called once per shard.

This is the one structural move in the change, and it is the *smaller* diff: the
alternative, wrapping the block in a loop in place, re-indents roughly eighty
lines and buries the real change in whitespace. Extracting keeps the moved lines
byte-identical apart from `quic_ctx_` becoming a local, so a reviewer can diff
them by eye.

The split point is the `local_tp_options_` block: populating it is one-time and
stays in `Start()`, while `picoquic_set_default_tp(quic_ctx_, &local_tp_options_)`
is per-instance and moves. `config_`, `local_tp_options_` and `wt_config_`
remain single instances shared by all shards, read-only after setup.

`picoquic_runner_queue_.SetLimit(tconfig_.callback_queue_size)` becomes a
per-shard `runner_queue.SetLimit(...)`. `cbNotifyQueue_` and `cbNotifyThread_`
are untouched and stay singular.

`Server()` loops over shards, filling each `loop_params` per the table above and
calling `picoquic_start_network_thread(shard.quic_ctx, &shard.loop_params,
PqLoopCb, &shard, &shard.loop_return_value)`.

### Shard group stability — implementation rule

The `SO_REUSEPORT` group must be **fixed for the process lifetime**.

Linux rehashes the group whenever the set of listening sockets changes.
`doc/parallel.md` is explicit that bucket assignment is stable through the
handshake but has *"no guarantee that it will remain stable for the entire
duration of a long running connection."* With a fixed group, assignment is
stable in practice.

Therefore:

- Shards are created once, in `Start()`, and never added or removed.
- A shard whose loop thread dies **must not be respawned**. Rebinding its socket
  rehashes the group and breaks live connections on every *other* shard. A shard
  failure fails the whole transport: log it and drive the normal shutdown path.

### Shutdown

`Shutdown()` iterates shards, performing today's sequence for each: set status,
`picoquic_wake_up_network_thread`, wait for `thread_is_ready` to clear,
`picoquic_delete_network_thread`, `picoquic_free`.

`SetStatus(kShutdown)` is currently driven from a single loop callback. With N
shards it needs a per-shard drained flag, with the global status advancing to
`kShutdown` only once every shard reports drained. Each shard's
`picoquic_get_first_cnx(quic)` already scopes to its own instance, so the
existing close-every-connection logic needs no change beyond that aggregation.

## Testing

- **Unit**: shard index derivation from `picoquic_get_quic_ctx`; `shards == 1`
  produces `loop_params` byte-identical to today's.
- **Regression**: the existing suite at `quic_shards = 1`, expected wholly
  unchanged.
- **Integration**: the existing suite at `quic_shards = 4`, configured through
  `test/integration_test/test_server.cpp`, which already builds a `ServerConfig`.
- **Thread-safety**: a build with picoquic's `WITH_THREAD_CHECK=ON`
  (`dependencies/picoquic/CMakeLists.txt:436`), which `debugbreak()`s on any
  picoquic API call from the wrong thread. Given that `doc/parallel.md` names
  relay-style cross-connection fan-out as the canonical way to get this wrong,
  this is the primary check on `RunPqFunction` routing. Run the integration
  suite under it.
- **Manual, on Linux**: `ss -uanp` shows N sockets on the listen port; a
  multi-participant meeting distributes connections across shards; `top -H`
  shows N busy loop threads where one was busy before.
- **Benchmark**: N-way mesh throughput and relay CPU at `quic_shards` of 1 vs
  core count, to confirm the gain and locate the next bottleneck.

## Relationship to in-flight refactors

Built on `main`. Two open PRs were assessed:

- **#914 (`resolve-later`)** — session layer only; does not touch the transport.
  Irrelevant here.
- **#892 (`refactor-session`)** — rewrites `transport_picoquic.cpp` (+674/-677)
  and `transport_picoquic.h` (+65/-229), so it overlaps almost every function
  this change modifies. It does **not** make sharding easier: every member the
  change touches survives it verbatim (`quic_ctx_`, `quic_network_thread_ctx_`,
  `quic_network_thread_params_`, `quic_loop_return_value_`,
  `picoquic_runner_queue_`, `state_mutex_`, `local_tp_options_`, `config_`),
  `RunPqFunction` and `PqRunner` are unchanged, and `Server()` is untouched. The
  refactor works on the connection/session layer; sharding works on the
  instance/loop/thread layer.

What #892 changes for this work is mechanical:

| | `main` | post-#892 |
|---|---|---|
| connection map | `std::map<std::uint64_t, ConnectionContext> conn_context_` | `std::map<std::uint64_t, std::shared_ptr<PicoQuicConnection>> connections_` |
| `shard_idx` home | `ConnectionContext` in `src/transport_picoquic.h` | `PicoQuicConnection` in `src/picoquic_connection.h` |
| `TransportConfig` | `include/quicr/transport.h` | `include/quicr/config.h`, content identical |

Function bodies are otherwise unchanged — `conn_ctx.metrics` becomes
`connection->metrics`.

Because #892 is expected to land, the implementation hedges against it at no
present cost:

- `shard_idx` is a single field on the connection type, relocating cleanly to
  `PicoQuicConnection`.
- `GetConnShardIdx` is the only place mapping a `conn_id` to a shard, so the map
  rename lands in one function.
- Nothing is renamed by this change. Every rename added here multiplies conflicts
  during the eventual rebase.

## Documentation to update

`docs/implementation.md:107` currently states *"Three threads are used by
libquicr"*, the first being the single picoquic event loop thread. That becomes
inaccurate with sharding and must be updated in the same PR — a maintainer
reading the design docs should not be told something the code contradicts.

## Deferred work

Recorded deliberately, not overlooked.

- **Connection migration and NAT rebinding.** Under 4-tuple hashing, a migrated
  client's packets reach a shard that does not know the connection ID. Out of
  scope by decision; needs its own design. Nothing here blocks it.
- **Preferred-address migration.** picoquic's own answer to residual hash
  instability: each shard advertises its `local_port` as the server's preferred
  address and clients migrate off the shared port after the handshake.
  Implemented in picoquic (`config.c:930`) but needs the relay's public IP
  configured, a known per-shard port (see "local_port choice"), and client-side
  support. Part of the migration work above.
- **CID-aware steering via `SO_ATTACH_REUSEPORT_EBPF`.** The robust fix for both
  of the above; `doc/parallel.md` describes it as unimplemented future work.
- **`Enqueue`'s global `state_mutex_`** (`transport_picoquic.cpp:1103`). Held for
  the whole function body, so the O(N²) enqueues per frame in a mesh serialise
  across all shards. The next wall after `sendmsg`, deliberately left until
  sharding is measured.
- **The single `cbNotifyThread_`.** Every delegate callback — all receive-side
  dispatch and the relay's fan-out logic — runs on one thread. Splitting it
  per-shard would preserve per-connection callback ordering while parallelising
  across connections, but changes the threading contract `Session` and relay code
  rely on, so it needs its own risk review.
- **WebTransport under sharding.** Expected to work, since h3zero and
  WebTransport contexts are already per-connection and the `path_app_ctx` chain
  is untouched. Untested; `PqAlpnSelectCb` and the per-instance
  `path_items`/`server_params` should be re-read before relying on it.

## Non-issues

Ticket and token concerns raised in `doc/parallel.md` — per-thread session
resumption state, STEK sharing across instances — do not apply. libquicr uses no
0-RTT, session resumption, or address-validation tokens.
