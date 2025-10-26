# Scenario Config Format

`aiquant run-config` consumes an INI-like text file processed by `load_scenario_file` (see `src/main.cpp`). Lines look like:

```
key = value  # optional inline comment
```

- Leading/trailing whitespace is ignored.
- Empty lines or ones starting with `#` are ignored.
- Inline comments use `#` as well; everything after it is removed.
- Keys are case-insensitive; values are case-sensitive except for boolean tokens.

## Supported Keys

| Key aliases | Type | Default | Notes |
| --- | --- | --- | --- |
| `ticks`, `ticks_path`, `data` | string | **required** | CSV with raw ticks. Relative paths are resolved from the working directory. |
| `tf`, `timeframe` | enum | `M1` | One of `S1`, `S5`, `M1`, `M5`, `H1`. |
| `train_ratio` | double | `0.7` | Clamped to `[0.1, 0.95]`. |
| `ridge`, `ridge_lambda` | double | `1e-6` | Ridge regularization term for linear model. |
| `ema_fast` | size_t | `12` | Fast EMA window (candles). |
| `ema_slow` | size_t | `26` | Slow EMA window. |
| `rsi` | size_t | `14` | RSI period. |
| `macd_fast` | size_t | `12` | MACD fast EMA. |
| `macd_slow` | size_t | `26` | MACD slow EMA. |
| `macd_signal` | size_t | `9` | MACD signal line EMA. |
| `rsi_buy` | double | `30.0` | RSI threshold to buy (<=). |
| `rsi_sell` | double | `70.0` | RSI threshold to sell (>=). |
| `use_ema_crossover` | bool | `true` | Accepts `true/false`, `1/0`, `yes/no`, `on/off`. |
| `no_ema_xover` | bool | — | Inverse toggle; `true` disables EMA crossover checks. |
| `cash`, `initial_cash` | double | engine default | Starting account cash. |
| `qty`, `trade_qty` | double | engine default | Quantity per trade. |
| `fee`, `fee_per_trade` | double | engine default | Flat fee per trade. |
| `model_out`, `model_output` | string | none | Save trained linear model to this path. |
| `preview`, `preview_limit` | size_t | `3` | Rows of validation preview copied to stdout. |

## Boolean Parsing

Boolean fields accept the tokens `true/false`, `1/0`, `yes/no`, and `on/off` (case-insensitive). Invalid tokens abort the load with an error message.

## Example

See `scenarios/mvp.ini` for a ready-to-run configuration wired up to the sample CSVs shipped in the repo.
