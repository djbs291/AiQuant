# IO Layer - Design

## ✅ Purpose

The **IO Layer** is the data ingestion pipeline of the engine. It provides unified abstractions for loading **historical data** (CSV, JSON) and streaming **real-time tricks**, and transforms raw data into structures (`Tick`, `Candle`) consumable by the Processing (Indicators, ML) and Application Layers.

## 🧱 Key Components

### 1) **Tick & Candle Sources**

- Abstract interface to unify historical and live data feeds.
- Examples:
  - `FileTickSource` (CSV reader → yields `Tick`)
  - `MockTickSource` (synthetic data for tests)
  - `StreamTickSource` (WebSocket/UDP, optional future)

```cpp
class ITickSource {
public:
 virtual ~ITickSource() = default;
 virtual std::optional<Tick> next() = 0; // blocking or non-blocking yield
};
```

### 2) Resampler

- Converts a tick stream into time-bucketed OHLCV candles;
- Supports configurable interval (e.g., 1m, 5m, 1h).
- Aggregates:
  - `open`: first tick price in interval
  - `high`: max price
  - `low`: min price
  - `close`: last tick price
  - `volume`: sum of tick volumes
- Exposes:
  - `update(Tick)` → optional `candle`
  - `flush()` → closes current candle (end of session/day)

### 3) CSV Readers

- **Tick CSV Loader**
  - Reads historical tick files into `Tick` stream.
  - Columns: `Timestamp`, `symbol`, `price`, `volume`
- Candle CSV Loader
  - Reads pre-aggregated OHLCV data.
  - Columns: `Timestamp`, `open`, `high`, `low`, `close`, `volume`
- Streaming-friendly: iterators/generators instead of full file slurp

### 4) Session / Calendar Utilities

- Handle trading-day boundaries.
- Reset session-aware indicators (e.g., VWAP).
- MVP: UTC midnight; later: exchange calendars

## 🔗 Execution Flow

### Historical Backtest

```css
[Candle CSV Reader] ──▶ [Indicators.update(Candle)]
                └─▶ [ML / SignalEngine]
```

## Real-Time

```css
[Tick Source] ──▶ [Resampler] ──▶ [Indicators.update(Candle)]
                     └─▶ [ML / SignalEngine]
```

## 📈 Design Goals

- **Unified API** for backtest and live streaming.
- **Minimal dependencies** (std::ifstream, chrono).
- **Streaming-first**: no unbounded memory growth.
- **Resilient parsing**: skip malformed CSV lines.
- **Composable**: IO Layer is dumb; higher layers decide strategy.

## 🧪 Testing Strategy

- **Resampler**
  - Ticks accross interval boundaries → exactly one candle emitted.
  - OHLCV fields match manual aggregation.
- **CSV Readers**
  - Parse small sample files → vector of expected `Tick`/ `Candle`.
  - Skip malformed rows gracefully.
- **Integration**
  - `FileTickSouce + Resampler` produces correct candle sequence.

## 🚀 Future Extensions

- WebSocket feed for real-time ticks.
- JSON reader for alternative formats.
- Support for **volume bars**, **tick bars**, or event-driven bars.
- Direct Kafka or Redis connectors for scale-out streaming.

## Design for Each IO Module

[Resampler (Tick → Candle) - Design](https://www.notion.so/Resampler-Tick-Candle-Design-25f16e551b5e80a5a887d829df03f6ed?pvs=21)

[CSV Readers - Design](https://www.notion.so/CSV-Readers-Design-25f16e551b5e80c1bd2ce8abd4c6fa0b?pvs=21)
