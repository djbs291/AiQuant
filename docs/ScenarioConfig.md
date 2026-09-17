# Scenario Config Format

`aiquant run-config` consumes an INI-like text file processed by `load_scenario_file` (see `src/fin/app/ScenarioConfigIO.cpp`). Lines look like:

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
| `features` | list | `close,ema_fast,rsi,macd,macd_signal,macd_hist` | Comma-separated model feature set, in column order. An unknown name is an error (unlike an unknown key, which is ignored). See the catalogue below. |
| `sma`, `sma_period` | size_t | `14` | SMA period, for the `sma` feature. |
| `bb_period` | size_t | `20` | Bollinger period, for `bb_upper`/`bb_mid`/`bb_lower`. |
| `bb_k` | double | `2.0` | Bollinger band width in standard deviations. |
| `atr`, `atr_period` | size_t | `14` | ATR period. |
| `adx`, `adx_period` | size_t | `14` | ADX period, also used by `plus_di`/`minus_di`. |
| `stoch_k`, `stoch_k_period` | size_t | `14` | Stochastic %K period. |
| `stoch_d`, `stoch_d_period` | size_t | `3` | Stochastic %D period. |
| `zscore`, `zscore_period` | size_t | `20` | Z-Score window. |
| `momentum`, `momentum_period` | size_t | `10` | Momentum lookback. |
| `model` | enum | `ridge` | `ridge` (the closed-form solver, and the historical behaviour) or `sgd` (stochastic gradient descent). The value is case-folded; `linear` is an alias for `ridge`. |
| `sgd_learning_rate`, `sgd_lr` | double | `0.01` | Step size at the first update. Read only when `model = sgd`. |
| `sgd_l2` | double | `1e-6` | L2 penalty on the weights. The bias is deliberately excluded. |
| `sgd_epochs` | size_t | `10` | Sequential passes over the training rows. Must be at least 1. |
| `sgd_power_t` | double | `0.25` | Decay exponent: `eta_t = sgd_learning_rate / (1 + t)^sgd_power_t`. Zero holds the rate constant. |
| `sgd_standardize` | bool | `true` | Center and scale each column by its running mean and standard deviation before each update. |
| `online_update`, `online` | bool | `false` | Keep training through the out-of-sample stretch. Requires `model = sgd`. |

## Feature catalogue

`features` accepts these names: `close`, `sma`, `ema_fast`, `ema_slow`, `rsi`, `macd`, `macd_signal`, `macd_hist`, `bb_upper`, `bb_mid`, `bb_lower`, `atr`, `adx`, `plus_di`, `minus_di`, `stoch_k`, `stoch_d`, `vwap`, `zscore`, `momentum`.

Two things to keep in mind:

- **Warmup is all-or-nothing.** A row is emitted only once *every* selected feature is ready, so adding a long-warmup indicator shortens the usable series. `adx` needs `2N-1` candles, and with the default period that is 27 before the first row. The resolved set and the row counts are reported in the JSON output and in the error raised when too few rows survive.
- **`vwap` is session-scoped.** It accumulates from the first candle and only restarts when the bus is reset, so on a long file it drifts toward a whole-file average rather than a daily one.

The order of the list is the column order of the model, and it is recorded in the trained model file.

## Model and online learning

`model` picks the trainer. `ridge` is the closed-form solver the engine has always used, and it
stays the default: a scenario that does not mention `model` trains exactly the model it did
before this key existed.

`sgd` fits the same target, `next_close - close`, by stochastic gradient descent. Two things
follow from that:

- **It can keep learning.** `online_update = true` hands the model every candle that closes
  past the training split, as one `partial_fit` on the delta that candle has just realized.
  The update always runs one row behind, because a row's target is only known once the next
  row closes, so the replay never reads a candle before it has happened. The run then reports
  two errors over the same rows: `validation_rmse` for the model as trained, and
  `online_validation_rmse` for the same rows scored predict-then-learn. `online_updates` says
  how many updates were applied, and it equals `validation_samples` whenever the two agree on
  which rows are out of sample.
- **It needs its features on a common scale.** `close` is around 100 while `macd` is around
  0.01, and one learning rate cannot serve both. `sgd_standardize` (on by default) centers and
  scales each column by its running mean and standard deviation. With it off, a rate that
  suits one column overshoots on the other; the model raises an error naming the learning rate
  rather than producing silent NaNs.

Three caveats worth knowing before trusting a number:

- `model_out` writes the model **as trained**, before any online update. The file is the
  artifact of the training run, not of the replay that follows it.
- The standardizer's moments keep moving, so a prediction depends on the moments at the time
  it was made. The same feature vector can score differently after further updates.
- An SGD run is persisted and served as a plain linear model: the standardizer is folded back
  into the weights (`w'_j = w_j / sigma_j`), so the reported weights, the saved file,
  `aiquant backtest --model-linear` and the HTTP `/predict` endpoint all behave exactly as
  they do for a ridge run.

`online_update = true` without `model = sgd` is refused rather than ignored: the ridge model
has no `partial_fit`, and a run that reported online learning without doing any would be worse
than an error.

See `examples/sgd_online.ini` for a tuned, runnable configuration.

## Boolean Parsing

Boolean fields accept the tokens `true/false`, `1/0`, `yes/no`, and `on/off` (case-insensitive). Invalid tokens abort the load with an error message.

## Example

See `scenarios/mvp.ini` for a ready-to-run configuration. It points at `scenarios/ticks_mvp.csv`, a synthetic 300-tick file that is long enough to clear indicator warmup:

```bash
./build/aiquant run-config scenarios/mvp.ini
```

Run it from the repo root, since relative paths in the INI are resolved from the working directory.
