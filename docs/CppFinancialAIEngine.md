# **🧠 C++ Financial AI Engine**

This document defines the architecture of a high-performance financial AI engine implemented in C++. It focuses on modularity, streaming data, real-time signal generation, and integration with AI models and external interfaces.

---

## **✅ Domain & Problem**

**Domain**: Algorithmic trading and financial time series analysis.

**Goal**: Process real-time or historical market data (ticks), compute indicators and AI-based predictions, generate trading signals, and evaluate performance through backtesting.

---

## **✅ Layered Architecture**

| Layer | Responsibility |
| --- | --- |
| Presentation | CLI tool, HTTP microservice, dashboards |
| API | C++ public API and Python bindings (native CPython module) |
| Application | Signal orchestration, backtesting engine |
| Processing | Technical indicators, AI models, feature engineering |
| Data | CSV/JSON readers, tick streamers, resampling to OHLCV |
| Core / Domain | Base types: Tick, Bar, Signal, Time, Symbol, RingBuffer |
| Infrastructure | Logging, config, metrics, threading abstractions |

---

## **✅ Core Data Types**

```cpp
struct Tick {
  Timestamp ts;
  std::string symbol;
  double price;
  double volume;
};

struct Bar {
  Timestamp ts;
  double open, high, low, close, volume;
};

enum class SignalType { Buy, Sell, Hold };

struct Signal {
  Timestamp ts;
  std::string symbol;
  SignalType type;
  double score;
  std::string source;
};

```

---

## **✅ Module Map**

```
/include/fin
├─ core/           → Timestamp, Tick, Candle, RingBuffer
├─ io/             → CSV loader, Resampler, Pipeline helpers
├─ indicators/     → Indicators (RSI, EMA, MACD, Bollinger, etc.) and FeatureBus
├─ ml/             → AI Models (LinearModel, LinearTrainer), Feature extraction
├─ signal/         → Signal Engine, Rules, Events
├─ backtest/       → Backtesting engine and metrics
├─ app/            → Scenario config, runner, JSON serialization
└─ api/            → ScenarioService facade

```

The front-ends live outside `include/fin`: the CLI in `src/main.cpp`, the HTTP service in `src/server/http_main.cpp`, and the optional Python bindings in `bindings/python/`.

---

## **✅ Execution Flow**

**Batch (Backtest):**

```
[CSV Loader] → [BarSeries]
             → [Indicators] → [AI Model] → [Signal Engine] → [Metrics Report]
```

**Real-Time (Streaming):**

```
[Tick Feed] → [Resampler]
            → [Indicators.update()] → [Model.predict()]
            → [SignalEngine.eval()] → [Signal Dispatch / Alert]
```

---

## **✅ Interfaces**

### **IModel**

```cpp
class IModel {
public:
  virtual void fit(std::span<const Feature> X, std::span<const double> y) = 0;
  virtual double predict(const Feature& x) const = 0;
  virtual void partial_fit(const Feature& x, double y) = 0;
  virtual ~IModel() = default;
};
```

### **Signal Engine**

```cpp
class SignalEngine {
public:
  Signal eval(const IndicatorsSnapshot&, std::optional<double> prediction);
};
```

---

## **✅ Performance Notes**

- SIMD acceleration via AVX/NEON (configurable)
- Parallel streaming by symbol (multi-threaded workers)
- Lock-free queues (MPMC) for real-time pipelines
- Pre-allocated memory for indicators (no runtime allocations)

---

## **✅ External Integration**

- **Python bindings**: native CPython module exposing scenario runner
- **CLI tool**: `aiquant features ticks.csv --tf M1`, `aiquant train-linear ticks.csv --out linear_model.csv`, `aiquant backtest ticks.csv --model-linear linear_model.csv`
- **Optional HTTP endpoint**: `/predict`, `/signal`, `/health`

---

## **✅ Testing & Validation**

- Unit tests: Indicators, Models, Signal rules
- Golden tests: RSI, MACD vs TA-Lib outputs
- Property tests: Monotonicity, range checks
- Fuzzing: Input readers and streaming update paths

---

## **✅ Future Expansion**

- Add order execution + broker APIs
- GUI dashboard for signals
- WebSocket feed support
- Portfolio risk models and multi-asset simulation

---

## **🚀 MVP Coverage**

- **Presentation**: `aiquant` CLI plus the `aiquant_http` microservice (POST `/run-file` or `/run-config`).
- **API**: `fin::api::ScenarioService` for native embedding and the optional `aiquant_api` Python module (built with pybind11, which is required whenever `AIQUANT_BUILD_PYTHON` is ON — the default).

---

## **📁 Folder Structure**

```
/aiquant
├─ include/fin/
│   ├─ core/
│   ├─ io/
│   ├─ indicators/
│   ├─ ml/
│   ├─ signal/
│   ├─ backtest/
│   ├─ app/
│   └─ api/
├─ src/
│   ├─ fin/            → one directory per static library
│   ├─ main.cpp        → aiquant CLI
│   └─ server/         → aiquant_http
├─ bindings/python/    → aiquant_api (pybind11)
├─ scenarios/          → example INI + tick CSV
├─ tests/
└─ docs/

```

---

## Diagrams

The layer diagrams are not checked in. The per-layer design docs (`docs/CoreLayerDesign_*`, `docs/IndicatorsLayerDesign_*`, `docs/IOLayerDesign_*`) describe the same structure in text.

## **✅ Summary**

This architecture enables:

- High-speed financial data processing
- Real-time or batch signal generation
- AI-enhanced decision logic
- Clean separation of concerns
- Extensible and testable components

You can adapt this to other time-series domains like IoT, healthcare monitoring, or anomaly detection.
