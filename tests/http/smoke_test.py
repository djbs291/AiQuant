"""Smoke test for the aiquant_http service.

Starts the built server on a free port with a temp scenario root and checks the
status codes and payloads of every endpoint, including the error paths.

    python3 tests/http/smoke_test.py            # uses ./build/aiquant_http
    AIQUANT_BUILD_DIR=/tmp/b python3 tests/http/smoke_test.py
"""

import concurrent.futures
import http.client
import json
import math
import os
import socket
import subprocess
import sys
import tempfile
import time

BUILD_DIR = os.environ.get("AIQUANT_BUILD_DIR", "build")
SERVER = os.path.join(BUILD_DIR, "aiquant_http")
MAX_BODY = 65536
TICKS = 300


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def write_ticks(path, rows=TICKS):
    with open(path, "w") as f:
        f.write("Timestamp,symbol,price,volume\n")
        for i in range(rows):
            price = 100.0 + 0.05 * i + 2.0 * math.sin(i / 5.0)
            f.write(f"{1693492800000 + i * 60_000},ABC,{price:.4f},1\n")


def wait_until_ready(port, proc, timeout=15.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"server exited early with code {proc.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not start listening in time")


def request(port, method, path, body=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
    try:
        conn.request(method, path, body=body)
        response = conn.getresponse()
        return response.status, response.read().decode()
    finally:
        conn.close()


def get_with_headers(port, path):
    """GET that also returns the response headers, for checking Content-Type."""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
    try:
        conn.request("GET", path)
        response = conn.getresponse()
        return response.status, dict(response.getheaders()), response.read().decode()
    finally:
        conn.close()


def check(label, got, expected):
    assert got == expected, f"{label}: expected {expected}, got {got}"
    print(f"  ok  {label} -> {got}")


def start_server(args):
    """Starts the server on a free port and waits for it to listen."""
    port = free_port()
    proc = subprocess.Popen([SERVER, "--port", str(port)] + args,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        wait_until_ready(port, proc)
    except Exception:
        proc.terminate()
        raise
    return port, proc


def stop_server(proc):
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()


def check_static(tmp):
    """Static serving is off unless --static is given, and confined to it when it is."""
    static_root = os.path.join(tmp, "static")
    os.mkdir(static_root)
    with open(os.path.join(static_root, "index.html"), "w") as f:
        f.write('<!DOCTYPE html>\n<title>AiQuant</title>\n<p id="marker">dashboard</p>\n')
    with open(os.path.join(static_root, "app.js"), "w") as f:
        f.write("// app\n")
    with open(os.path.join(static_root, "notes.md"), "w") as f:
        f.write("markdown is not on the whitelist\n")
    with open(os.path.join(static_root, "icon.svg"), "w") as f:
        f.write("<svg xmlns='http://www.w3.org/2000/svg'></svg>\n")  # svg can carry script
    with open(os.path.join(static_root, ".hidden.txt"), "w") as f:
        f.write("dotfiles are refused even with a whitelisted extension\n")

    secret = os.path.join(tmp, "secret.txt")
    with open(secret, "w") as f:
        f.write("should never be served\n")
    # A symlink pointing out of the directory must be refused, not followed.
    os.symlink(secret, os.path.join(static_root, "link.txt"))

    # Off by default: a GET is just a wrong method.
    port, proc = start_server(["--root", tmp])
    try:
        check("GET / without --static", request(port, "GET", "/")[0], 405)
        check("GET /health without --static", request(port, "GET", "/health")[0], 200)
    finally:
        stop_server(proc)

    port, proc = start_server(["--root", tmp, "--static", static_root])
    try:
        status, headers, body = get_with_headers(port, "/")
        check("GET / with --static", status, 200)
        assert "text/html" in headers.get("Content-Type", ""), headers
        assert "marker" in body, body

        status, headers, _ = get_with_headers(port, "/app.js")
        check("GET /app.js", status, 200)
        assert "javascript" in headers.get("Content-Type", ""), headers

        check("GET /health with --static", request(port, "GET", "/health")[0], 200)
        check("GET /missing.html", request(port, "GET", "/missing.html")[0], 404)
        check("GET /notes.md (not whitelisted)", request(port, "GET", "/notes.md")[0], 404)
        check("GET /link.txt (symlink out of the dir)", request(port, "GET", "/link.txt")[0], 403)
        check("GET /../secret.txt", request(port, "GET", "/../secret.txt")[0], 403)
        check("GET percent-encoded path", request(port, "GET", "/%2e%2e/secret.txt")[0], 400)
        check("GET directory", request(port, "GET", "/sub/")[0], 404)
        check("POST / with --static", request(port, "POST", "/", "x")[0], 404)
        check("GET /icon.svg (svg is off the whitelist)", request(port, "GET", "/icon.svg")[0], 404)
        check("GET /.hidden.txt (dotfile)", request(port, "GET", "/.hidden.txt")[0], 404)

        # An API route asked for with the wrong method stays 405: static serving must not
        # swallow it and answer "no such file".
        check("GET /predict with --static", request(port, "GET", "/predict")[0], 405)
        check("GET /run-config with --static", request(port, "GET", "/run-config")[0], 405)

        # Security headers travel with every static response.
        _, headers, _ = get_with_headers(port, "/index.html")
        assert headers.get("X-Content-Type-Options") == "nosniff", headers
        assert "default-src 'self'" in headers.get("Content-Security-Policy", ""), headers
        print("  ok  static responses carry nosniff and a CSP")

        assert proc.poll() is None, "server died serving static files"
    finally:
        stop_server(proc)

    # The page the README points at must actually be there.
    dashboard = os.path.join("examples", "dashboard")
    if os.path.isdir(dashboard):
        for name in ("index.html", "app.css", "app.js"):
            assert os.path.exists(os.path.join(dashboard, name)), f"missing {name}"
        print("  ok  examples/dashboard is present")


def check_scenario_paths(tmp):
    """The files a scenario names are held to --root: its ticks, and any model_out.

    POST /run-config once wrote model_out wherever it pointed, so these check both that the
    request is refused and that nothing was written.
    """
    root = os.path.join(tmp, "paths_root")
    os.mkdir(root)
    ticks = os.path.join(root, "ticks.csv")
    write_ticks(ticks)

    outside_ticks = os.path.join(tmp, "outside_ticks.csv")
    write_ticks(outside_ticks)
    outside_model = os.path.join(tmp, "escaped_model.csv")

    port, proc = start_server(["--root", root])
    try:
        def run_config(ini):
            return request(port, "POST", "/run-config", ini)

        status, body = run_config(f"ticks = {outside_ticks}\n")
        check("POST /run-config (ticks outside root)", status, 403)
        assert "Ticks path" in body, body

        status, _ = run_config(f"ticks = {os.path.join(root, 'nope.csv')}\n")
        check("POST /run-config (ticks missing inside root)", status, 404)

        # The scenario file itself is inside the root; what it points at is not. The file's
        # contents are checked, not just its location.
        inner = os.path.join(root, "points_out.ini")
        with open(inner, "w") as f:
            f.write(f"ticks = {outside_ticks}\n")
        status, _ = request(port, "POST", "/run-file", "points_out.ini")
        check("POST /run-file (scenario inside root, ticks outside)", status, 403)

        status, body = run_config(f"ticks = {ticks}\nmodel_out = {outside_model}\n")
        check("POST /run-config (model_out outside root)", status, 403)
        assert not os.path.exists(outside_model), "model_out was written outside the root"

        # Relative paths resolve against the server's working directory, so climbing out of it
        # has to be refused as well, and must not create anything where it points.
        cwd_escape = os.path.relpath(outside_model, os.getcwd())
        status, _ = run_config(f"ticks = {ticks}\nmodel_out = {cwd_escape}\n")
        check("POST /run-config (relative model_out outside root)", status, 403)
        assert not os.path.exists(outside_model), "model_out was written outside the root"

        if hasattr(os, "symlink"):
            # A link inside the root to a file outside it, existing and not yet created.
            victim = os.path.join(tmp, "victim.csv")
            with open(victim, "w") as f:
                f.write("do not overwrite\n")
            existing_link = os.path.join(root, "existing_link.csv")
            os.symlink(victim, existing_link)
            status, _ = run_config(f"ticks = {ticks}\nmodel_out = {existing_link}\n")
            check("POST /run-config (model_out via symlink to a file outside)", status, 403)
            with open(victim) as f:
                assert f.read() == "do not overwrite\n", "the symlink target was overwritten"

            dangling_target = os.path.join(tmp, "dangling_target.csv")
            dangling_link = os.path.join(root, "dangling_link.csv")
            os.symlink(dangling_target, dangling_link)
            status, _ = run_config(f"ticks = {ticks}\nmodel_out = {dangling_link}\n")
            check("POST /run-config (model_out via dangling symlink)", status, 403)
            assert not os.path.exists(dangling_target), "a dangling symlink was followed out of the root"

            ticks_link = os.path.join(root, "ticks_link.csv")
            os.symlink(outside_ticks, ticks_link)
            status, _ = run_config(f"ticks = {ticks_link}\n")
            check("POST /run-config (ticks via symlink to a file outside)", status, 403)

        # Inside the root, both still work: train over HTTP, then serve the model by name.
        inside_model = os.path.join(root, "trained.csv")
        status, _ = run_config(f"ticks = {ticks}\nmodel_out = {inside_model}\n")
        check("POST /run-config (model_out inside root)", status, 200)
        with open(inside_model) as f:
            assert f.readline().startswith("# AiQuant"), "model_out inside the root was not written"
        status, _ = request(port, "POST", "/predict",
                            json.dumps({"model": "trained.csv",
                                        "features": {"close": 100.0, "ema_fast": 100.0, "rsi": 50.0,
                                                     "macd": 0.0, "macd_signal": 0.0, "macd_hist": 0.0}}))
        check("POST /predict (model trained over HTTP)", status, 200)

        missing_dir = os.path.join(root, "no_such_dir", "model.csv")
        status, _ = run_config(f"ticks = {ticks}\nmodel_out = {missing_dir}\n")
        check("POST /run-config (model_out in a missing directory)", status, 404)

        assert proc.poll() is None, "server died during the path checks"
    finally:
        stop_server(proc)


def check_bind(tmp):
    """Loopback by default; --bind takes an IPv4 address and refuses anything else."""
    port, proc = start_server(["--root", tmp, "--bind", "127.0.0.1"])
    try:
        check("GET /health with --bind 127.0.0.1", request(port, "GET", "/health")[0], 200)
    finally:
        stop_server(proc)

    result = subprocess.run([SERVER, "--port", str(free_port()), "--bind", "not-an-ip"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=10)
    check("--bind not-an-ip exit code", result.returncode, 2)


def main():
    if not os.path.exists(SERVER):
        print(f"{SERVER} not found; build the project first", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "scenarios")
        os.mkdir(root)

        ticks = os.path.join(root, "ticks.csv")
        write_ticks(ticks)
        # Paths inside a scenario resolve against the server's working directory, as on the
        # CLI, and must land inside --root. Keep them absolute so the test does not depend on
        # where it is run from; check_scenario_paths covers the containment itself.
        scenario = os.path.join(root, "mvp.ini")
        with open(scenario, "w") as f:
            f.write(f"ticks = {ticks}\ntf = M1\n")

        short_ticks = os.path.join(root, "short.csv")
        write_ticks(short_ticks, rows=5)
        short_scenario = os.path.join(root, "short.ini")
        with open(short_scenario, "w") as f:
            f.write(f"ticks = {short_ticks}\ntf = M1\n")

        outside = os.path.join(tmp, "outside.ini")
        with open(outside, "w") as f:
            f.write(f"ticks = {ticks}\n")

        # bias 0.5, close 0.1, rsi -0.02  ->  0.5 + 0.1*100 - 0.02*50 = 9.5
        model = os.path.join(root, "model.csv")
        with open(model, "w") as f:
            f.write("# AiQuant LinearModel weights\n# features: close,rsi\n"
                    "bias,0.5\nclose,0.1\nrsi,-0.02\n")
        outside_model = os.path.join(tmp, "outside_model.csv")
        with open(outside_model, "w") as f:
            f.write("bias,0.0\nclose,1.0\n")

        port = free_port()
        proc = subprocess.Popen(
            [SERVER, "--port", str(port), "--root", root, "--max-body", str(MAX_BODY)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        try:
            wait_until_ready(port, proc)

            status, body = request(port, "GET", "/health")
            check("GET /health", status, 200)
            assert json.loads(body)["status"] == "ok", body

            status, body = request(port, "POST", "/run-file", "mvp.ini")
            check("POST /run-file (relative to root)", status, 200)
            payload = json.loads(body)
            assert payload["candles"] == TICKS, payload["candles"]
            assert "metrics" in payload and "pnl" in payload["metrics"], payload

            status, body = request(port, "POST", "/run-file", scenario)
            check("POST /run-file (absolute, inside root)", status, 200)
            assert json.loads(body)["candles"] == TICKS

            with open(scenario) as f:
                ini = f.read()
            status, body = request(port, "POST", "/run-config", ini)
            check("POST /run-config", status, 200)
            assert json.loads(body)["metrics"] == payload["metrics"]

            status, body = request(port, "POST", "/run-file", "../outside.ini")
            check("POST /run-file (escapes root)", status, 403)
            status, body = request(port, "POST", "/run-file", outside)
            check("POST /run-file (absolute, outside root)", status, 403)

            status, body = request(port, "POST", "/run-file", "nope.ini")
            check("POST /run-file (missing file)", status, 404)

            status, body = request(port, "POST", "/run-file", "")
            check("POST /run-file (empty body)", status, 400)

            status, body = request(port, "POST", "/run-config", "this line has no equals sign\n")
            check("POST /run-config (malformed INI)", status, 400)

            status, body = request(port, "POST", "/run-file", "short.ini")
            check("POST /run-file (too few candles)", status, 422)

            # An unparsable feature name is a client error, like any other bad INI.
            status, body = request(port, "POST", "/run-config", f"ticks = {ticks}\nfeatures = close,bogus\n")
            check("POST /run-config (unknown feature)", status, 400)
            assert "bogus" in body, body

            status, body = request(port, "GET", "/run-config")
            check("GET /run-config", status, 405)

            status, body = request(port, "POST", "/nope")
            check("POST /nope", status, 404)

            status, body = request(port, "POST", "/run-config", "x" * (MAX_BODY + 1))
            check("POST /run-config (body over the limit)", status, 413)

            # /predict: the model is named per request and resolved under --root.
            status, body = request(port, "POST", "/predict",
                                   json.dumps({"model": "model.csv",
                                               "features": {"close": 100.0, "rsi": 50.0}}))
            check("POST /predict", status, 200)
            payload = json.loads(body)
            assert abs(payload["prediction"] - 9.5) < 1e-9, payload
            assert payload["features"] == ["close", "rsi"], payload

            status, body = request(port, "POST", "/predict",
                                   json.dumps({"model": "model.csv", "features": {"close": 100.0}}))
            check("POST /predict (missing feature)", status, 400)
            assert "rsi" in body, body

            status, body = request(port, "POST", "/predict",
                                   json.dumps({"model": "../outside_model.csv",
                                               "features": {"close": 100.0}}))
            check("POST /predict (model escapes root)", status, 403)

            status, body = request(port, "POST", "/predict",
                                   json.dumps({"model": "nope.csv", "features": {"close": 1.0}}))
            check("POST /predict (missing model)", status, 404)

            status, body = request(port, "POST", "/predict", "{not json")
            check("POST /predict (invalid JSON)", status, 400)

            status, body = request(port, "POST", "/predict",
                                   json.dumps({"features": {"close": 1.0}}))
            check("POST /predict (no model configured)", status, 400)

            status, body = request(port, "GET", "/predict")
            check("GET /predict", status, 405)

            # /signal: rules only, with a caller-supplied prediction.
            status, body = request(port, "POST", "/signal",
                                   json.dumps({"close": 100.0, "rsi": 20.0, "ema_fast": 11.0,
                                               "ema_slow": 10.0, "prediction": 0.5}))
            check("POST /signal (explicit prediction)", status, 200)
            payload = json.loads(body)
            assert payload["signal"] == "Buy", payload
            assert abs(payload["score"] - 2.5) < 1e-9, payload

            # /signal: the model supplies the prediction.
            status, body = request(port, "POST", "/signal",
                                   json.dumps({"model": "model.csv", "close": 100.0, "rsi": 50.0,
                                               "features": {"close": 100.0, "rsi": 50.0}}))
            check("POST /signal (model prediction)", status, 200)
            payload = json.loads(body)
            assert abs(payload["prediction"] - 9.5) < 1e-9, payload
            assert payload["features"] == ["close", "rsi"], payload

            # /signal with no model and no prediction still evaluates the rules.
            status, body = request(port, "POST", "/signal", json.dumps({"close": 100.0, "rsi": 80.0}))
            check("POST /signal (rules only)", status, 200)
            payload = json.loads(body)
            assert payload["signal"] == "Sell", payload
            assert payload["prediction"] is None, payload

            # Each connection is served on its own thread; run a batch at once.
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                batch = [pool.submit(request, port, "POST", "/run-file", "mvp.ini") for _ in range(8)]
                statuses = sorted({f.result()[0] for f in batch})
            check("8 concurrent POST /run-file", statuses, [200])

            assert proc.poll() is None, "server died during the run"
        finally:
            stop_server(proc)

        check_static(tmp)
        check_scenario_paths(tmp)
        check_bind(tmp)

    print("aiquant_http OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
