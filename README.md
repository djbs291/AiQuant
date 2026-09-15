# AiQuant

AiQuant is a C++20 engine for quantitative-trading research. It reads tick data from CSV, resamples it into OHLCV candles, computes technical indicators, trains a ridge linear model to predict the next candle's close-to-close delta, and backtests a rule-based signal engine driven by those predictions.

The whole pipeline is described by a single INI "scenario" and can be driven three ways: the `aiquant` CLI, the `aiquant_http` microservice, and the `aiquant_api` Python module.

- Architecture and roadmap: `docs/CppFinancialAIEngine.md`, per-layer designs in `docs/*LayerDesign_*.md`
- Current state, known issues and priorities: `docs/ProjectStatus.md`
- Scenario INI grammar: `docs/ScenarioConfig.md`

## Build

Requires CMake 3.20+, Ninja and a C++20 compiler. Python bindings are ON by default, and configuring **fails** without pybind11 (`brew install pybind11`, `pip install pybind11`, or `apt install pybind11-dev`). Pass `-DAIQUANT_BUILD_PYTHON=OFF` to skip them.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Test

```bash
ctest --test-dir build --output-on-failure   # C++ tests (aiquant_tests)
PYTHONPATH=build python3 tests/python/smoke_test.py   # Python module smoke test
```

## CLI

Subcommands: `features`, `backtest`, `train-linear`, `run-mvp`, `run-config`. Run `./build/aiquant` without arguments for usage.

```bash
./build/aiquant run-config scenarios/mvp.ini            # ready-to-run example scenario
./build/aiquant run-config scenarios/mvp.ini --json
./build/aiquant run-mvp scenarios/ticks_mvp.csv --tf M1 --model-out model.csv
./build/aiquant backtest scenarios/ticks_mvp.csv --model-linear model.csv
```

A scenario needs enough candles to clear indicator warmup: about 33 with the default periods, plus at least 3 feature rows. `scenarios/ticks_mvp.csv` is a synthetic 300-tick file that satisfies this; the small `ticks_*.csv` files written by the tests do not.

## Scenario Configs

`aiquant run-config` executes full scenarios. The supported keys and grammar are documented in `docs/ScenarioConfig.md`, and a ready-to-run example lives at `scenarios/mvp.ini` (pointing to `scenarios/ticks_mvp.csv`). Use these as a template when wiring new experiments.

## C++ / Python API

The `fin::api::ScenarioService` offers a stable programmatic entry point for running scenarios. Link against the `fin_api` static library and call `ScenarioService::run` or `ScenarioService::run_file`. Python bindings are implemented with [pybind11](https://pybind11.readthedocs.io/) (installable via `pip install pybind11`) and expose the same helpers. Build + import example:

```bash
cmake -DAIQUANT_BUILD_PYTHON=ON -S . -B build
cmake --build build --target aiquant_api
PYTHONPATH=build python3 -c "import aiquant_api as aq; print(aq.run_config({'ticks_path': 'scenarios/ticks_mvp.csv'})['metrics'])"
```

Use the interpreter CMake found (printed as `Found Python3` at configure time); the extension module is built against that ABI. `cmake` automatically checks `python -m pybind11 --cmakedir` when locating the package, but you can always pass `-DCMAKE_PREFIX_PATH=$(python -m pybind11 --cmakedir)` explicitly if you use a custom Python environment.

## HTTP Microservice

`aiquant_http` exposes the scenario runner over HTTP. Example usage:

```bash
./build/aiquant_http --port 8080 &
curl -X POST http://localhost:8080/run-file --data "scenarios/mvp.ini"      # body = path on the server
curl -X POST http://localhost:8080/run-config --data-binary @scenarios/mvp.ini  # body = raw INI
```

`POST /run-file` expects the HTTP body to contain a path to an existing scenario file on disk. `POST /run-config` accepts raw INI contents and executes them via a temporary file. Both endpoints return the JSON emitted by the CLI `--json` flag. The service is single-threaded and reads any path the process can reach, so keep it on a trusted network (see `docs/ProjectStatus.md`).
