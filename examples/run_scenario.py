#!/usr/bin/env python3
"""Run scenarios through the aiquant_api Python module.

Build the module first, then run this from the repo root with the interpreter CMake found
(it is printed as "Found Python3" at configure time):

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
    PYTHONPATH=build python3 examples/run_scenario.py

Exits non-zero if anything fails, so CI can run it as a check that the bindings still work.
"""

import os
import sys

try:
    import aiquant_api as aq
except ImportError:
    sys.exit("aiquant_api not importable. Build it and set PYTHONPATH=build (see the docstring).")

TICKS = "scenarios/ticks_mvp.csv"


def show(title, result):
    metrics = result["metrics"]
    print(f"\n{title}")
    print(f"  features : {', '.join(result['features'])}")
    print(f"  candles  : {result['candles']} (warmup {result['warmup_candles']}, "
          f"{result['feature_rows']} feature rows)")
    print(f"  val RMSE : {result['validation_rmse']:.6f}")
    print(f"  trades   : {metrics['trades']} ({metrics['wins']}W / {metrics['losses']}L), "
          f"PnL {metrics['pnl']:.4f}")


def main():
    if not os.path.exists(TICKS):
        sys.exit(f"{TICKS} not found — run this from the repository root.")

    # 1. Defaults: omit "features" and you get the historical six.
    show("default feature set", aq.run_config({"ticks_path": TICKS, "timeframe": "M1"}))

    # 2. A wider set. Every name comes from the catalogue in docs/ScenarioConfig.md.
    show("wider feature set", aq.run_config({
        "ticks_path": TICKS,
        "timeframe": "M1",
        "features": ["close", "ema_fast", "rsi", "atr", "adx", "bb_mid"],
        "atr_period": 14,
        "adx_period": 14,
    }))

    # 3. The same thing from an INI file on disk.
    show("from examples/wide_features.ini", aq.run_file("examples/wide_features.ini"))

    # 4. Errors are exceptions, not silent fallbacks: an unknown feature name is refused.
    try:
        aq.run_config({"ticks_path": TICKS, "features": ["close", "bogus"]})
    except ValueError as exc:
        print(f"\nunknown feature rejected, as it should be: {exc}")
    else:
        sys.exit("an unknown feature name should have raised")

    print("\nOK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
