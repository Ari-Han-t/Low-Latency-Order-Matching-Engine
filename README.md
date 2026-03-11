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
  "order_id": 1001,
  "user_id": 42,
  "client_ts_ns": 0
}
```

### Outbound events

- `ack` for accepted/rejected command ingestion.
- `l2` payload after accepted book updates:
  - `sequence`
  - `ts_ns`
  - `bids`: `[price_ticks, total_qty, order_count]`
  - `asks`: `[price_ticks, total_qty, order_count]`
  - `trades`: array of matched fills

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

