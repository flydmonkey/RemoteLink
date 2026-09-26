# syntax=docker/dockerfile:1.7
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG REMOTELINK_VERSION=dev

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates cmake git ninja-build npm pkg-config python3 \
      freerdp3-dev libwinpr3-dev libopenh264-dev libopus-dev libyuv-dev \
      libssl-dev libssh2-1-dev nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY . .
RUN bash ./scripts/copy-novnc-core.sh \
    && cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DREMOTELINK_ENABLE_STREAMING=ON \
      -DREMOTELINK_BUILD_TESTS=OFF \
    && cmake --build build --parallel "$(nproc)" \
    && find web -type f -name '*.html' -exec \
      sed -i "s/__REMOTELINK_VERSION__/${REMOTELINK_VERSION}/g" {} + \
    && install -D -m 0755 build/remotelink /stage/bin/remotelink \
    && install -D -m 0755 build/libremotelink_streaming.so /stage/lib/libremotelink_streaming.so \
    && find build -name 'libdatachannel.so*' -exec cp -a {} /stage/lib/ \; \
    && find build -name 'libvncclient.so*' -exec cp -a {} /stage/lib/ \; \
    && find build -name 'libvncserver.so*' -exec cp -a {} /stage/lib/ \; \
    && test -e /stage/lib/libdatachannel.so \
    && test -e /stage/lib/libvncclient.so

FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
ARG REMOTELINK_VERSION=dev
LABEL org.opencontainers.image.title="RemoteLink" \
      org.opencontainers.image.description="Self-hosted RDP, VNC and SSH remote access gateway" \
      org.opencontainers.image.version="${REMOTELINK_VERSION}" \
      org.opencontainers.image.source="https://github.com/flydmonkey/RemoteLink"

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl tini \
      freerdp3-dev libwinpr3-dev libopenh264-dev libopus-dev libyuv-dev \
      libssl-dev libssh2-1-dev \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --user-group --home-dir /nonexistent \
      --shell /usr/sbin/nologin remotelink \
    && install -d -o remotelink -g remotelink -m 0750 /var/lib/remotelink \
    && install -d -o root -g remotelink -m 0750 /etc/remotelink \
    && printf '{"targets":[]}\n' > /etc/remotelink/targets.json

COPY --from=builder /stage/ /opt/remotelink/
COPY --from=builder /src/web /opt/remotelink/web
COPY docker/entrypoint.sh /usr/local/bin/remotelink-entrypoint

ENV LD_LIBRARY_PATH=/opt/remotelink/lib \
    REMOTELINK_WEB_ROOT=/opt/remotelink/web \
    REMOTELINK_STATE_DIR=/var/lib/remotelink \
    REMOTELINK_TARGETS_FILE=/etc/remotelink/targets.json \
    REMOTELINK_ALLOWED_HOSTS=* \
    REMOTELINK_BEHIND_TLS_PROXY=1

VOLUME ["/var/lib/remotelink"]
EXPOSE 18080
USER remotelink
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/remotelink-entrypoint"]
CMD ["/opt/remotelink/bin/remotelink"]
HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
  CMD curl --fail --silent http://127.0.0.1:18080/healthz || exit 1
