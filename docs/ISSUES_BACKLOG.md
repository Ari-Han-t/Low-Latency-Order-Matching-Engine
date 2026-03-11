# Issue Backlog

Prioritized technical backlog for next development cycles.

## P0

1. Add explicit websocket schema version and reject reason codes.
2. Validate new order risk bounds before command ingest.
3. Add market data sequence-gap detection support for clients.
4. Add CI artifact uploads for benchmark and test logs.

## P1

1. Add matching-engine counters and surface them in logs/metrics.
2. Add JSON schema docs for inbound and outbound websocket payloads.
3. Add unit/integration tests for websocket command parser behavior.
4. Add cancellation idempotency tests under repeated cancel messages.

## P2

1. Add stress test harness for bursty producer traffic.
2. Add benchmark mode for configurable symbols and order distributions.
3. Add optional persistence of fills and snapshots for replay.
4. Add Dockerized local environment for reproducible builds.

## Open Questions

1. Should risk checks live at gateway, matcher, or both layers?
2. What is the target max fan-out client count for L2 streaming?
3. Do we optimize first for p99 latency or sustained throughput?
4. Which transport(s) beyond websocket are required in MVP?
