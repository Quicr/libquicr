# picoquic Sharding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run N picoquic instances inside one `PicoQuicTransport`, each with its own network thread, sharing the listen port via `SO_REUSEPORT`, so relay `sendmsg` runs on N cores instead of one.

**Architecture:** A private `Shard` struct absorbs everything currently singular in `PicoQuicTransport` — the `picoquic_quic_t`, its network thread, its loop params and its runner queue. `conn_context_` stays a single map guarded by `state_mutex_`; each `ConnectionContext` records the `shard_idx` that owns it, and all work bound for picoquic is routed to that shard's thread. Server mode only; clients are forced to one shard.

**Tech Stack:** C++20, picoquic (vendored submodule), doctest, CMake, spdlog.

**Spec:** `docs/superpowers/specs/2026-08-08-picoquic-sharding-design.md` — read it before starting. It carries the reasoning behind decisions this plan only states.

## Global Constraints

- **`quic_shards == 1` must be a byte-for-byte no-op.** At the default, thread count, socket count and `picoquic_packet_loop_param_t` contents are identical to today. This is the change's reviewability anchor; nothing may compromise it.
- **Naming is CI-enforced.** `.clang-tidy` runs `readability-identifier-naming` with `WarningsAsErrors: "*"` over `src/`; CI configures `-DLINT=ON`. Classes and functions `CamelCase`; members `lower_case`; **private** members take a `_` suffix, **public** members take none; constants are `k`-prefixed `CamelCase`. `Shard`'s members are public (no suffix); `shards_` is private (suffix).
- **Formatting is CI-enforced.** `.clang-format`: Mozilla base, 120 columns, 4-space indent. Run `make format` before every commit.
- **Rename nothing.** Every rename multiplies conflicts against in-flight PR #892. Any member not explicitly moving into `Shard` keeps its current name.
- **Do not touch:** `include/quicr/` beyond the one `TransportConfig` field; `src/session.cpp`; `src/transport.cpp`; the signatures or context pointers of `PqEventCb`, `PqAlpnSelectCb`, `DefaultWebTransportCallback`; the WebTransport `path_items` / `server_params` / `path_app_ctx` chain.
- **All tasks are commits in a single PR.** Intermediate commits may contain not-yet-called helpers; the final state must be coherent.
- **Every task ends green.** `make test` must pass at the end of each task.

### Build and test commands

```bash
make cert
```

```bash
make test
```

`make cert` is needed once per clean build directory — the integration tests load `server-cert.pem` / `server-key.pem` from the build tree. `make test` configures, builds, and runs `ctest`.

---

### Task 1: Config field and pure shard helpers

Two pure static functions and one config field. Nothing calls the helpers yet — Task 5 wires them. They are pure, so they are the only part of this change that is cleanly unit-testable, and they encode the `quic_shards == 1` no-op guarantee as an executable assertion.

**Files:**
- Modify: `include/quicr/transport.h` (`TransportConfig`, end of struct)
- Modify: `src/transport_picoquic.h` (public "Internal public methods" section)
- Modify: `src/transport_picoquic.cpp`
- Create: `test/transport_picoquic.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `static std::size_t PicoQuicTransport::EffectiveShardCount(bool is_server_mode, std::size_t quic_shards);`
  - `static picoquic_packet_loop_param_t PicoQuicTransport::MakeLoopParams(uint16_t listen_port, std::size_t socket_buffer_size, std::size_t shard_count);`
  - `TransportConfig::quic_shards` (`std::size_t`, default `1`)

- [ ] **Step 1: Establish a green baseline**

Run: `make cert && make test`
Expected: all tests pass. If anything fails here, stop — it is pre-existing and must be understood before refactoring on top of it.

- [ ] **Step 2: Add the config field**

In `include/quicr/transport.h`, append to the **end** of `struct TransportConfig`, after `initial_max_stream_data` (appending means no existing member's position moves):

```cpp
        std::size_t quic_shards{ 1 }; ///< Parallel QUIC instances sharing the listen port (server only)
```

- [ ] **Step 3: Write the failing tests**

Create `test/transport_picoquic.cpp`:

```cpp
// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"

#include <doctest/doctest.h>

TEST_CASE("Clients are always a single shard")
{
    CHECK_EQ(quicr::PicoQuicTransport::EffectiveShardCount(false, 1), 1);
    CHECK_EQ(quicr::PicoQuicTransport::EffectiveShardCount(false, 8), 1);
}

TEST_CASE("Servers honour the configured shard count, with a floor of one")
{
    CHECK_EQ(quicr::PicoQuicTransport::EffectiveShardCount(true, 0), 1);
    CHECK_EQ(quicr::PicoQuicTransport::EffectiveShardCount(true, 1), 1);
    CHECK_EQ(quicr::PicoQuicTransport::EffectiveShardCount(true, 8), 8);
}

TEST_CASE("A single shard produces the unsharded loop parameters")
{
    const auto params = quicr::PicoQuicTransport::MakeLoopParams(33435, 2'000'000, 1);

    CHECK_EQ(params.local_port, 33435);
    CHECK_EQ(params.public_port, 0);
    CHECK_EQ(params.is_port_shared, 0);
    CHECK_EQ(params.prefer_extra_socket, 0);
    CHECK_EQ(params.local_af, PF_UNSPEC);
    CHECK_EQ(params.dest_if, 0);
    CHECK_EQ(params.socket_buffer_size, 2'000'000);
    CHECK_EQ(params.do_not_use_gso, 0);
    CHECK_EQ(params.extra_socket_required, 0);
    CHECK_EQ(params.simulate_eio, 0);
    CHECK_EQ(params.send_length_max, 0);
}

TEST_CASE("Multiple shards move the listen port to the shared public port")
{
    const auto params = quicr::PicoQuicTransport::MakeLoopParams(33435, 2'000'000, 4);

    CHECK_EQ(params.local_port, 0);
    CHECK_EQ(params.public_port, 33435);
    CHECK_EQ(params.is_port_shared, 1);
    CHECK_EQ(params.prefer_extra_socket, 1);
    CHECK_EQ(params.socket_buffer_size, 2'000'000);
}
```

In `test/CMakeLists.txt`, add `transport_picoquic.cpp` to the `quicr_test` source list, between `transport_metrics.cpp` and `uintvar.cpp` (the list is alphabetical):

```cmake
    transport_metrics.cpp
    transport_picoquic.cpp
    uintvar.cpp
```

No other CMake change is needed: `quicr_test` already has `${PROJECT_SOURCE_DIR}/src` on its include path, and `quicr` links `picoquic-core` as `PUBLIC`, so picoquic headers are already visible to the test target.

- [ ] **Step 4: Run the tests to verify they fail**

Run: `make test`
Expected: compile error — `EffectiveShardCount` and `MakeLoopParams` are not members of `quicr::PicoQuicTransport`.

- [ ] **Step 5: Declare the helpers**

In `src/transport_picoquic.h`, in the public section under the `/* Internal public methods */` comment, next to `MetricsSampleIntervalUs()`:

```cpp
        /**
         * @brief Number of picoquic instances to run
         *
         * @param is_server_mode    True for server mode, false for client mode
         * @param quic_shards       Configured shard count from TransportConfig
         *
         * @returns Shard count to use. Always 1 for clients: a single connection is a
         *      single 4-tuple and cannot be split across sockets.
         */
        static std::size_t EffectiveShardCount(bool is_server_mode, std::size_t quic_shards);

        /**
         * @brief Build the picoquic packet loop parameters for one shard
         *
         * @param listen_port           Port the server listens on
         * @param socket_buffer_size    UDP socket buffer size
         * @param shard_count           Total number of shards
         *
         * @returns Loop parameters. With shard_count of 1 these are identical to the
         *      unsharded values, so the default configuration is unchanged.
         */
        static picoquic_packet_loop_param_t MakeLoopParams(uint16_t listen_port,
                                                           std::size_t socket_buffer_size,
                                                           std::size_t shard_count);
```

- [ ] **Step 6: Implement the helpers**

In `src/transport_picoquic.cpp`, immediately above `PicoQuicTransport::Server()`:

```cpp
std::size_t
PicoQuicTransport::EffectiveShardCount(bool is_server_mode, std::size_t quic_shards)
{
    if (!is_server_mode) {
        return 1;
    }

    return std::max<std::size_t>(1, quic_shards);
}

picoquic_packet_loop_param_t
PicoQuicTransport::MakeLoopParams(uint16_t listen_port, std::size_t socket_buffer_size, std::size_t shard_count)
{
    picoquic_packet_loop_param_t params{};

    params.local_af = PF_UNSPEC;
    params.dest_if = 0;
    params.socket_buffer_size = static_cast<int>(socket_buffer_size);
    params.do_not_use_gso = 0;
    params.extra_socket_required = 0;
    params.simulate_eio = 0;
    params.send_length_max = 0;

    if (shard_count > 1) {
        /*
         * Shards share the listen port via SO_REUSEPORT. picoquic applies the shared
         * port socket options to public_port only, never local_port, so the listen
         * port must be set there. local_port stays ephemeral and unshared.
         *
         * prefer_extra_socket makes send socket selection settle on the shared socket
         * rather than breaking on the first address family match, which would be the
         * ephemeral one.
         */
        params.local_port = 0;
        params.public_port = listen_port;
        params.is_port_shared = 1;
        params.prefer_extra_socket = 1;
    } else {
        params.local_port = listen_port;
        params.public_port = 0;
        params.is_port_shared = 0;
        params.prefer_extra_socket = 0;
    }

    return params;
}
```

Add `#include <algorithm>` to `src/transport_picoquic.cpp` if it is not already present.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `make test`
Expected: PASS, including the four new `TEST_CASE`s.

- [ ] **Step 8: Format and commit**

```bash
make format
git add include/quicr/transport.h src/transport_picoquic.h src/transport_picoquic.cpp test/transport_picoquic.cpp test/CMakeLists.txt
git commit -m "Add quic_shards config and pure shard helpers"
```

---

### Task 2: Shard struct scaffolding with a single shard

A pure mechanical refactor. Everything singular in `PicoQuicTransport` moves into a `Shard`, and exactly one shard is created. No behaviour changes; the existing test suite is the gate.

**Files:**
- Modify: `src/transport_picoquic.h`
- Modify: `src/transport_picoquic.cpp`

**Interfaces:**
- Consumes: nothing from Task 1 (the helpers stay uncalled until Task 5).
- Produces:
  - `struct PicoQuicTransport::Shard` with public members `transport`, `index`, `quic_ctx`, `thread_ctx`, `loop_params`, `loop_return_value`, `runner_queue`, `loop_prev_time`, `loop_metrics_prev_time`
  - `std::vector<std::unique_ptr<Shard>> shards_`
  - `picoquic_quic_t* PicoQuicTransport::CreateQuicInstance(uint64_t current_time);`
  - `void PicoQuicTransport::RunPqFunction(std::size_t shard_idx, std::function<int()>&& function);`
  - `int PicoQuicTransport::PqRunner(Shard& shard);`

- [ ] **Step 1: Add the Shard struct to the header**

In `src/transport_picoquic.h`, inside `class PicoQuicTransport`, in the public section immediately after the `ConnectionContext` struct definition:

```cpp
        /**
         * A single picoquic instance and the network thread that runs its packet loop
         *
         * @details Shards share the listen port via SO_REUSEPORT so the kernel spreads
         *      incoming 4-tuples across them. Each shard owns its connections outright:
         *      picoquic APIs for a connection may only be called on its shard's thread.
         */
        struct Shard
        {
            PicoQuicTransport* transport{ nullptr }; /// Owning transport
            std::size_t index{ 0 };                  /// This shard's index into shards_

            picoquic_quic_t* quic_ctx{ nullptr };                 /// Picoquic instance
            picoquic_network_thread_ctx_t* thread_ctx{ nullptr };  /// Network thread running the packet loop
            picoquic_packet_loop_param_t loop_params{};            /// Socket and loop configuration
            int loop_return_value{ 0 };                            /// Packet loop exit code

            /// Threads queue functions that picoquic will call via the pq_loop callback
            SafeQueue<std::function<int()>> runner_queue;

            uint64_t loop_prev_time{ 0 };         /// Last congestion check time
            uint64_t loop_metrics_prev_time{ 0 }; /// Last metrics sample time
        };
```

- [ ] **Step 2: Replace the singular members**

In `src/transport_picoquic.h`, delete these two public members (they sit above the `Exceptions` comment):

```cpp
        uint64_t pq_loop_prev_time = 0;
        uint64_t pq_loop_metrics_prev_time = 0;
```

and delete these private members from the `/* Variables */` block:

```cpp
        picoquic_quic_t* quic_ctx_{ nullptr };
        picoquic_network_thread_ctx_t* quic_network_thread_ctx_{ nullptr };
        picoquic_packet_loop_param_t quic_network_thread_params_{};
        int quic_loop_return_value_{ 0 };
```

and:

```cpp
        /// Threads queue functions that picoquic will call via the pq_loop callback
        SafeQueue<std::function<int()>> picoquic_runner_queue_;
```

Add in their place, in the `/* Variables */` block immediately after `local_tp_options_`:

```cpp
        std::vector<std::unique_ptr<Shard>> shards_;
```

`unique_ptr` because each `Shard*` is handed to picoquic as a callback context and must not move when the vector grows.

- [ ] **Step 3: Update the changed method signatures in the header**

Change:

```cpp
        int PqRunner();
```

to:

```cpp
        int PqRunner(Shard& shard);
```

Change:

```cpp
        void RunPqFunction(std::function<int()>&& function);
```

to:

```cpp
        void RunPqFunction(std::size_t shard_idx, std::function<int()>&& function);
```

Add next to `Shutdown()` in the private section:

```cpp
        /**
         * @brief Create and configure one picoquic instance
         *
         * @param current_time  Picoquic current time for instance creation
         *
         * @returns The created instance
         * @throws PicoQuicException if the instance could not be created
         */
        picoquic_quic_t* CreateQuicInstance(uint64_t current_time);
```

- [ ] **Step 4: Extract CreateQuicInstance**

In `src/transport_picoquic.cpp`, move the per-instance block out of `Start()` into a new `CreateQuicInstance()`. The moved lines are every statement that takes `quic_ctx_`, from `picoquic_create_and_configure` through `picoquic_set_qlog`. Replace `quic_ctx_` with the local `quic_ctx` and return it. Populating `local_tp_options_` stays behind in `Start()`; `picoquic_set_default_tp` moves.

Place it immediately above `PicoQuicTransport::Start()`:

```cpp
picoquic_quic_t*
PicoQuicTransport::CreateQuicInstance(uint64_t current_time)
{
    picoquic_quic_t* quic_ctx = nullptr;

    if (is_server_mode) {
        quic_ctx = picoquic_create_and_configure(&config_, NULL, &wt_config_->server_params, current_time, NULL);

        if (quic_ctx == NULL) {
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }

        picoquic_set_alpn_select_fn(quic_ctx, PqAlpnSelectCb);
        picoquic_use_unique_log_names(quic_ctx, 1);
    } else {
        if (transport_mode == TransportMode::kWebTransport) {
            SPDLOG_LOGGER_INFO(logger, "Client configured for WebTransport over QUIC");
            quic_ctx = picoquic_create_and_configure(&config_, NULL, NULL, current_time, NULL);
        } else {
            SPDLOG_LOGGER_INFO(logger, "Client configured for Raw QUIC");
            quic_ctx = picoquic_create_and_configure(&config_, PqEventCb, this, current_time, NULL);
        }
    }

    if (quic_ctx == NULL) {
        SPDLOG_LOGGER_CRITICAL(logger, "Unable to create picoquic context, check certificate and key filenames");
        throw PicoQuicException("Unable to create picoquic context");
    }

    if (config_.enable_sslkeylog) {
        if (std::getenv("SSLKEYLOGFILE") == nullptr) {
            SPDLOG_LOGGER_WARN(logger, "Key log enabled but $SSLKEYLOGFILE not set");
        }
        picoquic_set_key_log_file_from_env(quic_ctx);
    }

    picoquic_set_default_handshake_timeout(quic_ctx, (tconfig_.idle_timeout_ms * 1000) / 2);
    picoquic_set_default_tp(quic_ctx, &local_tp_options_);

    // Must run after set_default_tp; WebTransport requires reset_stream_at in transport parameters.
    if (is_server_mode || transport_mode == TransportMode::kWebTransport) {
        picowt_set_default_transport_parameters(quic_ctx);
    }

    picoquic_set_default_idle_timeout(quic_ctx, tconfig_.idle_timeout_ms);
    picoquic_set_default_priority(quic_ctx, 2);
    picoquic_set_default_datagram_priority(quic_ctx, 1);

    if (!tconfig_.quic_qlog_path.empty()) {
        SPDLOG_LOGGER_INFO(logger, "Enabling qlog using '{0}' path", tconfig_.quic_qlog_path);
        picoquic_set_qlog(quic_ctx, tconfig_.quic_qlog_path.c_str());
    }

    return quic_ctx;
}
```

Note the `SPDLOG_LOGGER_DEBUG(logger, "Start: As Server, configuring WebTransport Path Params")` line and the `wt_config_->path_items` / `wt_config_->server_params` setup stay in `Start()` — they are one-time, not per-instance.

- [ ] **Step 5: Create the single shard in Start()**

In `Start()`, where the extracted block used to be (after the `wt_config_->server_params` setup, before the `local_tp_options_` work), leave nothing. After the `picoquic_init_transport_parameters` / `local_tp_options_` block and before `cbNotifyQueue_.SetLimit(...)`, add:

```cpp
    auto shard = std::make_unique<Shard>();
    shard->transport = this;
    shard->index = 0;
    shard->quic_ctx = CreateQuicInstance(current_time);
    shard->runner_queue.SetLimit(tconfig_.callback_queue_size);
    shards_.push_back(std::move(shard));
```

Delete the now-dangling `picoquic_runner_queue_.SetLimit(tconfig_.callback_queue_size);` line. Leave `cbNotifyQueue_.SetLimit(...)` and the `cbNotifyThread_` start exactly as they are.

- [ ] **Step 6: Update PqLoopCb to take a Shard**

In `src/transport_picoquic.cpp`, change the top of `PqLoopCb`:

```cpp
static int
PqLoopCb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode, void* callback_ctx, void* callback_arg)
{
    auto* shard = static_cast<PicoQuicTransport::Shard*>(callback_ctx);
    int ret = 0;

    if (shard == NULL || shard->transport == NULL) {
        std::cerr << "picoquic transport was called with NULL transport" << '\n';
        return PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }

    PicoQuicTransport* transport = shard->transport;

    if (transport->Status() == TransportStatus::kDisconnected) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }

    transport->PqRunner(*shard);
```

In the `picoquic_packet_loop_time_check` case, replace the four references to the transport's timers with the shard's:

```cpp
            if (!shard->loop_prev_time) {
                shard->loop_prev_time = targ->current_time;
            }

            if (targ->current_time - shard->loop_metrics_prev_time >= transport->MetricsSampleIntervalUs()) {
                // Use this time to clean up streams that have been closed
                transport->RemoveClosedStreams();

                if (shard->loop_metrics_prev_time) {
                    transport->EmitMetrics();
                }

                shard->loop_metrics_prev_time = targ->current_time;
            }

            if (targ->current_time - shard->loop_prev_time > kCongestionCheckInterval) {

                transport->CheckConnsForCongestion();

                shard->loop_prev_time = targ->current_time;
            }
```

Leave everything else in `PqLoopCb` unchanged.

- [ ] **Step 7: Update PqRunner and RunPqFunction**

```cpp
int
PicoQuicTransport::PqRunner(Shard& shard)
{

    if (shard.runner_queue.Empty()) {
        return 0;
    }

    // note: check before running move of optional, which is more CPU taxing when empty
    while (auto cb = shard.runner_queue.Pop()) {
        try {
            if (auto ret = (*cb)()) {
                SPDLOG_LOGGER_ERROR(logger, "PQ function resulted in error: {}", ret);
                return ret;
            }
        } catch (const std::exception& e) {
            SPDLOG_LOGGER_ERROR(
              logger, "Caught exception running callback via notify thread (error={}), ignoring", e.what());
            // TODO(tievens): Add metrics to track if this happens
        }
    }

    return 0;
}
```

```cpp
void
PicoQuicTransport::RunPqFunction(std::size_t shard_idx, std::function<int()>&& function)
{
    auto& shard = *shards_.at(shard_idx);

    bool should_wake = shard.runner_queue.Empty();
    shard.runner_queue.Push(std::move(function));

    if (should_wake) {
        picoquic_wake_up_network_thread(shard.thread_ctx);
    }
}
```

- [ ] **Step 8: Update the remaining references**

Pass `0` as the shard index at all nine existing `RunPqFunction(` call sites — Task 3 replaces these with real routing. In `Server()`, `StartClient()` and `Shutdown()`, replace `quic_ctx_` with `shards_[0]->quic_ctx`, `quic_network_thread_ctx_` with `shards_[0]->thread_ctx`, `quic_network_thread_params_` with `shards_[0]->loop_params`, `quic_loop_return_value_` with `shards_[0]->loop_return_value`, and pass `shards_[0].get()` as the `loop_callback_ctx` argument to `picoquic_start_network_thread`. Replace `picoquic_runner_queue_.StopWaiting()` in `Shutdown()` with `shards_[0]->runner_queue.StopWaiting()`.

Build iteratively — the compiler will find every remaining reference:

Run: `make`
Expected: compiles clean once all references are updated.

- [ ] **Step 9: Run the tests**

Run: `make test`
Expected: PASS, unchanged from the Task 1 baseline. This is a pure refactor; any test change is a bug.

- [ ] **Step 10: Format and commit**

```bash
make format
git add src/transport_picoquic.h src/transport_picoquic.cpp
git commit -m "Move singular picoquic state into a Shard struct"
```

---

### Task 3: Route picoquic work to the owning shard

Records which shard owns each connection and routes every `RunPqFunction` call to that shard's thread. Still one shard, so still no behaviour change.

**Files:**
- Modify: `src/transport_picoquic.h` (`ConnectionContext`, private methods)
- Modify: `src/transport_picoquic.cpp`

**Interfaces:**
- Consumes: `Shard`, `shards_`, `RunPqFunction(std::size_t, std::function<int()>&&)` from Task 2.
- Produces:
  - `ConnectionContext::shard_idx` (`std::size_t`, default `0`)
  - `std::optional<std::size_t> PicoQuicTransport::GetConnShardIdx(const std::uint64_t& conn_id);`

- [ ] **Step 1: Add shard_idx to ConnectionContext**

In `src/transport_picoquic.h`, in `struct ConnectionContext`, immediately after `uint64_t last_stream_id{ 0 };`:

```cpp
            std::size_t shard_idx{ 0 };       /// Shard whose thread owns this connection
```

- [ ] **Step 2: Declare the lookup helper**

In `src/transport_picoquic.h`, in the private section next to `DeleteDataContextInternal`:

```cpp
        /**
         * @brief Find the shard that owns a connection
         *
         * @param conn_id   Connection ID to look up
         *
         * @returns Shard index, or nullopt if the connection is not known
         *
         * @warning Takes state_mutex_. state_mutex_ is not recursive, so this must never
         *      be called while it is held. Callers that already hold it have the
         *      connection context in hand and should read conn_ctx.shard_idx directly.
         */
        std::optional<std::size_t> GetConnShardIdx(const std::uint64_t& conn_id);
```

- [ ] **Step 3: Implement the lookup helper**

In `src/transport_picoquic.cpp`, immediately after `PicoQuicTransport::GetConnContext`:

```cpp
std::optional<std::size_t>
PicoQuicTransport::GetConnShardIdx(const std::uint64_t& conn_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return std::nullopt;
    }

    return conn_it->second.shard_idx;
}
```

- [ ] **Step 4: Record the owning shard on connection creation**

In `CreateConnContext`, after `conn_ctx.pq_cnx = pq_cnx;`:

```cpp
    // Record which shard's thread owns this connection. picoquic APIs for this
    // connection may only be called on that thread.
    const auto* cnx_quic_ctx = picoquic_get_quic_ctx(pq_cnx);
    for (const auto& shard : shards_) {
        if (shard->quic_ctx == cnx_quic_ctx) {
            conn_ctx.shard_idx = shard->index;
            break;
        }
    }
```

- [ ] **Step 5: Route the call sites that hold the connection context**

In `Enqueue`, both `RunPqFunction` calls already run under `state_mutex_` with `conn_ctx_it` in hand. Replace the `0` placeholders from Task 2:

```cpp
            RunPqFunction(conn_ctx_it->second.shard_idx, [this, conn_id, data_ctx_id, stream_id]() {
                MarkStreamActive(conn_id, data_ctx_id, stream_id);
                return 0;
            });
```

and:

```cpp
            RunPqFunction(conn_ctx_it->second.shard_idx, [this, conn_id]() {
                MarkDgramReady(conn_id);
                return 0;
            });
```

In `SendStreamBytes`, the reset-wait branch holds `state_mutex_` and has `conn_ctx`:

```cpp
                        RunPqFunction(
                          conn_ctx->shard_idx,
                          [this, conn_id = data_ctx->conn_id, data_ctx_id = data_ctx->data_ctx_id, stream_id]() {
                              MarkStreamActive(conn_id, data_ctx_id, stream_id);
                              return 0;
                          });
```

- [ ] **Step 6: Route the call sites that hold only a conn_id**

For the remaining call sites that have a `conn_id` but do not hold `state_mutex_` — `Close`, `CreateStream`, the public `CloseStream`, and the data-context teardown path — look the shard up first. `Close` becomes:

```cpp
void
PicoQuicTransport::Close(const std::uint64_t& conn_id, AppReasonForClose app_reason)
{
    const auto shard_idx = GetConnShardIdx(conn_id);
    if (!shard_idx.has_value()) {
        return;
    }

    RunPqFunction(*shard_idx, [=, this]() {
        CloseInternal(conn_id, app_reason);

        return 0;
    });
}
```

`DeleteDataContext` — insert directly above its `RunPqFunction`, after the existing race-condition comment:

```cpp
    const auto shard_idx = GetConnShardIdx(conn_id);
    if (!shard_idx.has_value()) {
        return;
    }

    RunPqFunction(*shard_idx, [=, this]() {
        DeleteDataContextInternal(conn_id, data_ctx_id, delete_on_empty);
        return 0;
    });
```

`CreateStream` — throws rather than returning, matching `CreateStreamInternal`, which already throws `PicoQuicException` when the connection is missing:

```cpp
    const auto shard_idx = GetConnShardIdx(conn_id);
    if (!shard_idx.has_value()) {
        throw PicoQuicException("Unable to find connection context");
    }

    RunPqFunction(*shard_idx, [this, conn_id = conn_id, data_ctx_id = data_ctx_id, state, priority]() {
```

The public `CloseStream`:

```cpp
    const auto shard_idx = GetConnShardIdx(conn_id);
    if (!shard_idx.has_value()) {
        return;
    }

    RunPqFunction(*shard_idx, [this, conn_id = conn_id, data_ctx_id = data_ctx_id, stream_id, use_reset]() {
```

`SendNextDatagram` is the exception in this group: it runs on the picoquic thread and already has `conn_ctx` in hand, so it needs no lookup and must not take one — use the field directly:

```cpp
            RunPqFunction(conn_ctx->shard_idx, [this, conn_id = conn_ctx->conn_id]() {
                MarkDgramReady(conn_id);
                return 0;
            });
```

`StartClient`'s `RunPqFunction` has no connection yet and stays at shard `0`. Add a brief comment saying clients only ever have one shard.

That is all nine call sites: two in `Enqueue` and one in `SendStreamBytes` from Step 5; `Close`, `DeleteDataContext`, `CreateStream`, `CloseStream` and `SendNextDatagram` here; and `StartClient`.

- [ ] **Step 7: Build and test**

Run: `make test`
Expected: PASS, unchanged. Still one shard, so routing always resolves to `0`.

- [ ] **Step 8: Format and commit**

```bash
make format
git add src/transport_picoquic.h src/transport_picoquic.cpp
git commit -m "Route picoquic work to the shard owning the connection"
```

---

### Task 4: Per-shard periodic work and locking fixes

Scopes the three `time_check` jobs to their own shard's connections, and fixes two places that become unsafe once more than one thread touches `conn_context_`.

**Files:**
- Modify: `src/transport_picoquic.h`
- Modify: `src/transport_picoquic.cpp`

**Interfaces:**
- Consumes: `ConnectionContext::shard_idx` from Task 3.
- Produces:
  - `void PicoQuicTransport::EmitMetrics(std::size_t shard_idx);`
  - `void PicoQuicTransport::RemoveClosedStreams(std::size_t shard_idx);`
  - `void PicoQuicTransport::CheckConnsForCongestion(std::size_t shard_idx);`

- [ ] **Step 1: Add the shard parameter to the three declarations**

In `src/transport_picoquic.h`, change:

```cpp
        void CheckConnsForCongestion();
        void EmitMetrics();
        void RemoveClosedStreams();
```

to:

```cpp
        void CheckConnsForCongestion(std::size_t shard_idx);
        void EmitMetrics(std::size_t shard_idx);
        void RemoveClosedStreams(std::size_t shard_idx);
```

- [ ] **Step 2: Filter each job to its own shard**

In `src/transport_picoquic.cpp`, add the parameter to each definition and skip connections belonging to other shards. In all three, immediately inside the `for (auto& [conn_id, conn_ctx] : conn_context_)` loop:

```cpp
        if (conn_ctx.shard_idx != shard_idx) {
            continue;
        }
```

`CheckConnsForCongestion` needs this for correctness, not just efficiency: it calls `picoquic_get_path_quality(conn_ctx.pq_cnx, ...)`, which would otherwise touch another instance's connection from the wrong thread. `EmitMetrics` needs it to avoid emitting each connection's metrics once per shard. `RemoveClosedStreams` needs it to keep each shard's work proportional to its own connections.

- [ ] **Step 3: Fix the missing lock in EmitMetrics**

`EmitMetrics` iterates `conn_context_` while taking no lock at all. It already races with `state_mutex_`-guarded mutation from application threads; with N shard threads it becomes far more likely to bite. Add as the first line of the function body, matching `RemoveClosedStreams` and `CheckConnsForCongestion`:

```cpp
    std::lock_guard<std::mutex> _(state_mutex_);
```

- [ ] **Step 4: Fix the connection map race in CreateConnContext**

`CreateConnContext` carries this comment:

```cpp
    /*
     * @note: This is thread safe because picoquic network thread is the only one that calls this
     */
```

That premise is false once there is more than one network thread — N threads would call `conn_context_.emplace` on the same map concurrently. Replace the comment and take the lock:

```cpp
    /*
     * @note: Called from each shard's picoquic network thread. With more than one shard
     *      there is more than one such thread, so the connection map must be locked.
     */
    std::lock_guard<std::mutex> _(state_mutex_);
```

Then verify by inspection that no caller of `CreateConnContext` already holds `state_mutex_` — `state_mutex_` is not recursive. The three call sites are in `PqEventCb`, `StartClient` and the WebTransport connection path.

- [ ] **Step 5: Pass the shard index from the loop callback**

In `PqLoopCb`'s `picoquic_packet_loop_time_check` case, pass the shard's index to each:

```cpp
                transport->RemoveClosedStreams(shard->index);

                if (shard->loop_metrics_prev_time) {
                    transport->EmitMetrics(shard->index);
                }
```

and:

```cpp
                transport->CheckConnsForCongestion(shard->index);
```

- [ ] **Step 6: Build and test**

Run: `make test`
Expected: PASS, unchanged. With one shard the filter never skips anything.

- [ ] **Step 7: Format and commit**

```bash
make format
git add src/transport_picoquic.h src/transport_picoquic.cpp
git commit -m "Scope periodic picoquic work to the owning shard"
```

---

### Task 5: Create and shut down N shards

The flip. Creates `EffectiveShardCount()` instances and threads, and aggregates shutdown across them.

**Files:**
- Modify: `src/transport_picoquic.cpp`

**Interfaces:**
- Consumes: `EffectiveShardCount` and `MakeLoopParams` from Task 1; `Shard`/`shards_` from Task 2.
- Produces: a `PicoQuicTransport` running N picoquic instances when `TransportConfig::quic_shards > 1` in server mode.

- [ ] **Step 1: Create N shards in Start()**

Replace the single-shard block from Task 2 Step 5 with:

```cpp
    const auto shard_count = EffectiveShardCount(is_server_mode, tconfig_.quic_shards);

    if (shard_count > 1) {
        SPDLOG_LOGGER_INFO(logger, "Starting {0} QUIC shards sharing the listen port", shard_count);
    }

    for (std::size_t i = 0; i < shard_count; i++) {
        auto shard = std::make_unique<Shard>();
        shard->transport = this;
        shard->index = i;
        shard->quic_ctx = CreateQuicInstance(current_time);
        shard->runner_queue.SetLimit(tconfig_.callback_queue_size);
        shards_.push_back(std::move(shard));
    }
```

- [ ] **Step 2: Start a network thread per shard**

Replace the body of `Server()` with a loop. Note that `MakeLoopParams` now supplies every field the old code set by hand:

```cpp
void
PicoQuicTransport::Server()
{
    for (auto& shard : shards_) {
        shard->loop_params = MakeLoopParams(serverInfo_.port, tconfig_.socket_buffer_size, shards_.size());

        SPDLOG_LOGGER_DEBUG(logger, "Starting picoquic network thread for shard {0}", shard->index);
        shard->thread_ctx = picoquic_start_network_thread(
          shard->quic_ctx, &shard->loop_params, PqLoopCb, shard.get(), &shard->loop_return_value);

        if (shard->quic_ctx == NULL || shard->thread_ctx == NULL) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to start picoquic network thread for shard {0}", shard->index);
            SetStatus(TransportStatus::kShutdown);
            return;
        }

        // Wait for something to happen with the thread
        while (!shard->thread_ctx->thread_is_ready && !shard->thread_ctx->return_code) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (shard->thread_ctx->return_code) {
            SPDLOG_LOGGER_ERROR(logger,
                                "Could not start quic network thread for shard {0} error: {1}",
                                shard->index,
                                shard->thread_ctx->return_code);
            SetStatus(TransportStatus::kShutdown);
            return;
        }
    }
}
```

The old code called `picoquic_free(quic_ctx_)` on thread start failure. Drop that here: `Shutdown()` frees every shard's instance, and freeing one here would leave a dangling pointer for it to free again.

**The failure path fails the whole transport, and that is deliberate.** The `SO_REUSEPORT` group must stay fixed for the process lifetime: Linux rehashes the group whenever the set of listening sockets changes, so rebinding one shard's socket would break live connections on every *other* shard. Never retry or respawn an individual shard — set `kShutdown` and let the normal shutdown path run.

`StartClient()` keeps using `shards_[0]` — clients always have exactly one shard.

- [ ] **Step 3: Shut down every shard**

In `Shutdown()`, replace the single-thread block with a loop over shards, keeping the existing sequence for each:

```cpp
    for (auto& shard : shards_) {
        if (shard->thread_ctx != NULL) {
            SPDLOG_LOGGER_INFO(logger, "Closing transport picoquic thread for shard {0}", shard->index);
            picoquic_wake_up_network_thread(shard->thread_ctx);

            while (shard->thread_ctx->thread_is_ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            picoquic_delete_network_thread(shard->thread_ctx);
            shard->thread_ctx = nullptr;
        }

        shard->runner_queue.StopWaiting();
    }
```

and replace the `picoquic_free(quic_ctx_)` block, which sits after the `cbNotifyThread_` join, with:

```cpp
    for (auto& shard : shards_) {
        if (shard->quic_ctx != nullptr) {
            picoquic_free(shard->quic_ctx);
            shard->quic_ctx = nullptr;
        }
    }

    shards_.clear();
```

- [ ] **Step 4: Aggregate the shutdown status across shards**

`PqLoopCb`'s `picoquic_packet_loop_wake_up` case currently drives the global status from a single loop. Each shard must only report its own instance drained, and the transport may only reach `kShutdown` once every shard has. Replace the `kShuttingDown` branch:

```cpp
            if (transport->Status() == TransportStatus::kShuttingDown) {
                SPDLOG_LOGGER_INFO(transport->logger, "picoquic is shutting down shard {0}", shard->index);

                picoquic_cnx_t* close_cnx = picoquic_get_first_cnx(quic);

                while (close_cnx != NULL) {
                    SPDLOG_LOGGER_INFO(
                      transport->logger, "Closing connection id {0}", reinterpret_cast<uint64_t>(close_cnx));
                    transport->CloseInternal(reinterpret_cast<uint64_t>(close_cnx), AppReasonForClose::kShutdown);
                    close_cnx = picoquic_get_next_cnx(close_cnx);
                }

                return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }
```

The status no longer advances to `kShutdown` from inside the loop callback. `Shutdown()` already waits for every shard's thread to clear `thread_is_ready` in Step 3, which is the real completion signal; set the status once after that loop:

```cpp
    SetStatus(TransportStatus::kShutdown);
```

- [ ] **Step 5: Build and run the existing tests at the default**

Run: `make test`
Expected: PASS, unchanged. The default `quic_shards` of 1 must behave exactly as before — this is the global no-op constraint.

- [ ] **Step 6: Format and commit**

```bash
make format
git add src/transport_picoquic.cpp
git commit -m "Create and shut down a picoquic instance per shard"
```

---

### Task 6: Verify sharding under test

**Files:**
- Modify: `test/integration_test/integration_test.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces: an integration test exercising a sharded server.

- [ ] **Step 1: Add a shard count option to the test server factory**

In `test/integration_test/integration_test.cpp`, extend `MakeTestServer` with a fourth optional parameter, following the existing pattern exactly:

```cpp
static std::shared_ptr<TestServer>
MakeTestServer(const std::optional<std::string>& qlog_path = std::nullopt,
               std::optional<std::size_t> max_connections = std::nullopt,
               std::optional<std::uint64_t> initial_max_stream_data = std::nullopt,
               std::optional<std::size_t> quic_shards = std::nullopt)
```

and inside, alongside the other optional assignments:

```cpp
    if (quic_shards.has_value()) {
        server_config.transport_config.quic_shards = *quic_shards;
    }
```

- [ ] **Step 2: Write the sharded test**

Add a `TEST_CASE` following the conventions of the existing cases in the file — use `MakeTestServer` and `MakeTestClient`, and drive a publish/subscribe exchange exactly as the nearest existing case does:

Add this after `TEST_CASE("Integration - Subscribe")`, on which it is modelled. It drives a full subscribe roundtrip so the test proves data flows through a sharded server, not merely that a connection is established:

```cpp
TEST_CASE("Integration - Sharded server")
{
    auto server = MakeTestServer(std::nullopt, std::nullopt, std::nullopt, 4);

    auto test_sharded = [&](const std::string& protocol_scheme) {
        auto client = MakeTestClient(true, std::nullopt, protocol_scheme);

        // Make a subscription.
        FullTrackName ftn;
        ftn.name_space = TrackNamespace({ "namespace" });
        ftn.name = { 1, 2, 3 };
        const messages::Filter filter = messages::TrackFilter{ 1, 2, 3 };
        const auto handler = TestSubscribeHandler::Create(ftn, 0, std::nullopt, filter);

        // When we subscribe, server should receive a subscribe.
        std::promise<TestServer::SubscribeDetails> promise;
        std::future<TestServer::SubscribeDetails> future = promise.get_future();
        server->SetSubscribePromise(std::move(promise));

        // Subscribe.
        CHECK_NOTHROW(client->SubscribeTrack(handler));

        // Server should receive the subscribe.
        auto status = future.wait_for(kDefaultTimeout);
        REQUIRE(status == std::future_status::ready);
        const auto& details = future.get();
        CHECK_EQ(details.track_full_name.name, ftn.name);
        CHECK_EQ(details.track_full_name.name_space, ftn.name_space);

        // Server should respond, track should go live.
        const bool track_live =
          WaitFor([&handler]() { return handler->GetStatus() == SubscribeTrackHandler::Status::kOk; });
        CHECK(track_live);

        // Test is complete, unsubscribe while we are connected.
        CHECK_NOTHROW(client->UnsubscribeTrack(handler));

        // Check track handler cleanup / strong reference cycles.
        CHECK_EQ(handler.use_count(), 1);
    };

    SUBCASE("Raw QUIC")
    {
        CAPTURE("Raw QUIC");
        test_sharded("moq");
    }

    SUBCASE("WebTransport")
    {
        CAPTURE("WebTransport");
        test_sharded("https");
    }
}
```

On macOS this exercises the sharded code path with all traffic landing on one shard; on Linux it exercises real distribution. Both are useful — the test is about correctness, not parallelism.

- [ ] **Step 3: Run the tests**

Run: `make test`
Expected: PASS, including the new case.

- [ ] **Step 4: Run the suite under picoquic's thread checker**

picoquic ships `WITH_THREAD_CHECK`, which `debugbreak()`s on any picoquic API call made from the wrong thread. `doc/parallel.md` names relay-style cross-connection fan-out as the canonical way to get this wrong, so this is the primary check on the Task 3 routing.

```bash
cmake -B build-threadcheck -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DQUICR_BUILD_TESTS=ON -DQUICR_BUILD_EXAMPLES=OFF -DWITH_THREAD_CHECK=ON .
```

```bash
cmake --build build-threadcheck --parallel 8
```

Copy the certificates into the new build tree as `make cert` does, then:

```bash
ctest --test-dir build-threadcheck --output-on-failure
```

Expected: PASS with no debug break. A break means a picoquic API is being called from a thread that does not own the connection — fix the routing, do not work around the check.

- [ ] **Step 5: Commit**

```bash
make format
git add test/integration_test/integration_test.cpp
git commit -m "Add integration coverage for a sharded server"
```

---

### Task 7: Update the threading documentation

**Files:**
- Modify: `docs/implementation.md:107-119`

- [ ] **Step 1: Correct the thread count**

`docs/implementation.md` states *"Three threads are used by libquicr"*, the first being the single picoquic event loop thread. Update that section to say the transport runs one picoquic event loop thread **per shard**, defaulting to one, configured by `TransportConfig::quic_shards`, and that each shard owns its connections — picoquic APIs for a connection may only be called on its shard's thread. Keep the existing wording and structure; change only what is now inaccurate.

- [ ] **Step 2: Commit**

```bash
git add docs/implementation.md
git commit -m "Document per-shard picoquic event loop threads"
```

---

## Verification before opening the PR

- [ ] `make test` passes.
- [ ] The thread-check build from Task 6 Step 4 passes with no debug break.
- [ ] `make format` leaves the tree clean (`git diff --exit-code`).
- [ ] A `-DLINT=ON` build passes, since clang-tidy naming violations are CI errors:

```bash
cmake -B build-lint -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DQUICR_BUILD_TESTS=ON -DLINT=ON . && cmake --build build-lint --parallel 8
```

- [ ] `git diff main --stat` shows changes confined to: `include/quicr/transport.h`, `src/transport_picoquic.{h,cpp}`, `test/transport_picoquic.cpp`, `test/CMakeLists.txt`, `test/integration_test/integration_test.cpp`, `docs/`.

## Benchmarking, after merge

Sharding is a performance change and is unproven until measured. Run an N-way mesh at `quic_shards` of 1 versus the relay's core count, and record relay CPU and throughput. Two known follow-on bottlenecks are documented in the spec's "Deferred work" and are expected to surface once `sendmsg` stops being the limit: `Enqueue` holding `state_mutex_` for its whole body, and the single `cbNotifyThread_` running all delegate callbacks. Measure before picking the next one.
