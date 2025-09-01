# Core Layer Design (C++ Financial AI System)

This document defines the **Core Layer** of the Financial AI System. This layer provides fundamental data structures and types that other modules (indicators, ML, signals, backtesting) rely on.

---

## Overview

The Core Layer provides:

- Type-safe domain primitives (e.g., `Price`, `Timestamp`, `Volume`, `Symbol`)
- Core entities (e.g., `Tick`, `Candle`)
- Efficient data structures for real-time streaming (e.g., `RingBuffer`)

---

## 1. Type-Safe Domain Primitives

### Timestamp

```cpp
using Timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;
```

- Represents high-precision point in time
- Consistent across all time-based logic

### Price

```cpp
class Price {
 public:
  explicit Price(double v);
  double value() const;

  bool operator==(const Price& other) const;
  bool operator<(const Price& other) const;
  Price operator-(const Price& other) const;
  Price operator+(const Price& other) const;

 private:
  double val_;
};

```

- Enforces encapsulation and avoids invalid price assignment
- Enables operator overloading for convenience

### Volume

```cpp
class Volume {
 public:
  explicit Volume(double v);
  double value() const;

  Volume operator+(const Volume& other) const;

 private:
  double val_;
};

```

- Ensures volume is manipulated safely

### Symbol

```cpp
class Symbol {
 public:
  explicit Symbol(const std::string& v);
  const std::string& value() const;

  bool operator==(const Symbol& other) const;

 private:
  std::string val_;
};

```

- Represents asset identity safely and consistently

---

## 2. Tick

```cpp
class Tick {
 public:
  Tick(Timestamp ts, Symbol sym, Price p, Volume v);

  Timestamp timestamp() const;
  const Symbol& symbol() const;
  Price price() const;
  Volume volume() const;

 private:
  Timestamp ts_;
  Symbol symbol_;
  Price price_;
  Volume volume_;
};

```

- Represents a single market update (trade or quote)
- All data is immutable after construction

---

## 3. Candle (OHLCV)

```cpp
class Candle {
 public:
  Candle(Timestamp start, Price open, Price high, Price low, Price close, Volume vol);

  Timestamp start_time() const;
  Price open() const;
  Price high() const;
  Price low() const;
  Price close() const;
  Volume volume() const;

 private:
  Timestamp start_;
  Price open_;
  Price high_;
  Price low_;
  Price close_;
  Volume volume_;
};

```

- Used to aggregate a series of `Tick`s into defined intervals (e.g., 1-minute).
- Immutable once constructed

---

## 4. RingBuffer

A fixed-size circular buffer to store recent elements with O(1) insertion.

```cpp
template<typename T, std::size_t N>
class RingBuffer {
 public:
  void push(const T& value);
  const T& operator[](std::size_t index) const;
  std::size_t size() const;
  bool full() const;

 private:
  std::array<T, N> data_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

```

- Used by streaming indicators like `RSI`, `EMA`, `VWAP`, etc.

---

## Design Principles

- **Type Safety**: Avoid raw doubles for price/time.
- **Encapsulation**: Prevent accidental modification of internal data.
- **Reusability**: Used by all other layers (Indicators, SignalEngine, ML, etc.)
- **Performance**: Minimal overhead, designed for real-time systems.
- **Independence**: No external dependencies, self-contained.

---

## Next Steps

- Implement and test each component in isolation
- Integrate into Indicators layer once complete
- Add serialization utilities (optional)

---

*Author: AiQuant Design System*

*Version: Draft 1.1*
