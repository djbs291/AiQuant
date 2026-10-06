# AiQuant HTTP service image.
#
# Build:  docker build -t aiquant-http .
# Run:    docker run --rm -p 8080:8080 -e AIQUANT_API_KEYS="$MY_KEY" aiquant-http
#
# The service has no TLS of its own: terminate TLS at a reverse proxy (or the platform's load
# balancer) in front of it, and set AIQUANT_API_KEYS so only known callers get through. Inside
# the container it binds 0.0.0.0 (the default CMD) so the mapped port works; keep the published
# port behind that proxy rather than exposing it raw.

# ---- build stage -------------------------------------------------------------------------------
FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake ninja-build g++ libstdc++-12-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Python bindings off (the service does not need them, and it drops the pybind11 dependency); the
# bundled minicatch keeps the configure step from reaching out for Catch2, so the build needs no
# network. Only the server target is built.
RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DAIQUANT_BUILD_PYTHON=OFF \
        -DAIQUANT_USE_BUNDLED_CATCH=ON \
    && cmake --build build --target aiquant_http

# ---- runtime stage -----------------------------------------------------------------------------
FROM debian:bookworm-slim AS runtime

# libstdc++6 is the only runtime dependency of the statically-linked-against-project binary.
RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --uid 10001 aiquant \
    && mkdir -p /data \
    && chown aiquant:aiquant /data

COPY --from=build /src/build/aiquant_http /usr/local/bin/aiquant_http

USER aiquant
WORKDIR /data
EXPOSE 8080

# A single, self-contained CMD -- binary included, no separate ENTRYPOINT. Some hosts (Render
# among them) run the image's command directly and do not honour a separate ENTRYPOINT, so a
# bare-flags CMD would be exec'd as if "--bind" were the program and the container would exit
# immediately (status 128). Keeping the whole command here avoids that.
#
# Production-ready defaults: listen on every interface (so the published port reaches it), hold
# every file to the mounted /data, and rate-limit. The API key comes from the AIQUANT_API_KEYS
# env var -- pass it with `-e AIQUANT_API_KEYS=...`, never on the command line. To change the
# flags, override the whole command, e.g.
#   docker run -e AIQUANT_API_KEYS=$KEY -p 8080:8080 aiquant-http \
#       aiquant_http --bind 0.0.0.0 --port 8080 --root /data --model /data/model.csv
CMD ["aiquant_http", "--bind", "0.0.0.0", "--port", "8080", "--root", "/data", "--rate-limit", "120", "--rate-window", "60"]
