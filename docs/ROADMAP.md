# Roadmap

This roadmap tracks the planned evolution of the matching engine from educational prototype to production-style system.

## 2026 Q1 - Core Stability

- Complete deterministic matching semantics for limit and cancel paths.
- Expand unit tests around edge cases (queue pressure, empty-book transitions, invalid input).
- Add CI checks on Linux and Windows.
- Define baseline websocket schema versioning policy.

## 2026 Q2 - Risk and Market Controls

- Add configurable risk limits:
  - max order quantity
  - min/max price band
  - optional user-level throttling
- Introduce reject telemetry with structured reason codes.
- Add circuit-breaker style guard rails for abnormal spread/volatility.

## 2026 Q3 - Performance and Observability

- Add engine counters for ingest, accepted/rejected commands, trade throughput.
- Export periodic metrics snapshots (JSON endpoint or websocket channel).
- Add replayable benchmark scenarios with reproducible seeds and reports.
- Compare lock-free queue implementations under contention.

## 2026 Q4 - Robust Networking

- Add websocket authentication and per-client permissions.
- Add heartbeat + stale-client eviction policy.
- Add binary protocol option for lower-overhead market data streaming.
- Implement optional gzip/deflate compression path for snapshots.

## Long-term Goals

- Multi-symbol support with sharded matching loops.
- Persistence and recovery log (WAL + snapshots).
- SBE/FIX gateway adapters.
- Deterministic simulation mode for backtesting and research.
