# Examples

Runnable examples for the CLI, the Python module and the HTTP service. Everything here uses
`scenarios/ticks_mvp.csv`, which ships with the repository, so there is no data to fetch.

Run them from the **repository root** — relative paths resolve against the working directory.

| File | What it shows |
| --- | --- |
| `wide_features.ini` | a scenario with a wider feature set than the default six |
| `sgd_online.ini` | an SGD model that keeps training through the out-of-sample stretch |
| `run_scenario.py` | the `aiquant_api` Python module, including how errors surface |
| `api_calls.sh` | every HTTP endpoint with `curl`, success and failure |
| `dashboard/` | a small page that drives the service from a browser |

## The scenario

```bash
./build/aiquant run-config examples/wide_features.ini --json | jq '.features, .warmup_candles'
```

Omit the `features` key and you get the historical six (`close`, `ema_fast`, `rsi`, `macd`,
`macd_signal`, `macd_hist`). The catalogue of names is in `docs/ScenarioConfig.md`.

## Online learning

```bash
./build/aiquant run-config examples/sgd_online.ini --json \
  | jq '{model, validation_rmse, online_validation_rmse, online_updates}'
```

`model = sgd` trains by stochastic gradient descent instead of the closed-form ridge solver,
and `online_update = true` keeps that model learning through the out-of-sample stretch: one
update per candle that closes, always one row behind, because a row's target is only realized
once the next row closes.

The run reports two errors over the same rows — `validation_rmse` for the model as trained,
and `online_validation_rmse` for those rows scored predict-then-learn. Both scenarios below
use the default six features and the same 186/80 split, so the numbers are comparable:

| scenario | validation RMSE | prequential RMSE | PnL |
| --- | --- | --- | --- |
| `scenarios/mvp.ini` (ridge) | 0.00357 | — | 0.6811 |
| `sgd_online.ini` | 0.00202 | 0.00184 | 0.6811 |

The hyperparameters in the file are tuned for this tick file. The learning rate is what decides
between crawling and diverging, and a model that diverges says so rather than emitting NaNs.

## The Python module

```bash
PYTHONPATH=build python3 examples/run_scenario.py
```

Use the interpreter CMake found — it is printed as `Found Python3` at configure time, and the
extension module is built against that ABI.

## The HTTP service

```bash
./build/aiquant run-mvp scenarios/ticks_mvp.csv --tf M1 --model-out scenarios/model.csv
./build/aiquant_http --port 8080 --root scenarios --static examples/dashboard &
./examples/api_calls.sh 8080
```

`--root` confines both scenario and model paths; `--static` is what serves the dashboard, and
it is off unless you pass it.

## The dashboard

With the service started as above, open <http://localhost:8080/>. Three panels, one per thing
the engine can actually do: run a scenario, predict from feature values, and evaluate the
signal rules. It is plain HTML, CSS and JavaScript — no build step, and nothing loaded from a
CDN, so it works offline.
