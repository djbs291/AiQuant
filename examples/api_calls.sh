#!/usr/bin/env bash
# The HTTP surface, end to end, with curl.
#
# Start the service first, from the repo root:
#
#   ./build/aiquant run-mvp scenarios/ticks_mvp.csv --tf M1 --model-out /tmp/model.csv
#   cp /tmp/model.csv scenarios/model.csv
#   ./build/aiquant_http --port 8080 --root scenarios --static examples/dashboard &
#
# Then:  ./examples/api_calls.sh [port]
#
# Note the split: the scenario endpoints take INI, /predict and /signal take JSON.

set -euo pipefail
PORT="${1:-8080}"
BASE="http://localhost:${PORT}"

say() { printf '\n=== %s\n' "$1"; }

say "GET /health"
curl -sS "${BASE}/health"

say "POST /run-file — body is a path, resolved under --root"
curl -sS -X POST "${BASE}/run-file" --data "mvp.ini" | head -12

say "POST /run-config — body is the INI itself"
curl -sS -X POST "${BASE}/run-config" --data-binary @scenarios/mvp.ini | head -12

say "POST /predict — a trained model applied to one set of feature values"
curl -sS -X POST "${BASE}/predict" \
  -d '{"model": "model.csv", "features": {"close": 100.0, "ema_fast": 99.5, "rsi": 55.0,
       "macd": 0.2, "macd_signal": 0.1, "macd_hist": 0.1}}'

say "POST /signal — rules plus an explicit prediction"
curl -sS -X POST "${BASE}/signal" \
  -d '{"close": 100.0, "rsi": 20.0, "ema_fast": 11.0, "ema_slow": 10.0, "prediction": 0.5}'

say "POST /signal — rules only, no model involved"
curl -sS -X POST "${BASE}/signal" -d '{"close": 100.0, "rsi": 80.0}'

say "errors are explicit, not silent"
echo "-- unknown feature in the INI (400):"
curl -sS -o /dev/null -w '   %{http_code}\n' -X POST "${BASE}/run-config" \
  --data "ticks = ticks_mvp.csv
features = close,bogus"
echo "-- model outside --root (403):"
curl -sS -o /dev/null -w '   %{http_code}\n' -X POST "${BASE}/predict" \
  -d '{"model": "../../etc/passwd", "features": {"close": 1.0}}'
echo "-- unknown endpoint (404):"
curl -sS -o /dev/null -w '   %{http_code}\n' -X POST "${BASE}/nope"

printf '\ndone\n'
