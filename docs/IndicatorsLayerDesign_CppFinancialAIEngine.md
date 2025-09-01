# Indicators Layer Design

## Overview

This document defines the **Indicators Layer** of the C++ Financial AI Engine. This layer computes common technical indicators used for trading signals, features extraction, and strategy logic.

## 🎯 Purpose

- Consume real-time or historical market data (`Price`, `Tick`, `Candle`);
- Compute **streaming technical indicators** (RSI, EMA, SMA, VWAP, etc.);
- Expose a common API to upstream modules like `SignalEngine`, `Backtester` or `ML` models.

## **🔁 Runtime Behavior**

- Most indicators are **online algorithms**: one value in, one value out;
- Internally use **RingBuffer<T>**, exponential smoothing, or rolling math;
- Must operate **without dynamic memory** (important for low-latency systems)

## **🧱 Key Components**

### Abstract Interface (optional)

```cpp
namespace fin::indicators {

class IIndicator {
public:
  virtual void update(Price price) = 0;
  virtual bool is_ready() const = 0;
  virtual double value() const = 0;
  virtual ~IIndicator() = default; 
}
} // namespace fin::indicators
```

#### Concrete Indicators

Each has a similar shape:

- `RSI`
- `EMA`
- `SMA`
- `VWAP`
- `MACD`
- `ZScore`
- `Momentum`

### Typical API

```cpp
update(Price)
value() const
is_ready() const
```

## Design Principles

| **Principle** |  |
| --- | --- |
| Type Safety | All prices use `Price`, now raw `double`  |
| Performance  | Pre-allocated buffers (RingBuffer) |
| Minimal State | No unnecessary data kept |
| Reusability | Plug into any stream |
| Encapsulation  | No public mutable fields |

## Folder Layout

```
include/fin/indicators/
├── RSI.hpp
├── EMA.hpp
├── VWAP.hpp
└── ...

src/fin/indicators/
├── RSI.cpp
├── EMA.cpp
└── ...

tests/indicators/
├── test_rsi.cpp
└── test_ema.cpp
```

## Next Steps

- Finalize `RSI` and `EMA` designs;
- Write shared test utilities for price streams;
- Consider a `CompositeIndicator` for chaining

## Indicator Design Specification

[**RSI Indicator Design**](https://www.notion.so/RSI-Indicator-Design-25a16e551b5e80f789c4eb9b72684df3?pvs=21)

[Simple Moving Average (SMA) - Design](https://www.notion.so/Simple-Moving-Average-SMA-Design-25a16e551b5e80bbb1eafc6a811a327c?pvs=21)

[Exponential Moving Average (EMA) Design](https://www.notion.so/Exponential-Moving-Average-EMA-Design-25b16e551b5e8040825fcbb8bf750987?pvs=21)

[Bollinger Bands (BB) Design](https://www.notion.so/Bollinger-Bands-BB-Design-25c16e551b5e80af938ece623a94dc4c?pvs=21)

[MACD (Moving Average Convergence/Divergence) Design Document](https://www.notion.so/MACD-Moving-Average-Convergence-Divergence-Design-Document-25c16e551b5e8064bde5e94d7b5e0487?pvs=21)

[ATR (Average True Range) Design](https://www.notion.so/ATR-Average-True-Range-Design-25c16e551b5e80a1933de0466f982a5e?pvs=21)

[Stochastic Oscillator (%K, %D) Design](https://www.notion.so/Stochastic-Oscillator-K-D-Design-25d16e551b5e80a69764d4984287bf36?pvs=21)

[VWAP (Volume-Weighted Average Price) — Design](https://www.notion.so/VWAP-Volume-Weighted-Average-Price-Design-25d16e551b5e809b9967cc96a1a7d3c4?pvs=21)

[ADX (Average Directional Index) Design](https://www.notion.so/ADX-Average-Directional-Index-Design-25d16e551b5e8022960df0eb9cb7197f?pvs=21)

[ZScore (Rolling Z-Score) — Design](https://www.notion.so/ZScore-Rolling-Z-Score-Design-25e16e551b5e80ec8350e3b64e3008f8?pvs=21)

[Momentum — Design](https://www.notion.so/Momentum-Design-25e16e551b5e806f836bc0d18c7c35f3?pvs=21)
