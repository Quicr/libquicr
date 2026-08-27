# Stream Context Usage: Analysis and Path Forward

## Summary of what I found

The system currently gives picoquic a `PicoQuicDataContext*` as the per-stream app context, then re-derives the actual per-stream state (`PicoQuicDataContext::StreamContext`) from a `std::map<uint64_t, StreamContext>` lookup on *every single* callback. Since a data context owns many streams (one per subgroup), that map lookup is unavoidable today — the pointer picoquic hands back is too coarse to identify the stream.

Switching picoquic's app stream context to point at the per-stream object is the right move, and it's worth more than the map lookups alone: it also removes a global mutex acquisition per received chunk, turns one O(N_streams) per-callback loop into O(1), and closes a real data race.

Following that thread to its conclusion, **the data context stops having a reason to exist.** Once the stream is a first-class object, every remaining responsibility of `DataContext` either belongs on the stream, belongs on the track handler, or is dead code. Removing it is therefore an explicit goal of this work rather than a side effect — see §5 and Phase 5.

---

## 1. Where stream state actually lives

There are **three** parallel registries keyed by `stream_id`, split across two owners:

```cpp
// src/picoquic_connection.h:118-119
        std::map<std::uint64_t, StreamContext> streams;
        std::mutex stream_mutex;
```

```cpp
// src/picoquic_connection.h:207-215
        /// Map of stream receive buffers, key is stream_id
        std::map<std::uint64_t, RxStreamBuffer> rx_stream_buffer;

        /**
         * WebTransport stream ID to data context mapping
         * Used in WebTransport mode to look up data context for a stream. The mapping does not own the
         * context; contexts are owned by active_data_contexts.
         */
        std::map<std::uint64_t, std::weak_ptr<PicoQuicDataContext>> wt_stream_to_data_ctx;
```

TX state hangs off the *data context*; RX state hangs off the *connection*. A bidirectional stream therefore has entries in both, with no link between them. Above the transport there are two more: `PublishTrackHandler::stream_info_by_group_` (a two-level map ending in a `stream_id`) and `SubscribeTrackHandler::streams_` (parse state per `stream_id`).

## 2. The per-object lookup budget today

**Transmitting one object on a subgroup stream** (native QUIC):

| Step | Cost |
|---|---|
| `stream_info_by_group_[group][subgroup]` | 2 tree lookups, app thread |
| `Transport::Enqueue` | acquires the transport-wide `state_mutex_` |
| `Enqueue` → `streams.find(stream_id)` | tree lookup |
| `MarkStreamActive` → `streams.find(stream_id)` | tree lookup, picoquic thread |
| `PqEventCb` → `GetConnection(conn_id)` | tree lookup, per callback |
| `SendStreamBytes` → `streams.find(stream_id)` | tree lookup, per callback |
| `CheckCallbackDelta` | **O(active subgroups)**, per callback |

That last one is the outlier. Every `prepare_to_send` callback walks every stream on the data context and takes two locks per stream:

```cpp
// src/transport_picoquic.cpp:2727-2744
    std::lock_guard _(data_ctx->stream_mutex); // TODO: This doesn't seem to help.

    for (auto& [_, stream] : data_ctx->streams) {
        if (stream.last_tx_tick == 0) {
            stream.last_tx_tick = current_tick;
            continue;
        }

        const auto delta_ms = current_tick - stream.last_tx_tick;
        stream.last_tx_tick = current_tick;

        data_ctx->metrics.tx_callback_ms.AddValue(delta_ms);

        std::lock_guard __(*stream.tx_data);
        if (stream.priority > 0 && delta_ms > 50 && stream.tx_data->Size() >= 20) {
            data_ctx->metrics.tx_delayed_callback++;
        }
    }
```

A publisher with 30 concurrent subgroups pays 30 iterations and 60 lock operations per send opportunity, to update one stream's tick. With a per-stream context this is straight-line O(1) code.

**Receiving one chunk:**

| Step | Cost |
|---|---|
| `OnRecvStreamBytes` → `rx_stream_buffer.find` | tree lookup, picoquic thread |
| `Session::OnRecvStream` → `GetStreamRxContext` | **`state_mutex_`** + tree lookup, notify thread |
| `std::any_cast` on `caller_any` + `weak_ptr::lock` | typeid compare, per chunk |
| `SubscribeTrackHandler::streams_.find` | tree lookup, per chunk |

The second row is the notable one. The picoquic thread already holds `rx_buf.rx_ctx` when it queues the notification, then throws it away:

```cpp
// src/transport_picoquic.cpp:2189-2192
        if (rx_buf.rx_ctx->data_queue.Size() < 10 &&
            // Capturing the handle keeps the context alive until the notification is delivered.
            !cbNotifyQueue_.Push(
              [=, this, handle = data_ctx]() { connection->OnRecvStream(stream_id, handle, (stream_id & 2) == 0); })) {
```

The notify thread then re-acquires it through the transport's *global* mutex:

```cpp
// src/transport_picoquic.cpp:1237-1240
std::shared_ptr<StreamRxContext>
PicoQuicTransport::GetStreamRxContext(const std::shared_ptr<Connection>& connection, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);
```

So every received chunk contends the notify thread against the picoquic thread on `state_mutex_`, to recover a pointer that was in hand microseconds earlier. Capturing `rx_ctx` in that lambda is a one-line fix with no API impact and should land first, independently of everything else.

## 3. Three correctness problems the refactor should absorb

**A data race on `data_ctx->streams`.** `Enqueue` reads the map under `state_mutex_` from application threads, but `EraseStreamState` mutates it from the picoquic thread with no lock at all:

```cpp
// src/transport_picoquic.cpp:2949-2958
    if (data_ctx) {
        const auto stream_it = data_ctx->streams.find(stream_id);
        if (stream_it != data_ctx->streams.end()) {
            if (connection->GetAPI() == Connection::API::kWebTransport) {
                if (stream_it->second.wt_stream_ctx && connection->wt_h3_ctx) {
                    h3zero_delete_stream(connection->pq_cnx, connection->wt_h3_ctx, stream_it->second.wt_stream_ctx);
                }
            }
            data_ctx->streams.erase(stream_it);
        }
    }
```

`stream_mutex` was evidently an attempt at this, but `CheckCallbackDelta` is its only taker — hence the `// TODO: This doesn't seem to help.` The locking model for this container is not coherent today, and the refactor is the natural place to make it so.

**`SendStreamBytes` holds a reference into a map it may erase.** `stream_ctx` is a reference to the map node, and the deferred block can reach `EraseStreamState` and destroy it. The ordering happens to work (the `lock_guard` on `stream_ctx.tx_data` destructs before the `defer`, and nothing touches `stream_ctx` after the erase), and the comment at line 1839 shows this was reasoned about — but it's held together by destruction order rather than by ownership.

**`picoquic_callback_stream_released` is unhandled.** `picoquic_delete_stream` does not clear `app_stream_ctx`; `stream_released` is picoquic's signal that it is done with that pointer. Today the pointer is the long-lived, refcounted data context, so ignoring it is benign. Once the pointer becomes per-stream, this callback becomes the definitive release point and must be handled.

## 4. What the data context is actually for

`DataContext` serves three distinct roles today, and they have very different justifications.

**Role A — one context per bidirectional request stream (1:1).** This pattern dominates `session.cpp`:

```cpp
// src/session.cpp:693-696
        const auto data_ctx = quic_transport_->CreateDataContext(current_connection_, true, 0, true);
        ...
        request_id_by_data_ctx[data_ctx->GetID()] = handler->GetRequestId().value();
```

The same shape repeats at lines 860, 886, 1280, 1343, 1414, plus 249/1499 for the control stream and `CreateDataContextBiDirRecv` for inbound ones. Every MOQ request stream — subscribe, fetch, publish-namespace, subscribe-namespace, control — allocates a data context that will own exactly one stream, forever. The context exists only to give that stream a name for `request_id_by_data_ctx` to key on. This is already pure overhead, and it is what forces the special case in `Enqueue`:

```cpp
// src/transport_picoquic.cpp:1164-1170
        if (data_ctx->IsBidir() || (stream_id == 0 && priority == 0)) {
            if (streams.empty()) {
                return TransportError::kInvalidStreamId;
            }

            stream_id = streams.begin()->first;
            stream_it = streams.begin();
```

`SendCtrlMsg` passes `0` as the stream id and lets `Enqueue` guess "the only stream."

**Role B — one context per published track (1:N with subgroup streams).** Lines 1308–1315, 2234–2241, 2318. This is the case that appears to justify the abstraction: metrics rollup, `delete_on_empty`, and the reset-wait policy all nominally live here.

**Role C — datagram flows (no streams at all).** A datagram track creates a context with `use_reliable=false` and never opens a stream.

### Each justification, tested

**Reset-wait is abandoned.** The only flow-level *policy* the data context carried was gated on a config flag that libquicr's own callers all set false: `examples/qclient/client.cpp` and `c-bridge`'s `qbridge_client_config_init`.

It was *not*, as originally written here, unreachable. Decimus exposes it as a user-facing toggle (`useResetWaitCC` in `TransportConfigSettings.swift`), passed through `QClientObjC.mm` into `use_reset_wait_strategy`, and records `tx_reset_wait` per track in `TrackMeasurement.swift`. The strategy was reachable by anyone who flipped that switch.

Removed anyway, as a deliberate decision to drop the feature rather than carry it through the refactor. `Config::use_reset_wait_strategy`, the `tx_reset_wait` metric, and the c-bridge field are all gone, so Decimus needs a corresponding API update.

**Metrics are per-stream quantities.** Every field in `QuicDataContextMetrics` — `enqueued_objs`, `rx_stream_cb`, `rx_stream_bytes`, `tx_buffer_drops`, `tx_queue_discards`, `tx_queue_expired`, `tx_delayed_callback`, `tx_queue_size`, `tx_callback_ms`, `tx_object_duration_us`, `tx_stream_cb`, `tx_stream_objects`, `tx_stream_bytes` — is produced by a single stream. Only `tx_dgrams` and `tx_dgrams_bytes` are not.

Most of them are also never read. Only seven reach a handler: `rx_stream_bytes` (subscribe) and `tx_buffer_drops`, `tx_callback_ms`, `tx_delayed_callback`, `tx_object_duration_us`, `tx_queue_discards`, `tx_queue_expired`, `tx_queue_size` (publish). `enqueued_objs`, `rx_stream_cb`, `tx_stream_cb`, `tx_stream_objects`, `tx_stream_bytes`, `tx_dgrams`, and `tx_dgrams_bytes` are written and never surfaced.

Worse, several are actively *wrong* at data-context scope. `tx_queue_size`, `tx_callback_ms`, and `tx_object_duration_us` are `MinMaxAvg` aggregates fed from a loop over all streams, so a track with 30 subgroups blends 30 unrelated queues into one min/max. Per-stream collection is more correct, not merely faster.

The real question is who accumulates across a track's many short-lived subgroup streams — and the answer is the **track handler**, which is already per-track, long-lived, application-owned, and holding every stream in `stream_info_by_group_`. `request_id_by_data_ctx` exists solely to bridge from a transport-owned per-track object back to the handler that should have owned it in the first place.

**`delete_on_empty` already reduces to a per-stream flag.** `StreamContext::close_on_empty` exists today, and `DeleteDataContextInternal`'s delete-on-empty branch amounts to "mark all my streams active so they drain, then tear down." With handles, that is the handler setting `close_on_empty` on the streams it already holds.

**Datagrams are the only genuine non-stream case — and do not even need a tag.** `SendNextDatagram` recovers the context via `ConnData::data_ctx_id` purely to attribute two counters, and those two counters are never surfaced to a handler. The datagram queue is already per-*connection* (`dgram_tx_data`), not per-context, and `Dequeue` ignores its `data_ctx` parameter entirely. So the datagram channel needs neither an object with a registration lifecycle nor a per-track tag — just counters on the connection.

### Conclusion

Nothing `DataContext` does requires `DataContext`. It should be removed and replaced by two narrower things:

1. **The stream owns its own metrics**, sampled while open and rolled up to the owning handler on close. Per-track aggregation happens in the handler, where per-track identity already lives.
2. **One datagram channel per connection**, owning `tx_dgrams`/`tx_dgrams_bytes` alongside the queue that is already there. No per-track attribution, because none of those counters is surfaced per track.

What survives from `DataContext` is `is_bidir` and a priority, both of which are stream *creation parameters* rather than state, and the `IsRegistered()` guard, which becomes the stream's `IsOpen()`.

## 5. Proposed target model

Promote `StreamContext` to a first-class, shared-owned object that knows its own identity, and make *it* the thing picoquic holds:

```cpp
class PicoQuicStream : public std::enable_shared_from_this<PicoQuicStream>
{
  public:
    std::uint64_t GetStreamId() const noexcept { return stream_id_; }
    bool IsOpen() const noexcept { return open_.load(std::memory_order_acquire); }

    // existing TX state: tx_data, tx_object, tx_object_offset, priority,
    //                    close_on_empty, close_using_reset, last_tx_tick
    // existing RX state: rx_ctx (StreamRxContext), closed, checked_once
    // WebTransport:      wt_stream_ctx
    // metrics:           per-stream counters, rolled up to the owner on close

  private:
    std::uint64_t stream_id_;
    std::weak_ptr<PicoQuicConnection> conn_;
    std::atomic_bool open_{ true };
};
```

Note the absence of a back-pointer to a data context: per §4 there is nothing on the far end of it worth reaching. The owner is the track handler, reached through the same weak-handle pattern already used elsewhere.

`IsOpen()` is the per-stream analogue of the `IsRegistered()` flag added in `01a74f6b`, and replaces the coarser `IsDataContextActive()` gate in `Enqueue`. Keeping `stream_id_` on the object matters: every picoquic call still needs the raw id, and it keeps every existing log line and public callback signature intact.

The safety contract mirrors the one already established for data contexts, at stream granularity. Handing a raw pointer across the FFI boundary into picoquic means a use-after-free is the failure mode, so the invariants need to be explicit:

1. The owning `shared_ptr` lives in exactly one map, mutated only on the picoquic thread.
2. `PqEventCb` promotes the incoming `void*` via `shared_from_this()` before use, exactly as it does today for the data context — an in-flight callback then cannot be invalidated by a concurrent erase.
3. Teardown order is fixed: clear picoquic's pointer (`picoquic_unlink_app_stream_ctx` / `picoquic_reset_stream_ctx`), then set `open_ = false`, then erase from the map.
4. Application threads never touch the map. They hold a handle obtained at stream creation and only touch `tx_data` (already internally locked) and atomics.

Point 4 is what makes the race in §3 disappear rather than get papered over: the map stops being shared-mutable state.

Two picoquic behaviours to design around, both verified in the vendored source. `picoquic_mark_active_stream` always rewrites `app_stream_ctx` (it calls the internal helper with `do_update_ctx=1`), so it must be passed the stream pointer consistently — including the `mark_active_stream(..., 0, NULL)` on connection close, which conveniently nulls it. And `picoquic_add_to_stream` erases the ctx as a documented side effect, which the FIN path in `CloseStream` relies on.

## 6. Phasing

**Phase 0 — extract the type.** Move `StreamContext` out of `PicoQuicDataContext`, add the identity fields, change the container to `std::map<uint64_t, std::shared_ptr<PicoQuicStream>>`. Purely mechanical, no API change, no behavior change. Do the `EraseStreamState` locking fix here.

**Phase 1 — make it picoquic's app stream context (native QUIC).** Pass `stream.get()` from `CreateStreamInternal` and `MarkStreamActive`; cast `v_stream_ctx` to `PicoQuicStream*` in `PqEventCb`; change `SendStreamBytes` to take the stream handle. Handle `picoquic_callback_stream_released`. This alone removes the `SendStreamBytes` and `MarkStreamActive` lookups and makes `CheckCallbackDelta` O(1). Contained entirely within the transport.

**Phase 2 — the cheap RX win.** Capture `rx_ctx` in the `OnRecvStreamBytes` notify lambda and add an overload so `Session::OnRecvStream` receives it directly. Deletes a `state_mutex_` acquisition per chunk. Independent of Phases 0–1; can ship first.

**Phase 3 — hand stream handles up the stack.** `Transport::CreateStream` returns a handle; `Enqueue` and `CloseStream` take one. `PublishTrackHandler::StreamInfo`, `PublishFetchHandler::stream_id_`, and `TrackHandler::request_stream_id_` store handles.

The handle type is a new public `quicr::Stream` base (`include/quicr/stream.h`) that `PicoQuicStream` derives from, mirroring the existing `Connection` / `PicoQuicConnection` split. It exposes only `GetStreamId`, `GetConnectionID`, and `IsOpen`; all transport state stays in the derived class.

`IsOpen` becomes load-bearing in this phase. Once a holder keeps a handle for the life of a subgroup, the handle outlives removal from the transport's map, so the lookup that used to double as a liveness check is gone and `Enqueue` has to test `IsOpen` instead. That in turn requires every teardown path to actually close the streams it abandons, so `PicoQuicDataContext::MarkUnregistered` now closes its streams — previously connection teardown left them reporting open, which was harmless only because the `IsDataContextActive` check in `Enqueue` caught it first.

`CloseStream` keeps a by-id overload for receive streams, which the caller only knows by ID. Phase 4 gives RX streams handles too and it goes away.

Important sequencing note: handlers should receive stream handles **instead of** data context handles, not in addition to them. Churning `SetPublishDataContext`/`GetPublishDataContext` in this phase and then removing it in Phase 5 is wasted work — retire it here.

*Revised during implementation.* `SetPublishDataContext`/`GetPublishDataContext` survive Phase 3 untouched, because two callers still genuinely need a data context and neither is fixable here: `Enqueue` needs one for the datagram path, which only goes away once Phase 5 adds the per-connection datagram channel, and `CreateStream` needs one to own the new stream, which only goes away once Phase 4 moves stream ownership to the connection. Retiring the accessors now would mean building an interim session-side substitute for both that Phase 5 then deletes — the wasted work this note warns about. The intent behind the note is still met: nothing was churned, and the *stream* paths (`StreamInfo`, `PublishFetchHandler::stream_`, `TrackHandler::request_stream_`) hold stream handles rather than data contexts. The accessors are deleted in Phase 5 along with `DataContext` itself.

I would keep the *application-facing* callbacks (`SessionCallbacks::OnStreamClosed(stream_id, flag)`, `SubscribeTrackHandler::StreamDataRecv(stream_id, ...)`, `PublishTrackHandler::StreamClosed(stream_id, bool)`) taking the id. Those are public virtuals that downstream code may override, and the id is what appears in logs and on the wire. Drawing the boundary there keeps the break confined to `Transport`, which is the internal interface.

**Phase 4 — unify the registries.** One connection-owned `std::map<uint64_t, shared_ptr<PicoQuicStream>>` holding both TX and RX state. This is the main event, not an optional cleanup: it is what removes the last reason for the data context to own anything.

It also lets remote-initiated unidirectional RX streams get an app stream context, which removes the last `rx_stream_buffer.find` from the RX hot path; replaces `StreamRxContext::caller_any` (a `std::any_cast` per chunk) with a typed field; and can carry the subscriber's parse state so `SubscribeTrackHandler::streams_.find` goes away as well.

*Notes from implementation.* `PicoQuicDataContext` ended up with no stream container at all, rather than a lighter index into the connection's. Every call site that needed a context's streams already had the connection in scope, so the two candidate lookups — "all streams for a context" and "the one stream of a bidirectional context" — became filters on the connection's map (`GetStreamsForDataContext`, `GetOnlyStreamForDataContext`). Both are cold: teardown, and the no-handle `Enqueue` path used by control messages. Keeping a second container in sync for their benefit would have reproduced the dual bookkeeping the phase exists to remove.

The one path that would have been genuinely hot under filtering is `CheckConnsForCongestion`, which ran nested loops over contexts and their streams. It is now a single pass over the connection's streams that reaches each context through the stream, which is one lookup fewer than before rather than more.

`RemoveClosedStreams` needs care that the old split registries did not. Erasing an `rx_stream_buffer` entry used to discard receive state and nothing else; erasing from the unified map would take the send side with it. It therefore removes only receive-only streams and merely releases `rx_ctx` on streams that also send, leaving their teardown to the data context that owns them. Removal is safe with respect to picoquic because every path that sets `rx_closed` clears `app_stream_ctx` first, via `picoquic_reset_stream_ctx`.

The `stream_id == 0` convention in `SendCtrlMsg` survives to Phase 5 as originally planned. Retiring it means threading a stream handle through eighteen `Send*` functions and their callers, and the `GetOnlyStreamForDataContext` filter it depends on costs a walk of the connection's streams only on control messages. That is worth paying until the data context goes away and the accessors can be deleted together.

The by-id `Transport::CloseStream` overload also survives. Its one remaining caller learns the stream only as an ID, from `Connection::Delegate::OnRecvStream`, whose signature is reworked in Phase 5 when `rx_ctx` and `data_ctx` are replaced by the stream itself.

**Phase 5 — remove `DataContext`.** *Done.* With streams owned by the connection and handles held by handlers, the work was:

- Deleted the reset-wait machinery: the selection loop in `CheckConnsForCongestion`, `uses_reset_wait`, `tx_reset_wait_discard`, `kPqRestWaitMinPriority`, the `tx_reset_wait` metric, `Config::use_reset_wait_strategy`, and the c-bridge field.
- Moved metrics onto the stream. `QuicDataContextMetrics` split into `QuicStreamMetrics` on `PicoQuicStream` and `QuicDatagramMetrics` on `PicoQuicConnection`, which owns the datagram channel's counters outright — no track tag needed, since none of them is surfaced per track.
- Deleted the Role A `CreateDataContext` call sites in `session.cpp` in favour of the request stream the handler already holds, along with `request_id_by_data_ctx`, `GetOnlyStreamForDataContext`, and the `stream_id == 0` convention in `SendCtrlMsg`.
- Replaced `rx_ctx`/`data_ctx` on `Connection::Delegate::OnRecvStream` and `OnStreamClosed` with the stream, and deleted the by-id `Transport::CloseStream` overload.
- Replaced the Role B publish data context with per-stream metrics and `close_on_empty`, and retired `CreateDataContext`, `DeleteDataContext`, `CreateDataContextBiDirRecv`, `IsDataContextActive`, `active_data_contexts_`, `ConnData::data_ctx_id`, and `TransportError::kInvalidDataContextId`.

Reporting a flow's metrics survives the move intact: a flow is sampled once per period whether or not it currently has a stream, so `SampleMetrics` merges its streams' metrics rather than reporting each stream separately. Dropping that produced the one observable regression during the move — a publish track that had not yet opened a subgroup stream stopped reporting entirely, caught by `Integration - Publish metrics report transmitted objects`. `MinMaxAvg::Merge` and `QuicStreamMetrics::Merge` exist for this. Once the flow becomes a track tag, the merge keys on the tag instead.

*Notes from implementation.* Three pieces of behaviour lived on the data context rather than on any stream, and each needed a new home rather than a mechanical translation.

`delete_on_empty` meant "close the flow's streams once they have drained". Its replacement, `close_on_empty`, is per stream and already existed, but nothing was setting it at unbind time — the data context used to close streams on the flow's behalf. The handler now does it explicitly: `PublishTrackHandler::EndAllSubgroups` ends every open subgroup and `PublishFetchHandler::EndFetch` ends the fetch stream, both by enqueuing a close-stream marker so the stream drains first. `DeleteDataContextInternal`'s stream-closing half is gone rather than relocated.

Datagram priority was set once per flow, in `CreateDataContext`. With no flow object there is nowhere to hang "already set", so `PicoQuicConnection::dgram_priority` records the last value handed to picoquic and `EnqueueDatagram` calls `picoquic_set_datagram_priority` only when it changes. Calling it per send would work but would be a syscall-shaped cost on the datagram hot path for no reason.

The publish safety check needed rebuilding on a different foundation. `Integration - Unbound publish track cannot create streams` passed only because `Enqueue` refused to touch a deregistered data context, which caught a publish on an unbound track as a side effect. That gate is gone, so unbinding now sets the handler to `kNoSubscribers` and the handler's own status check refuses the publish — which is where the check belonged, since the transport has no notion of a track being bound. The test asserts the returned status rather than an exception, because the refusal no longer travels as a transport error.

One unrelated bug surfaced under ASan while verifying the phase: four message decoders read `buffer.Front()` into a span and then called `buffer.Pop()` before dereferencing it. Fixed in place; it predates this work.

**Phase 6 — rebase onto `refactor-session`.** *Done.* Upstream gained two changes in the same file this work rewrites: per-connection picoquic sharding (`Shard`, `Config::quic_shards`, `PicoQuicConnection::shard_idx`), and mark-active queues that replace the `RunPqFunction` lambda per enqueue. Both survive; the queue is the only one that needed rethinking, because upstream's `StreamMarkActiveInfo` carries `DataContext&` and hands `&data_ctx` to picoquic as the app stream context.

It is now `{weak_ptr<PicoQuicConnection>, weak_ptr<PicoQuicStream>}` — the stream knows its own ID, and the stream pointer is already what picoquic wants. Weak handles also retire the precondition upstream documented on `ProcessMarkActive`, that it must be drained before removing any stream or context: a queued mark whose stream has gone is now skipped, and so is one whose stream is merely closed, which both teardown paths guarantee (`CloseInternal` marks every stream closed, `EraseStreamState` does so via `RemoveStream`). The drain in `CloseInternal` stays, because a queued *datagram* mark has no equivalent guard and would otherwise reach a closed connection's `pq_cnx`.

Sharding needed no rethinking, only threading: `RunPqFunction` takes the connection's shard, and the stream handle it used to capture to keep a stream alive across the thread boundary is unnecessary on the enqueue paths now that they push to the queue instead.

Multi-shard does not work, on this branch or on `refactor-session`: the second `picoquic_create_and_configure` in `CreateQuicInstance` returns null, so any `quic_shards > 1` server throws `kFailedToCreateQuicInstance` at startup. That is upstream's unfinished experiment, gated off by the `quic_shards{ 1 }` default, and is preserved as-is rather than fixed here.

## 7. WebTransport

WebTransport cannot use picoquic's slot — picowt owns it and stores `h3zero_stream_ctx_t*` there. Two options, in order of preference:

The low-risk one is to retype `wt_stream_to_data_ctx` as `map<uint64_t, weak_ptr<PicoQuicStream>>`, which collapses today's "find data context, then find stream within it" into a single lookup and keeps both paths structurally identical. *Done in Phase 4, as `wt_stream_to_stream`.* Remote-initiated WebTransport streams are now registered there as they arrive, so the mapping covers receive-only streams rather than just locally-created ones, and `h3zero_stream_ctx_t*` is cached on the stream for every stream instead of only those with a data context.

The zero-lookup one is to set `stream_ctx->path_callback_ctx` to a small `{transport, weak_ptr<PicoQuicStream>}` struct instead of bare `this`. That works for locally-created streams (it's already set per-stream in `CreateStreamInternal`), but remote-initiated streams inherit their callback ctx from the path registration, so the map is still needed as a fallback. Treat this as optional and only pursue it if WebTransport becomes a throughput-critical path.

---

Net effect at the end of Phase 3: the TX path goes from six lookups plus an O(N_streams) loop per object down to one handler-side lookup, and the RX path loses a global mutex acquisition per chunk.

Net effect at the end of Phase 4: a stream is one object. The RX hot path has no registry lookup left — picoquic hands back the stream directly for raw QUIC, and WebTransport resolves it in one lookup instead of two — and dispatch to the subscriber is a typed `weak_ptr` rather than a `std::any_cast`. The unsynchronised `rx_stream_buffer` access from the picoquic thread, which raced the notify thread's erase under `state_mutex_`, is gone with the container itself.

Net effect at the end of Phase 5: one object per stream, owned by the connection, referenced directly by picoquic, with no intermediate indirection between a track handler and the bytes it is sending. The `DataContext` abstraction, the `request_id_by_data_ctx` bridge, and the dead reset-wait machinery all go away.

Net effect at the end of Phase 6: the above, on top of upstream's sharding and mark-active queues, with the enqueue paths no longer allocating a lambda or waking the picoquic thread per object.
