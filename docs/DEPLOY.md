# Deploying the AiQuant HTTP service

The service is a single Docker container (see the repo-root `Dockerfile`): one process, no
database. It has **no TLS of its own**, so it must run behind something that terminates HTTPS,
and it should run with **authentication on** (`AIQUANT_API_KEYS`) once it is reachable from the
internet. This guide covers Render (the recommended, lowest-effort host) in full, then sketches
the alternatives.

Whatever you pick, the invariants are the same:

- The container listens on `--port 8080`, `--bind 0.0.0.0` (the Dockerfile's default).
- `GET /health` returns `200` without a key — point the platform's health check at it.
- Set `AIQUANT_API_KEYS` to one or more comma-separated secrets; every request but `/health`
  then needs one in `X-API-Key` or `Authorization: Bearer`.
- Generate a key with `openssl rand -hex 24`. Keep it out of git — set it in the host's secrets.

---

## Render (recommended)

Render builds the `Dockerfile` straight from GitHub and serves it over HTTPS. Free to start.

### 1. Prerequisites

- The code is on GitHub (it is: `djbs291/AiQuant`).
- A key to use: run `openssl rand -hex 24` locally and copy the output somewhere safe.
- A free Render account — sign up at <https://render.com> with "Sign in with GitHub" (simplest,
  so Render can see the repo). No credit card is needed for the free plan.

### 2. Create the service from the Blueprint

The repo ships `render.yaml`, a Render *Blueprint* that describes the service, so you do not
configure anything by hand.

1. In the Render dashboard click **New +** → **Blueprint**.
2. Choose the `djbs291/AiQuant` repository (authorise Render to access it if asked).
3. Render reads `render.yaml` and shows one service, `aiquant-http`. It will prompt for the value
   of the `AIQUANT_API_KEYS` environment variable (it is marked secret, so it is not in git):
   paste the key you generated. For several keys, separate them with commas.
4. Click **Apply** / **Create**. Render builds the Docker image (a few minutes the first time)
   and deploys it.

When it finishes, the service has a URL like `https://aiquant-http.onrender.com`.

### 3. Verify it

```bash
BASE=https://aiquant-http.onrender.com
KEY=... # the key you set

# Health needs no key:
curl $BASE/health
# -> {"status": "ok"}

# A request without the key is refused:
curl -s -o /dev/null -w "%{http_code}\n" -X POST $BASE/signal -d '{"close":100,"rsi":20,"prediction":0.5}'
# -> 401

# With the key, the full pipeline on data you send inline:
curl -X POST $BASE/run-inline -H "X-API-Key: $KEY" \
  -d "{\"ticks_csv\": \"$(printf 'Timestamp,symbol,price,volume\n1,A,100,1\n...')\", \"config\": \"tf = M1\"}"
```

(For a real body, build the JSON from a CSV file — `scripts/validate_real_data.py` shows the
shape, or use `jq -Rs` to embed a CSV into the `ticks_csv` field.)

### 4. Know the free-plan behaviour

- The free service **sleeps after ~15 minutes idle**; the next request wakes it and takes a few
  seconds. Fine for validation and demos. Upgrade the `plan:` in `render.yaml` to `starter`
  (~$7/mo) to keep it always on.
- The container's filesystem is **ephemeral**: `/data` (the `--root`) starts empty on every
  deploy. That is fine for this API — `/run-inline` carries its own data, and `/predict` takes
  feature values inline. If you later want to serve a fixed model over `/predict`, bake it into
  the image or add a Render disk, and point `--model` at it.

### 5. Updating

Render redeploys automatically when you push to the branch it tracks (by default `main`). To
change the rate limit, region or plan, edit `render.yaml` and push.

---

## Alternatives

All of these also terminate TLS for you except the VPS, where you add it with Caddy.

### Fly.io

Container-native, CLI-driven, deploys close to your users, very cheap.

```bash
# one-time: install flyctl and log in (https://fly.io/docs/hands-on/install-flyctl/)
fly launch --no-deploy        # detects the Dockerfile; keep the internal port at 8080
fly secrets set AIQUANT_API_KEYS=$(openssl rand -hex 24)
fly deploy
```

In the generated `fly.toml`, set the HTTP service's `internal_port = 8080` and add a health
check for `GET /health`. Fly gives an HTTPS `*.fly.dev` URL.

### Google Cloud Run

Serverless containers that scale to zero (you pay only for requests), auto HTTPS. More account
setup, but the cheapest at low, bursty traffic.

```bash
gcloud run deploy aiquant-http --source . \
  --port 8080 --allow-unauthenticated \
  --set-env-vars AIQUANT_API_KEYS=$(openssl rand -hex 24)
```

`--allow-unauthenticated` lets the public reach the URL; your own `AIQUANT_API_KEYS` is what
actually gates it. Cloud Run sets `$PORT` (8080 by default), which matches the container.

### A VPS you manage (Hetzner, DigitalOcean, …)

Most control, flat ~$4–6/mo, most work. Rent a small Linux box, install Docker, and put
**Caddy** in front for automatic TLS (Caddy gets a Let's Encrypt certificate on its own).

```bash
# on the VPS, with a domain's A record pointing at it:
docker build -t aiquant-http https://github.com/djbs291/AiQuant.git
# The image's default command already binds 0.0.0.0, rate-limits, and roots at /data; the key
# comes from the env var. Only -p and -e are needed.
docker run -d --restart unless-stopped -p 127.0.0.1:8080:8080 \
  -e AIQUANT_API_KEYS=$(openssl rand -hex 24) --name aiquant aiquant-http
```

Then a `Caddyfile`:

```
api.yourdomain.com {
    reverse_proxy 127.0.0.1:8080
}
```

`caddy run` (or the systemd service) terminates TLS and proxies to the container. Open ports
80/443 in the firewall and keep 8080 bound to loopback so only Caddy reaches it.

---

## Security checklist before you share the URL

- [ ] `AIQUANT_API_KEYS` is set (the startup log says `authentication: N API key(s)`, not `off`).
- [ ] TLS is on — the platform's URL is `https://`, or Caddy is in front on a VPS.
- [ ] A rate limit is set (`--rate-limit`), so one caller cannot exhaust the box.
- [ ] You are **not** serving a `--static` directory or a `--root` with anything private in it.
- [ ] The landing page / docs say clearly this is a research/backtesting tool — not investment
      advice and not a performance guarantee.
