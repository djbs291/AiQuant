"""Smoke test for the aiquant_api Python module.

Run with the interpreter CMake found and the build dir on PYTHONPATH:
    PYTHONPATH=build python3 tests/python/smoke_test.py
"""

import math
import os
import sys
import tempfile

import aiquant_api as aq

TICKS = 300
TICK_MS = 60_000


def write_ticks(path):
    """One tick per minute, so M1 yields TICKS candles (enough for warmup)."""
    with open(path, "w") as f:
        f.write("Timestamp,symbol,price,volume\n")
        for i in range(TICKS):
            price = 100.0 + 0.05 * i + 2.0 * math.sin(i / 5.0)
            f.write(f"{1693492800000 + i * TICK_MS},ABC,{price:.4f},1\n")


def expect_raises(exc_type, fn, *args):
    try:
        fn(*args)
    except exc_type:
        return
    raise AssertionError(f"{fn.__name__}{args!r} did not raise {exc_type.__name__}")


def check_result(res):
    assert res["candles"] == TICKS, res["candles"]
    assert res["feature_rows"] >= 3, res["feature_rows"]
    assert res["training_samples"] > 0
    assert math.isfinite(res["validation_rmse"])
    for key in ("final_cash", "pnl", "return_pct", "trades", "wins", "losses", "max_drawdown"):
        assert key in res["metrics"], key


def main():
    for name in ("run_config", "run_file", "load_file"):
        assert callable(getattr(aq, name, None)), name

    with tempfile.TemporaryDirectory() as tmp:
        ticks = os.path.join(tmp, "ticks.csv")
        write_ticks(ticks)

        from_dict = aq.run_config({"ticks_path": ticks, "timeframe": "M1"})
        check_result(from_dict)

        ini = os.path.join(tmp, "scenario.ini")
        with open(ini, "w") as f:
            f.write(f"ticks = {ticks}\ntf = M1\n")
        cfg = aq.load_file(ini)
        assert cfg["ticks_path"] == ticks and cfg["timeframe"] == "M1", cfg

        from_file = aq.run_file(ini)
        check_result(from_file)
        assert from_file["metrics"] == from_dict["metrics"], (from_file["metrics"], from_dict["metrics"])

        expect_raises(ValueError, aq.run_config, {})
        expect_raises(ValueError, aq.run_config, {"ticks_path": ticks, "timeframe": "X9"})
        expect_raises(RuntimeError, aq.load_file, os.path.join(tmp, "missing.ini"))

        # Configurable feature set: the list goes in and the resolved set comes back.
        assert from_dict["features"] == ["close", "ema_fast", "rsi", "macd", "macd_signal", "macd_hist"], from_dict["features"]

        wanted = ["close", "ema_fast", "rsi", "atr"]
        wide = aq.run_config({"ticks_path": ticks, "timeframe": "M1", "features": wanted})
        check_result(wide)
        assert wide["features"] == wanted, wide["features"]

        # A bare string would otherwise iterate into single characters.
        expect_raises(ValueError, aq.run_config, {"ticks_path": ticks, "features": "close,rsi"})
        expect_raises(ValueError, aq.run_config, {"ticks_path": ticks, "features": ["close", "bogus"]})

    m = from_dict["metrics"]
    print(f"aiquant_api OK: {from_dict['candles']} candles, {m['trades']} trades, pnl {m['pnl']:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
