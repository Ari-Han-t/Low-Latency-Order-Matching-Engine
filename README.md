# Low-Latency Order Matching Engine 2025

C++, Multithreading, WebSockets, CMake

Asynchronous limit order book (LOB) matching engine with lock-free ingress/publish queues, lock-free memory pool allocation for order nodes, and a WebSocket API for real-time Level 2 market data.

## Highlights

- Single-writer matching core for deterministic FIFO price-time priority.
- Lock-free MPSC command queue for concurrent producer threads.
- Lock-free object pool for order-node allocation/reuse to reduce heap churn and latency spikes.
- Dedicated matcher + publisher threads for asynchronous market data fan-out.
- Native WebSocket server with RFC6455 handshake, text frames, ping/pong, and broadcast.
- Bench harness to measure p50/p95/p99 execution latencies.

## Project Layout

```text
include/lom/
  lockfree_mpsc_queue.hpp
  matching_engine.hpp
  order_book.hpp
  order_pool.hpp
  types.hpp
  websocket_server.hpp
src/
  core/order_book.cpp
  engine/matching_engine.cpp
  net/websocket_server.cpp
  util/base64.cpp
  util/sha1.cpp
  main.cpp
  benchmark.cpp
tests/
  order_book_tests.cpp
examples/
  ws_client.html
```

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Windows (MSVC) quickstart

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Linux / WSL quickstart

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Run Engine

```bash
./build/lom_engine
```

WebSocket endpoint:

```text
ws://127.0.0.1:9002
```

## API (WebSocket JSON)

### New order

```json
{
  "type": "new_order",
  "request_id": "client-req-001",
  "order_id": 1001,
  "user_id": 42,
  "side": "buy",
  "price_ticks": 10050,
  "quantity": 25,
  "client_ts_ns": 0
}
```

### Cancel order

```json
{
  "type": "cancel_order",
  "request_id": "client-req-002",
  "order_id": 1001,
  "user_id": 42,
  "client_ts_ns": 0
}
```

### Outbound events

- `ack` for accepted/rejected command ingestion. Includes:
  - `schema_version`
  - `request_id` (if provided by client)
  - `reason_code` + `reason` when rejected
- `l2` payload after accepted book updates:
  - `schema_version`
  - `sequence`
  - `ts_ns`
  - `bids`: `[price_ticks, total_qty, order_count]`
  - `asks`: `[price_ticks, total_qty, order_count]`
  - `trades`: array of matched fills

### Example `ack` event

```json
{
  "schema_version": 1,
  "type": "ack",
  "event": "new_order",
  "status": "accepted",
  "request_id": "client-req-001",
  "ts_ns": 1720000000000
}
```

### Example `l2` event

```json
{
  "schema_version": 1,
  "type": "l2",
  "sequence": 1024,
  "ts_ns": 1720000000000,
  "bids": [[10050, 45, 3]],
  "asks": [[10060, 20, 1]],
  "trades": [
    {
      "taker_order_id": 2002,
      "maker_order_id": 1999,
      "buy_user_id": 77,
      "sell_user_id": 51,
      "price_ticks": 10060,
      "quantity": 5,
      "match_ts_ns": 1720000000000
    }
  ]
}
```

## Risk Guardrails

Gateway validation rejects orders before enqueue if:

- `order_id == 0`
- `user_id == 0`
- `quantity == 0` or `quantity > 250000`
- `price_ticks` outside `[1, 2000000]`

## Threading Model

- Producer threads enqueue commands through a lock-free MPSC queue.
- Matcher thread is the single writer to the limit order book for deterministic matching.
- Publisher thread fans out acknowledgements and `l2` snapshots to websocket clients.
- Memory pool recycles order nodes to minimize heap-driven latency spikes.

## Runtime Counters

At shutdown, the engine prints:

- submit accepted/rejected totals
- commands processed/accepted/rejected
- total trades and matched quantity
- total market data events emitted

## Project Planning

- Roadmap: `docs/ROADMAP.md`
- Issue backlog: `docs/ISSUES_BACKLOG.md`

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Benchmark

```bash
./build/lom_benchmark
```

## Browser Smoke Test

Open `examples/ws_client.html`, connect to `ws://127.0.0.1:9002`, and submit orders.
