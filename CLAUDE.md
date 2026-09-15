# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

AiQuant is a C++20 quantitative-finance engine. It turns tick CSVs into OHLCV candles, computes technical indicators, trains a ridge linear model to predict the next candle's close delta, and backtests a rule-based signal engine that is driven by those predictions. It is exposed through a CLI, a minimal HTTP service, and a pybind11 Python module.

For current state, known issues and the roadmap, see `docs/ProjectStatus.md`. The layer design docs are `docs/*LayerDesign_CppFinancialAIEngine.md`, and the scenario INI grammar is in `docs/ScenarioConfig.md`.

## Commands

```bash
# Configure. Python bindings are ON by default and configure FAILS without pybind11:
#   brew install pybind11           (or: pip install pybind11)
#   or add -DAIQUANT_BUILD_PYTHON=OFF
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Tests: one ctest entry (aiquant_tests) containing all TEST_CASEs
ctest --test-dir build --output-on-failure

# Sanitizers (mirrors .github/workflows/sanitizers.yml); keep the build dir outside the repo
F="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S . -B /tmp/aiquant-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="$F" -DCMAKE_CXX_FLAGS="$F"
cmake --build /tmp/aiquant-asan && ctest --test-dir /tmp/aiquant-asan --output-on-failure

# CLI (subcommands: features, backtest, train-linear, run-mvp, run-config; run without args for usage)
./build/aiquant run-mvp ticks.csv --tf M1 --json --model-out model.csv
./build/aiquant run-config scenario.ini --json
./build/aiquant backtest ticks.csv --model-linear model.csv

# HTTP (POST only): body of /run-file = path to an INI on the server; body of /run-config = raw INI
./build/aiquant_http --port 8080

# Python: use the same interpreter CMake found (printed as "Found Python3" at configure time)
PYTHONPATH=build python3 -c "import aiquant_api as aq; print(aq.run_config({'ticks_path': 'ticks.csv'})['metrics'])"
PYTHONPATH=build python3 tests/python/smoke_test.py   # module smoke test, also run by ci.yml (not by ctest)
```

A scenario needs enough candles to get through indicator warmup (about 33 with default periods, plus at least 3 feature rows). The 3–7 row `ticks_*.csv` files at the repo root are too small for `run-mvp`/`run-config`.

## Testing gotchas

- The tests use a **bundled 95-line "minicatch"** (`tests/catch2/catch.hpp`) unless a real Catch2 is installed. It has a plain `int main()` that ignores arguments, so **tag and name filters don't work**: `aiquant_tests "[rsi]"` still runs everything. It supports only `TEST_CASE`, `REQUIRE`, `REQUIRE_FALSE` and `Approx(...).margin()`; there is no `SECTION`. Always include `"catch2_compat.hpp"` so the same test sources also compile against real Catch2 v2/v3.
- If a system Catch2 v3 is found, `CMakeLists.txt` takes a different path: it links `Catch2WithMain` and adds the extra `test_io` / `test_resampler` / `test_pipeline` executables. That path was not exercised locally.
- Several tests write CSV fixtures (`ticks_sample.csv`, `ticks_features.csv`, `ticks_sample_pipeline_<pid>.csv`) into the **current working directory**. `ctest` runs them inside `build/`. Running `aiquant_tests` from the repo root overwrites the committed root CSVs and leaves untracked files behind.
- Sources and tests are collected with `file(GLOB_RECURSE)`, so re-run the CMake configure step after adding or removing `.cpp` files. Any new `tests/**/*.cpp` is compiled into the single `aiquant_tests` binary.

## Architecture

Static libraries, one per directory under `src/fin/` and `include/fin/`, layered as follows (defined in `CMakeLists.txt`):

```
fin_core (Timestamp, Price, Volume, Symbol, Tick, Candle, RingBuffer)
 ├─ fin_indicators (SMA, EMA, RSI, MACD, BBands, ATR, ADX, Stochastic, VWAP, ZScore, Momentum,
 │                  candle/scalar adapters, FeatureBus)
 ├─ fin_io        (FileTickSource CSV reader, TickToCandleResampler S1/S5/M1/M5/H1, Pipeline helpers)
 └─ fin_signal    (IndicatorsSnapshot -> SignalEngine -> Signal)
fin_backtest (Backtester: long-only, cash/qty/fee, drawdown)   -> core, indicators, signal
fin_ml       (FeatureVector, IModel, LinearModel, LinearTrainer) -> core, indicators
fin_app      (ScenarioConfig INI IO, ScenarioRunner, JSON serialization) -> all of the above
fin_api      (fin::api::ScenarioService: run / run_file / load_file) -> fin_app
```

Front-ends: `src/main.cpp` (the `aiquant` CLI), `src/server/http_main.cpp` (`aiquant_http`), and `bindings/python/aiquant_module.cpp` (`aiquant_api` module).
- The HTTP service and the Python module go through `fin::api::ScenarioService`.
- The CLI's `run-mvp` / `run-config` print the same `ScenarioResult`, and `--json` uses the shared `fin::app::scenario_result_to_json`.

The core pipeline is `fin::app::run_scenario` (`src/fin/app/ScenarioRunner.cpp`):
1. `io::resample_csv_with_stats(ticks_path, timeframe)` builds the candles.
2. `indicators::FeatureBus` turns each candle into a `FeatureRow` (close, ema_fast, rsi, macd, macd_signal, macd_hist). It returns `std::nullopt` during warmup.
3. The first `train_ratio` of rows goes to `ml::train_linear_from_feature_rows`, a ridge regression on the target `next_close - close`.
4. Validation RMSE and a preview are computed on the remaining rows.
5. A second, fresh `FeatureBus` replays all candles. Each candle's model prediction is passed as the *pending* prediction to `Backtester::on_candle` for the **next** candle. The backtester maintains its own EMA/RSI and asks `SignalEngine::eval(snapshot, prediction)` for Buy/Sell/Hold.
6. `Backtester::finalize()` returns `Metrics` (final_cash, pnl, return_pct, max_drawdown, trades, wins, losses).

When you change a config field, keep these in sync:
- `ScenarioConfig` (`include/fin/app/ScenarioRunner.hpp`)
- the INI parser (`src/fin/app/ScenarioConfigIO.cpp`)
- JSON output (`src/fin/app/ScenarioSerialization.cpp`)
- CLI flags (`src/main.cpp`)
- the Python dict conversion (`bindings/python/aiquant_module.cpp`)
- `docs/ScenarioConfig.md`

## CI

GitHub Actions runs on Ubuntu only:
- `ci.yml`: Debug build + ctest.
- `sanitizers.yml`: ASan/UBSan.
- `coverage.yml`: gcov/lcov artifact.
- `release.yml`: on `v*.*.*` tags, packages `build-rel` as a tarball.

Because `AIQUANT_BUILD_PYTHON` defaults to `ON` and configure fails without pybind11, every workflow must install `pybind11-dev` and `python3-dev` via apt (PR #11). Any new workflow that configures the project needs the same step, or `-DAIQUANT_BUILD_PYTHON=OFF`.

## Branch and PR hygiene

- The repo has **"Automatically delete head branches"** enabled, so merging a PR through the GitHub UI or `gh pr merge` deletes the head branch and closes the PR. Prefer `gh pr merge <n> --squash --delete-branch`.
- `.github/workflows/branch-cleanup.yml` is the safety net for merges pushed straight from a clone: on every push to `main`, weekly, or on demand, it deletes unprotected remote branches whose tip is already an ancestor of `main`. It skips `main` and the head branch of any open PR.
- A PR whose head branch is deleted is closed automatically by GitHub, so stale branches and stale PRs are cleaned up by the same mechanism.
