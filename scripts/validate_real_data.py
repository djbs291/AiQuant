#!/usr/bin/env python3
"""Validate the AiQuant pipeline on real market data.

Fetches recent klines (candlesticks) from Binance's public REST API -- no key, no account --
turns them into the tick CSV the engine reads, and runs `aiquant run-mvp` on them for one or
more models, printing the out-of-sample back-test metrics beside the in-sample ones.

This exists to replace synthetic numbers with real ones: the project's example scenarios use
smooth synthetic ticks, which the docs are careful to say are "no basis for choosing" anything.
This gives honest figures on real price series instead.

It fetches at a fine interval (default 1m) and analyses at a coarser timeframe (default M5), so
every analysed candle is built from several real sub-bars and its high/low/open/close are real,
not flat. The model's target is the next candle's close-to-close move.

Not run by CI: it reaches the network. Requires only the Python standard library and a built
`./build/aiquant`.

Usage:
    python3 scripts/validate_real_data.py
    python3 scripts/validate_real_data.py --symbols BTCUSDT ETHUSDT --count 5000 --tf M5 \
        --models ridge sgd mlp

IMPORTANT: back-test metrics on a handful of trades over one window are a sanity check, not a
performance claim and not investment advice. Treat them as "does the engine run on real data and
produce sane numbers", nothing more.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request

BINANCE_KLINES = "https://api.binance.com/api/v3/klines"
# Binance caps a single klines request at 1000 rows.
MAX_PER_REQUEST = 1000


def fetch_klines(symbol, interval, count, base_url=BINANCE_KLINES):
    """Returns the `count` most recent klines for `symbol`, oldest first.

    Each kline is [open_time_ms, open, high, low, close, volume, ...]; we page backwards with
    endTime until we have enough.
    """
    collected = []
    end_time = None
    while len(collected) < count:
        want = min(MAX_PER_REQUEST, count - len(collected))
        url = f"{base_url}?symbol={symbol}&interval={interval}&limit={want}"
        if end_time is not None:
            url += f"&endTime={end_time}"
        with urllib.request.urlopen(url, timeout=30) as resp:
            batch = json.load(resp)
        if not batch:
            break
        collected = batch + collected
        # Next page ends one millisecond before this batch's first open time.
        end_time = batch[0][0] - 1
        if len(batch) < want:
            break
        time.sleep(0.2)  # be gentle with the public endpoint
    return collected[-count:]


def write_tick_csv(path, symbol, klines):
    """Writes one tick per kline (its close at its open time), in the engine's CSV format."""
    with open(path, "w") as f:
        f.write("Timestamp,symbol,price,volume\n")
        for k in klines:
            open_time_ms, _open, _high, _low, close, volume = k[0], k[1], k[2], k[3], k[4], k[5]
            f.write(f"{int(open_time_ms)},{symbol},{close},{volume}\n")


def run_model(binary, csv_path, timeframe, model, extra_args):
    """Runs `aiquant run-mvp` and returns the parsed JSON result, or raises on failure."""
    cmd = [binary, "run-mvp", csv_path, "--tf", timeframe, "--model", model, "--json"] + extra_args
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"{model}: aiquant exited {proc.returncode}: {proc.stderr.strip()}")
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{model}: could not parse JSON output: {exc}\n{proc.stdout[:400]}")


def fmt_metrics(m):
    return (f"trades={m['trades']:>3}  pnl={m['pnl']:+.4f}  "
            f"ret={m['return_pct']:+.3f}%  maxDD={m['max_drawdown']:.3f}%  "
            f"W/L={m['wins']}/{m['losses']}")


def main():
    parser = argparse.ArgumentParser(description="Validate AiQuant on real Binance data.")
    parser.add_argument("--symbols", nargs="+", default=["BTCUSDT", "ETHUSDT"])
    parser.add_argument("--interval", default="1m", help="kline interval to fetch (default 1m)")
    parser.add_argument("--count", type=int, default=3000, help="klines per symbol (default 3000)")
    parser.add_argument("--tf", default="M5", help="analysis timeframe S1|S5|M1|M5|H1 (default M5)")
    parser.add_argument("--models", nargs="+", default=["ridge", "sgd", "mlp"])
    parser.add_argument("--binary", default=os.path.join("build", "aiquant"))
    parser.add_argument("--keep-csv", action="store_true", help="keep the generated tick CSVs")
    parser.add_argument("--extra", nargs=argparse.REMAINDER, default=[],
                        help="extra flags passed through to run-mvp (after --extra)")
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"{args.binary} not found; build the project first (cmake --build build)", file=sys.stderr)
        return 2

    print("AiQuant validation on real market data (Binance public klines)")
    print(f"  fetch: {args.count} x {args.interval} per symbol   analyse: {args.tf}   "
          f"models: {', '.join(args.models)}")
    print("  NOTE: a few trades over one window is a sanity check, not a performance claim.\n")

    for symbol in args.symbols:
        try:
            klines = fetch_klines(symbol, args.interval, args.count)
        except Exception as exc:  # noqa: BLE001 - surface any network/parse failure plainly
            print(f"{symbol}: failed to fetch klines: {exc}", file=sys.stderr)
            continue
        if len(klines) < 100:
            print(f"{symbol}: only {len(klines)} klines returned; skipping", file=sys.stderr)
            continue

        fd, csv_path = tempfile.mkstemp(prefix=f"aiquant_{symbol}_", suffix=".csv")
        os.close(fd)
        write_tick_csv(csv_path, symbol, klines)

        first = int(klines[0][0]) // 1000
        last = int(klines[-1][0]) // 1000
        span_hours = (last - first) / 3600.0
        print(f"== {symbol} ==  {len(klines)} {args.interval} bars  (~{span_hours:.1f}h of history)")

        try:
            for model in args.models:
                try:
                    result = run_model(args.binary, csv_path, args.tf, model, args.extra)
                except Exception as exc:  # noqa: BLE001
                    print(f"  {model:<6} ERROR: {exc}", file=sys.stderr)
                    continue
                oos = result["metrics"]
                ins = result["metrics_in_sample"]
                print(f"  {model:<6} candles={result['candles']:>4} "
                      f"feat_rows={result['feature_rows']:>4} "
                      f"val_RMSE={result['validation_rmse']:.5f}")
                print(f"           out-of-sample  {fmt_metrics(oos)}")
                print(f"           in-sample      {fmt_metrics(ins)}")
        finally:
            if args.keep_csv:
                print(f"  (tick CSV kept at {csv_path})")
            else:
                os.remove(csv_path)
        print()

    print("Reminder: real data, real numbers -- but one window and few trades. Not advice.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
