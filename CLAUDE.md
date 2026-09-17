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

# Tests: one ctest entry per TEST_CASE (69 unit + 11 golden), so filters work
ctest --test-dir build --output-on-failure
ctest --test-dir build -L unit          # or -L golden; -R matches the test name
./build/aiquant_tests "[rsi]"           # Catch2 tag filtering

# Golden tests against TA-Lib (off by default; needs brew install ta-lib / setup-ta-lib)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAIQUANT_WITH_TALIB=ON

# Sanitizers (mirrors .github/workflows/sanitizers.yml); keep the build dir outside the repo.
# FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER matters locally: see the note below.
F="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S . -B /tmp/aiquant-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER \
  -DCMAKE_C_FLAGS="$F" -DCMAKE_CXX_FLAGS="$F"
cmake --build /tmp/aiquant-asan && ctest --test-dir /tmp/aiquant-asan --output-on-failure

# CLI (subcommands: features, backtest, train-linear, run-mvp, run-config; run without args for usage)
# With --json, stdout is JSON only and the human report goes to stderr, so it pipes into jq.
./build/aiquant run-mvp ticks.csv --tf M1 --json --model-out model.csv
./build/aiquant run-config scenario.ini --json | jq .metrics
./build/aiquant backtest ticks.csv --model-linear model.csv

# HTTP: GET /health; POST /run-file (body = INI path under --root), POST /run-config (raw INI),
# and POST /predict + /signal, which take JSON (see README). --root defaults to the cwd.
./build/aiquant_http --port 8080 --root scenarios --model model.csv [--static examples/dashboard] \
  [--max-body 1048576] [--max-connections 32]
# --static is off unless given; it serves GET from that directory only (whitelisted extensions,
# no dotfiles, no listings, symlinks out refused) and never shadows an API route.
python3 tests/http/smoke_test.py   # endpoint + status-code smoke test, also run by ci.yml (not by ctest)

# Python: use the same interpreter CMake found (printed as "Found Python3" at configure time)
PYTHONPATH=build python3 -c "import aiquant_api as aq; print(aq.run_config({'ticks_path': 'ticks.csv'})['metrics'])"
PYTHONPATH=build python3 tests/python/smoke_test.py   # module smoke test, also run by ci.yml (not by ctest)
```

A scenario needs enough candles to get through indicator warmup (about 33 with default periods, plus at least 3 feature rows). `scenarios/mvp.ini` and its `scenarios/ticks_mvp.csv` (300 synthetic ticks) are the ready-to-run example; run them from the repo root, since INI paths resolve against the working directory.

## Testing gotchas

- Tests build against **real Catch2 v3**. `FetchContent_Declare(... FIND_PACKAGE_ARGS 3)` prefers an installed Catch2 (`brew install catch2`, apt) and downloads v3.16.0 only when there is none, so a machine with the package configures offline. `catch_discover_tests` registers one ctest entry per `TEST_CASE`.
- The **bundled 95-line "minicatch"** (`tests/catch2/catch.hpp`) survives as the no-network, no-package fallback: `-DAIQUANT_USE_BUNDLED_CATCH=ON`. It has a plain `int main()` that ignores arguments, so **filters don't work there** and the whole binary is one ctest entry; it supports only `TEST_CASE`, `REQUIRE`, `REQUIRE_FALSE` and `Approx(...).margin()`, with no `SECTION`. Anything a new test uses beyond that subset breaks this path — check it with the flag before relying on those features.
- `#define CATCH_CONFIG_MAIN` in `tests/core/test_price.cpp` is **load-bearing for that fallback**: it is the only thing that makes minicatch emit `main()`. Real Catch2 ignores it and gets `main()` from `Catch2::Catch2WithMain`.
- **ASan plus a system Catch2 gives false `container-overflow` reports.** With `brew install catch2`, `FIND_PACKAGE_ARGS` picks the prebuilt, *uninstrumented* library while your own code is instrumented, and ASan's container annotations need both sides instrumented. The reports fire inside `Catch::Config::Config` → `Catch::trim` while parsing the test name, before any test body runs, so no project code appears in the stack. Configure sanitizer builds with `-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER` so Catch2 is compiled with the same flags (this is what CI does, since the runners have no Catch2 installed), or set `ASAN_OPTIONS=detect_container_overflow=0`. Both make the suite pass; the first is the honest fix.
- Always include `"catch2_compat.hpp"`, which picks the framework and bridges `Catch::Approx` so unqualified `Approx(...)` keeps compiling. Note minicatch's `Approx` is an absolute 1e-12 while Catch2's default epsilon is relative (~1.2e-5), so the same assertion is stricter under the fallback.
- Tests that need a file on disk use `tests/TestTempFiles.hpp` (`test_files::TempFile`), which writes to the system temp dir and deletes the file on scope exit. Keep new tests on that helper: writing into the current working directory pollutes the repo when `aiquant_tests` is run from the root.
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
fin_ml       (FeatureVector, IModel, LinearModel, LinearTrainer, SgdRegressor) -> core, indicators
fin_app      (ScenarioConfig INI IO, ScenarioRunner, JSON serialization) -> all of the above
fin_api      (fin::api::ScenarioService: run / run_file / load_file) -> fin_app
```

Front-ends: `src/main.cpp` (the `aiquant` CLI), `src/server/http_main.cpp` (`aiquant_http`), and `bindings/python/aiquant_module.cpp` (`aiquant_api` module).
- The HTTP service and the Python module go through `fin::api::ScenarioService`.
- The CLI's `run-mvp` / `run-config` print the same `ScenarioResult`, and `--json` uses the shared `fin::app::scenario_result_to_json`.

The core pipeline is `fin::app::run_scenario` (`src/fin/app/ScenarioRunner.cpp`):
1. `io::resample_csv_with_stats(ticks_path, timeframe)` builds the candles.
2. `indicators::FeatureBus` turns each candle into a `FeatureRow`. The feature set is configurable: the bus builds one indicator per name from the catalogue in `include/fin/indicators/FeatureSpec.hpp`, and the row carries its `FeatureSchema` so `FeatureVector` can name the columns. Without a `features` key the set is the historical six (close, ema_fast, rsi, macd, macd_signal, macd_hist), in that order. Warmup is all-or-nothing: `std::nullopt` until *every* selected indicator is ready. `FeatureRow::close` is kept outside `values` because the training target is `close[i+1] - close[i]`, which must hold even when `close` is not a selected feature.
3. The first `train_ratio` of rows goes to the trainer the `model` key selects: `ml::train_linear_from_feature_rows` (ridge, the default) or `ml::train_sgd_from_feature_rows`, which wraps `fin::ml::SgdRegressor` — the only `IModel` that implements `fit` and `partial_fit`. Both fit the target `next_close - close`. An SGD run is converted with `SgdRegressor::to_linear_model()`, which folds the running standardizer back into the weights, so the reported weights, the saved model file and `/predict` are shaped exactly like a ridge run.
4. Validation RMSE and a preview are computed on the remaining rows.
5. A second, fresh `FeatureBus` replays all candles. Each candle's model prediction is passed as the *pending* prediction to `Backtester::on_candle` for the **next** candle. The backtester maintains its own EMA/RSI and asks `SignalEngine::eval(snapshot, prediction)` for Buy/Sell/Hold. With `online_update` the replay also calls `partial_fit` on every row past the training split, always one row behind — a row's target is only realized when the next row closes — and `ScenarioResult` reports `online_updates` beside a prequential `online_validation_rmse`.
6. `Backtester::finalize()` returns `Metrics` (final_cash, pnl, return_pct, max_drawdown, trades, wins, losses).

When you change a config field, keep these in sync:
- `ScenarioConfig` (`include/fin/app/ScenarioRunner.hpp`)
- the INI parser (`src/fin/app/ScenarioConfigIO.cpp`)
- JSON output (`src/fin/app/ScenarioSerialization.cpp`)
- CLI flags (`src/main.cpp`)
- the Python dict conversion (`bindings/python/aiquant_module.cpp`)
- `docs/ScenarioConfig.md`

Adding a **feature** is a different list: register it in `feature_catalog()` (`src/fin/indicators/FeatureSpec.cpp`), give it an adapter in `adapters/CandleAdapters.hpp` if one does not exist, add any new period to `FeatureParams` *and* to `ScenarioConfig` (plus `make_feature_params` in `ScenarioRunner.cpp`), and document it in the catalogue table in `docs/ScenarioConfig.md`. Names are canonical and lowercase — no aliases, because the name is written verbatim into the model file.

## CI

GitHub Actions:
- `ci.yml`: Debug build + ctest, on **Ubuntu and macOS** (the macOS job arrived with PR #15).
- `sanitizers.yml`: ASan/UBSan.
- `coverage.yml`: gcov/lcov artifact.
- `release.yml`: on `v*.*.*` tags, packages `build-rel` as a tarball.

Because `AIQUANT_BUILD_PYTHON` defaults to `ON` and configure fails without pybind11, every workflow must install `pybind11-dev` and `python3-dev` via apt (PR #11). Any new workflow that configures the project needs the same step, or `-DAIQUANT_BUILD_PYTHON=OFF`.

## Branch and PR hygiene

- The repo has **"Automatically delete head branches"** enabled, so merging a PR through the GitHub UI or `gh pr merge` deletes the head branch and closes the PR. Prefer `gh pr merge <n> --squash --delete-branch`.
- `.github/workflows/branch-cleanup.yml` is the safety net for merges pushed straight from a clone: on every push to `main`, weekly, or on demand, it deletes unprotected remote branches whose tip is already an ancestor of `main`. It skips `main` and the head branch of any open PR.
- A PR whose head branch is deleted is closed automatically by GitHub, so stale branches and stale PRs are cleaned up by the same mechanism.
