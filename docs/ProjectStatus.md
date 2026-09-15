# AiQuant — Project Status & Roadmap

*Snapshot taken 2026-09-13 at `main` = `fa04ec0` (last commit 2025-11-09). Updated 2026-09-15: PR #11 merged (`a1e7e90`), CI green on `main`.*

## 1. What the project is

AiQuant is a C++20 engine for algorithmic-trading research. It ingests tick data from CSV, resamples it into OHLCV candles, and computes technical indicators. It then trains a ridge linear model to predict the next candle's close-to-close delta, and backtests a rule-based signal engine that also uses the model's prediction. Everything can be driven by an INI "scenario" through a CLI, a small HTTP service, or a Python module.

The target architecture is described in `docs/CppFinancialAIEngine.md`, with per-layer designs in `docs/CoreLayerDesign_*`, `docs/IndicatorsLayerDesign_*` and `docs/IOLayerDesign_*`. The MVP described there is implemented. The real-time, performance and integration parts of that design are not implemented yet.

## 2. History

57 commits: 31 by the `codex` agent, 36 by the maintainer (`djbs291` / David Silva).

| Period | Commits | What landed |
| --- | --- | --- |
| 2025-08-22 → 08-29 | 13 | Core domain types with tests; indicators RSI, SMA, EMA, Bollinger Bands, MACD, Stochastic, ATR, ADX, VWAP, Z-Score and Momentum; indicator adapters |
| 2025-09-01 → 09-12 | ~25 | CSV tick reader, `TickToCandleResampler`, `Pipeline` helpers, `FeatureBus`, design docs; GitHub Actions CI, sanitizer, coverage and release workflows (many iterations on lcov setup) |
| 2025-09-13 → 10-03 | ~10 | `Backtester` and CLI `backtest` / `features` commands with timeframe options; `FeatureVector`, `LinearModel`, `LinearTrainer`; CLI `train-linear` and the end-to-end `run-mvp` |
| 2025-10-12 → 10-26 | 7 | `ScenarioRunner`, INI scenario loading and validation, `fin::api::ScenarioService`, JSON serialization, `aiquant_http` microservice, first Python bindings |
| 2025-11-09 | 1 | Python bindings rewritten on pybind11; pybind11 made mandatory when `AIQUANT_BUILD_PYTHON=ON` (the default) |

## 3. What is implemented

| Layer | Library | Contents |
| --- | --- | --- |
| Core | `fin_core` | `Timestamp`, `Price`, `Volume`, `Symbol`, `Tick`, `Candle`, `RingBuffer` |
| Indicators | `fin_indicators` | 11 streaming indicators; candle/scalar/price-volume adapters; `FeatureBus`, which emits `FeatureRow{close, ema_fast, rsi, macd, macd_signal, macd_hist}` |
| IO | `fin_io` | `FileTickSource` (CSV, with parse/skip stats), resampler for S1/S5/M1/M5/H1 (EOF flush, out-of-order handling), `resample_csv_with_stats` |
| Signal | `fin_signal` | `SignalEngine`: RSI thresholds + optional EMA crossover + optional model prediction → Buy/Sell/Hold |
| Backtest | `fin_backtest` | Long-only `Backtester` with cash, quantity and fees; trade list; `Metrics` (PnL, return %, max drawdown, trades, wins, losses) |
| ML | `fin_ml` | `IModel` interface, `LinearModel` (positional/named weights, save and load), ridge `LinearTrainer` |
| App | `fin_app` | `ScenarioConfig` INI parser, `run_scenario` (resample → features → train/validate → backtest), JSON output |
| API | `fin_api` | `ScenarioService::run / run_file / load_file` |
| Front-ends | executables and module | `aiquant` CLI (`features`, `backtest`, `train-linear`, `run-mvp`, `run-config`), `aiquant_http` (POST `/run-file`, `/run-config`), Python module `aiquant_api` (`run_file`, `run_config`, `load_file`) |
| Tests | `aiquant_tests` | 69 test cases: core, all indicators, IO/resampler, feature pipeline, signal, backtester, linear model, scenario config and API |
| CI | GitHub Actions | Ubuntu build + test, ASan/UBSan, lcov coverage, tag-based release tarball |

## 4. Verification results (2026-09-13)

**Environment:** macOS 26.6.2 arm64, Apple clang 21.0.0, CMake 4.4.3, Ninja 1.13.2, pybind11 3.1.0 (Homebrew), Python 3.9.6 (Apple Command Line Tools).

| Check | Result |
| --- | --- |
| Configure + Debug build (`-G Ninja`, Python ON) | ✅ 70/70 steps in about 5 s. 0 compiler warnings with `-Wall -Wextra -Wpedantic`; 3 linker warnings `ignoring duplicate libraries` |
| `ctest` / `aiquant_tests` | ✅ 69/69 test cases pass |
| ASan + UBSan build (same flags as `sanitizers.yml`) | ✅ 69/69 pass, no sanitizer reports |
| Build with `-DAIQUANT_BUILD_PYTHON=OFF` | ✅ builds, tests pass |
| `aiquant features --tf M5`, `run-mvp --json --model-out`, `run-config --json`, `backtest --model-linear` on a synthetic 300-tick M1 CSV | ✅ all run. `run-mvp`: 300 candles, warmup 33, 267 feature rows, validation RMSE 0.2376, 14 trades (9 W / 5 L), PnL +20.97 |
| Python `import aiquant_api`: `load_file`, `run_file`, `run_config` | ✅ return dicts with metrics; bad input raises `RuntimeError` |
| `aiquant_http`: POST `/run-config`, `/run-file` | ✅ 200 with JSON. Unknown endpoint → 404, GET → 405, missing file → 500 |
| GitHub Actions on `main` | ❌ CI, Sanitizers and Coverage **all fail** for `fa04ec0`; all passed for the previous commit `88a96c8`. ✅ All pass again for `a1e7e90` (PR #11 merge, 2026-09-14) |
| GitHub Actions on PR #11 (`fix/ci-pybind11`, 2026-09-14) | ✅ Build & Test, ASan/UBSan and Coverage all pass once `pybind11-dev` is installed. Line coverage 82.2% (1063/1293 lines) |

## 5. Known issues

### High
1. **~~CI has been red on `main` since `fa04ec0`.~~ Fixed 2026-09-14 by PR #11 (`a1e7e90`); CI, Sanitizers and Coverage pass on `main`.** That commit made CMake stop with `FATAL_ERROR` when pybind11 is missing while `AIQUANT_BUILD_PYTHON` defaults to `ON`, and none of the workflows installs pybind11 or passes `-DAIQUANT_BUILD_PYTHON=OFF`. All three jobs fail about 30 s in with exit code 1, which fits a configure-step failure. Reproduced locally: configuring with pybind11 hidden (`-DCMAKE_DISABLE_FIND_PACKAGE_pybind11=ON`, no `pip` pybind11) stops at `CMakeLists.txt:121` with `pybind11 not found`. **Fix:** PR #11 (`fix/ci-pybind11`) installs `pybind11-dev` and `python3-dev` in all workflows. All three checks pass on it; `main` stays red until that PR is merged.

### Medium
2. **`aiquant_http /run-file` opens any path the server process can read.** The request body is used as a filesystem path, which is unsafe if the port is reachable by others. The server is also single-threaded and blocking, a missing file returns 500 instead of a 4xx, and there is no `/health` endpoint.
3. **`run-mvp --json` and `run-config --json` are not machine-readable:** the human-readable report is printed to stdout before the JSON, so the output can't be piped into `jq`. The HTTP and Python paths are unaffected.
4. **Test harness limitations:**
   - Tests use a bundled 95-line "minicatch" (`tests/catch2/catch.hpp`), so filtering by tag or name is not possible and there is no `SECTION`.
   - ~~Several tests write CSV fixtures into the current working directory.~~ Fixed 2026-09-16: fixtures go to the system temp dir via `tests/TestTempFiles.hpp` and are removed on scope exit.
   - ~~The committed root files `ticks_sample.csv`, `ticks_features.csv` and `ticks_sample_pipeline.csv` are copies of those generated fixtures, and running the tests from the repo root rewrites them.~~ Deleted 2026-09-16; `/ticks_*.csv` is now ignored.
5. **Documentation drift.** Fixed 2026-09-16, except the Notion links:
   - ~~`README.md` and `docs/ScenarioConfig.md` point to `scenarios/mvp.ini`, which does not exist.~~ `scenarios/mvp.ini` and `scenarios/ticks_mvp.csv` (300 synthetic ticks) are now in the repo.
   - ~~`docs/ScenarioConfig.md` says the INI parser is in `src/main.cpp`~~; it points to `src/fin/app/ScenarioConfigIO.cpp`.
   - ~~`docs/CppFinancialAIEngine.md` says the Python module has no external dependencies~~; the pybind11 requirement, the module map and the folder structure now match the repo, and the broken diagram attachment is gone.
   - The layer docs link to external Notion pages for per-indicator designs. **Still open** — the content is not in the repo.
   - ~~`README.md` has no project introduction or build/test instructions.~~ Rewritten with an overview and build/test/CLI/API/HTTP sections.
   - ~~The README `curl` example posts the INI *contents* to `/run-file`, which expects a *path* and answers `Failed to open scenario file`.~~ Fixed 2026-09-16: `/run-file` gets the path, `/run-config` gets the contents.

### Low
6. ~~The linker reports duplicate static libraries because the executable targets repeat the full library list even though the dependencies are already `PUBLIC`.~~ Fixed 2026-09-16: the executables and the test binary link `fin_api` only; the warnings are gone.
7. ~~`.gitignore` ignores `.vscode/`, but `.vscode/tasks.json` (a generic g++ single-file task) is committed.~~ Removed 2026-09-16; the file was a single-file g++ task unrelated to the CMake build.
8. `ci.yml` runs `apt-get install` before `apt-get update` and builds lcov without using it. `release.yml` tars the whole build directory for Linux x86_64 only.
9. ~~Seven stale, unmerged `codex/*` branches (CI/lcov experiments from 2025-09) remain on the remote.~~ Fixed 2026-09-16: the nine `codex/*` branches were deleted and PRs #4, #6, #7, #8 and #10 closed. Their tip SHAs are recorded in the closing comments, so the work can be restored if needed. Going forward, `delete_branch_on_merge` is enabled and `.github/workflows/branch-cleanup.yml` removes branches already merged into `main`.
10. `IModel::fit` / `partial_fit` default to throwing `logic_error`. Training happens only through the free function `train_linear_from_feature_rows`, and there is no online learning.
11. `FeatureBus`, and therefore the model and scenarios, uses only EMA/RSI/MACD. Bollinger Bands, ATR, ADX, Stochastic, VWAP, Z-Score and Momentum are implemented and tested but not available as model features.

## 6. Next steps (prioritized)

### P0: restore a green `main`
- ✅ Merge PR #11. It installs `pybind11-dev` and `python3-dev` in every workflow, fixes the `apt-get` ordering in `ci.yml` and drops the unused lcov install there. Merged 2026-09-14.
- ✅ Add a CI step that exercises the built `aiquant_api` module. `ci.yml` now runs `tests/python/smoke_test.py`: import, `run_config` / `load_file` / `run_file` on a synthetic 300-tick CSV, and the `ValueError` / `RuntimeError` error paths.

### P1: quick fixes and repo hygiene
- ✅ Add `scenarios/mvp.ini` plus `scenarios/ticks_mvp.csv` (300 synthetic ticks, enough to clear warmup).
- ✅ Correct the doc drift listed in §5.5 (except the Notion links) and add a build/test/usage intro to `README.md`.
- ✅ Make tests write fixtures to a temporary directory, delete the generated CSVs from the repo root, and ignore them.
- ✅ Remove `.vscode/tasks.json`. ⬜ Delete the stale `codex/*` branches and close PRs #7, #8 and #10 (needs a maintainer decision).
- ✅ Simplify `target_link_libraries` on the executables to rely on transitive `PUBLIC` dependencies.

### P2: hardening and test infrastructure
- Make `--json` emit only JSON on stdout (send the report to stderr or drop it when `--json` is given).
- HTTP service:
  - restrict `/run-file` to a configured scenarios directory;
  - map client errors to 4xx;
  - add `/health`;
  - add request size limits and concurrent request handling.
- Fetch real Catch2 v3 (e.g. with `FetchContent`) to get filtering, `SECTION`s and `catch_discover_tests`; keep minicatch only as an offline fallback.
- Add tests for the HTTP service and the Python module, plus golden tests against TA-Lib for the indicators, as the architecture doc plans.
- Add a macOS CI job; the project builds and passes cleanly on Apple Silicon.

### P3: roadmap from the architecture doc
- **Modelling:** make the `FeatureBus` feature set configurable (expose all 11 indicators), implement `IModel::fit` / `partial_fit` for online learning, and add richer models (the doc mentions MLP).
- **Streaming:** real-time pipeline (tick feed → resampler → indicators → model → signal dispatch), parallel per-symbol workers, lock-free MPMC queues, no allocations on the hot path, SIMD (NEON/AVX).
- **IO:** JSON reader, volume/tick/event bars, WebSocket feed, Kafka/Redis connectors.
- **Interfaces:** HTTP `/predict` and `/signal` endpoints, an `examples/` directory, a signals dashboard.
- **Trading:** portfolio risk models, multi-asset simulation, order execution and broker APIs.
- **Quality:** property-based tests and fuzzing of the CSV/INI readers and streaming update paths.
