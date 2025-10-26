## Scenario Configs

The MVP CLI can execute full scenarios via `aiquant run-config`. The supported keys and grammar are documented in `docs/ScenarioConfig.md`, and a ready-to-run example lives at `scenarios/mvp.ini` (pointing to `ticks_sample.csv`). Use these as a template when wiring new experiments.

## C++ / Python API

The `fin::api::ScenarioService` offers a stable programmatic entry point for running scenarios. Link against the `fin_api` static library and call `ScenarioService::run` or `ScenarioService::run_file`. Optional Python bindings are built when `pybind11` is available:

```
cmake -DAIQUANT_BUILD_PYTHON=ON -S . -B build
cmake --build build
python -c "import aiquant_api as aq; svc=aq.ScenarioService(); cfg=aq.ScenarioConfig(); cfg.ticks_path='ticks_sample.csv'; print(svc.run_config(cfg)['metrics'])"
```

## HTTP Microservice

`aiquant_http` exposes the scenario runner over HTTP. Example usage:

```
./aiquant_http --port 8080 &
curl -X POST http://localhost:8080/run-file --data-binary @scenarios/mvp.ini
```

`POST /run-file` expects the HTTP body to contain a path to an existing scenario file on disk. `POST /run-config` accepts raw INI contents and executes them via a temporary file. Both endpoints return the JSON emitted by the CLI `--json` flag.
