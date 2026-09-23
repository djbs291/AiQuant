# Scenario Config Format

`aiquant run-config` consumes an INI-like text file processed by `load_scenario_file` (see `src/fin/app/ScenarioConfigIO.cpp`). Lines look like:

```
key = value  # optional inline comment
```

- Leading/trailing whitespace is ignored.
- Empty lines or ones starting with `#` are ignored.
- Inline comments use `#` as well, but only when the `#` follows whitespace: `rsi = 10 # note`
  is a comment, while `ticks = runs#3.csv` keeps its `#`. A value glued to a `#`, as in
  `rsi = 10#x`, is therefore not a number and is refused rather than read as `10`.
- Keys are case-insensitive; values are case-sensitive except for boolean tokens.
- Each setting may appear once. A repeat is an error, whichever spelling it uses: `rsi` twice,
  `RSI` after `rsi`, an alias after its key (`sma_period` after `sma`), or both
  `use_ema_crossover` and `no_ema_xover`, which set the same flag.

## Supported Keys

| Key aliases | Type | Default | Notes |
| --- | --- | --- | --- |
| `ticks`, `ticks_path`, `data` | string | **required** | CSV with raw ticks. Relative paths are resolved from the working directory. |
| `symbol` | string | the first tick's symbol | Which instrument to take out of the file. Ticks for any other symbol are skipped and counted, never merged into the same bar. Case-sensitive, unlike the keys. See below. |
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
| `features` | list | `close,ema_fast,rsi,macd,macd_signal,macd_hist` | Comma-separated model feature set, in column order. An unknown name is an error. See the catalogue below. |
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

## Symbols

A scenario produces candles for **one** instrument. A resampler aggregates one series by
construction, so the tick file is filtered before it reaches one:

- Omit `symbol` and the run binds to the first tick in the file. A single-symbol file — which
  is what every scenario here uses — therefore needs no key at all, and behaves as it always
  did.
- Name a `symbol` and it is selected out of a file that holds several. Ticks belonging to
  anything else are skipped and counted, and the count comes back as `ticks_other_symbol` in
  the JSON alongside the resolved `symbol`.

This used to be silent, and wrong: the symbol was parsed off each tick and then dropped, so a
file holding `ABC` around 100 and `XYZ` around 900 produced bars that opened on one instrument
and closed on the other, with a high and a low that straddled both. Nothing said so.

Naming a symbol that the file does not contain yields no candles rather than the wrong ones,
which then fails the usual way — not enough data to get through indicator warmup.

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

## Validation

The load fails, naming the key and the line, rather than accepting a value that cannot mean
anything:

- **An unknown key is an error.** It used to be ignored. Silence meant a typo trained a
  different model than the one asked for and said nothing about it: `rsi_peroid = 20` left the
  period at its default of 14 and the run looked entirely normal.
- **A key set twice is an error**, naming both lines. The later value used to win silently,
  which is the same failure as the typo above: the file says one thing and the run does another.
- **Every period must be at least 1.** A zero period does not fail where it is written — the
  indicator simply never becomes ready, and the run dies much later with `Insufficient data
  after indicator warmup`, which blames the data for a configuration mistake.
- **`bb_k` must be greater than zero** and **`ridge` must not be negative.** A negative ridge
  term is not regularization but its opposite; on the sample scenario it quietly made the
  fitted model about thirty times worse.
- **Every number must be finite.** `nan`, `inf` and `infinity` are refused at the line that
  holds them. They used to parse, and every range check is a comparison that NaN passes by
  being false both ways; `train_ratio = nan` went on to a NaN-to-integer cast, which is
  undefined behaviour.
- **`cash` and `qty` must be greater than zero, and `fee` must not be negative.** The
  backtester trusts all three: on the MVP scenario `qty = -5` reported a return of +844% with
  zero trades, because buying a negative quantity pays out, and `fee = -1000` turned 18
  trades into 18 wins.
- **`rsi_buy` and `rsi_sell` must lie in `[0, 100]`**, the range an RSI can take.
- **The `sgd_*` options are checked even when `model = ridge`:** `sgd_learning_rate > 0`,
  `sgd_l2 >= 0`, `sgd_power_t >= 0`, `sgd_epochs >= 1`. A scenario that carries a bad value
  it does not use is still carrying a mistake.
- **`online_update = true` requires `model = sgd`**, in either order in the file.

`train_ratio` is the exception: any finite value is accepted and *clamped* to `[0.1, 0.95]`,
which is long-standing behaviour the runner relies on.

These rules are not specific to the INI format. They live in
`fin::app::validate_scenario_config`, which `run_scenario` also calls, so a config built from
CLI flags, a Python dict or the HTTP JSON is refused the same way (`std::invalid_argument`,
which Python sees as `ValueError`).

Note that `load_scenario_file` fills the config as it parses, so a failed load leaves partial
values behind. Pass a fresh `ScenarioConfig` for each file.

## Boolean Parsing

Boolean fields accept the tokens `true/false`, `1/0`, `yes/no`, and `on/off` (case-insensitive). Invalid tokens abort the load with an error message.

## Example

See `scenarios/mvp.ini` for a ready-to-run configuration. It points at `scenarios/ticks_mvp.csv`, a synthetic 300-tick file that is long enough to clear indicator warmup:

```bash
./build/aiquant run-config scenarios/mvp.ini
```

Run it from the repo root, since relative paths in the INI are resolved from the working directory.
