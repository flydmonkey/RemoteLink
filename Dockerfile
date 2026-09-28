# syntax=docker/dockerfile:1.7
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG REMOTELINK_VERSION=dev
ARG REMOTELINK_REVISION=unknown

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates cmake git ninja-build npm pkg-config python3 \
      freerdp3-dev libwinpr3-dev libopenh264-dev libopus-dev libyuv-dev \
      libssl-dev libssh2-1-dev nlohmann-json3-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY . .
RUN bash ./scripts/copy-novnc-core.sh \
    && cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DREMOTELINK_ENABLE_STREAMING=ON \
      -DREMOTELINK_BUILD_TESTS=ON \
    && cmake --build build --parallel "$(nproc)" \
    && ctest --test-dir build --output-on-failure \
    && find web -type f -name '*.html' -exec \
      sed -i "s/__REMOTELINK_VERSION__/${REMOTELINK_VERSION}/g" {} + \
    && install -D -m 0755 build/remotelink /stage/bin/remotelink \
    && install -D -m 0755 build/libremotelink_streaming.so /stage/lib/libremotelink_streaming.so \
    && find build -name 'libdatachannel.so*' -exec cp -a {} /stage/lib/ \; \
    && find build -name 'libvncclient.so*' -exec cp -a {} /stage/lib/ \; \
    && find build -name 'libvncserver.so*' -exec cp -a {} /stage/lib/ \; \
    && test -e /stage/lib/libdatachannel.so \
    && test -e /stage/lib/libvncclient.so

FROM builder AS guacd-builder

ARG GUACAMOLE_VERSION=1.3.0

RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf automake libtool libcairo2-dev libjpeg-turbo8-dev libpng-dev \
      freerdp2-dev libossp-uuid-dev libpulse-dev libwebp-dev uuid-dev \
    && rm -rf /var/lib/apt/lists/* \
    && git clone --branch "${GUACAMOLE_VERSION}" --depth 1 \
      https://github.com/apache/guacamole-server.git /guacamole-server \
    && cd /guacamole-server \
    && autoreconf -fi \
    && ./configure --prefix=/opt/guacamole \
      --with-freerdp-plugin-dir=/usr/lib/x86_64-linux-gnu/freerdp2 \
      --disable-guacenc --disable-guaclog \
      CPPFLAGS=-Wno-error=deprecated-declarations \
    && make -j"$(nproc)" \
    && make DESTDIR=/stage-guacd install

FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
ARG REMOTELINK_VERSION=dev
ARG REMOTELINK_REVISION=unknown
LABEL org.opencontainers.image.title="RemoteLink" \
      org.opencontainers.image.description="Self-hosted RDP, VNC and SSH remote access gateway" \
      org.opencontainers.image.version="${REMOTELINK_VERSION}" \
      org.opencontainers.image.revision="${REMOTELINK_REVISION}" \
      org.opencontainers.image.licenses="Apache-2.0" \
      org.opencontainers.image.source="https://github.com/flydmonkey/RemoteLink"

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl ghostscript openssl tini \
      freerdp3-dev libwinpr3-dev libopenh264-dev libopus-dev libyuv-dev \
      libfreerdp-client2-2t64 libfreerdp2-2t64 libwinpr2-2t64 \
      libcairo2 libjpeg-turbo8 libpng16-16t64 libpulse0 libssl-dev \
      libssh2-1-dev libwebp7 libossp-uuid16 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --user-group --home-dir /nonexistent \
      --shell /usr/sbin/nologin remotelink \
    && install -d -o remotelink -g remotelink -m 0750 /var/lib/remotelink \
    && install -d -o remotelink -g remotelink -m 0755 /var/lib/remotelink/users \
    && ln -s /var/lib/remotelink/users /users \
    && install -d -o root -g remotelink -m 0750 /etc/remotelink \
    && install -d -o remotelink -g remotelink -m 0750 /etc/remotelink/tls \
    && printf '{"targets":[]}\n' > /etc/remotelink/targets.json

COPY --from=builder /stage/ /opt/remotelink/
COPY --from=guacd-builder /stage-guacd/ /
COPY --from=builder /src/web /opt/remotelink/web
COPY docker/entrypoint.sh /usr/local/bin/remotelink-entrypoint
COPY docker/generate-default-certificate.sh /usr/local/bin/remotelink-generate-certificate

ENV LD_LIBRARY_PATH=/opt/remotelink/lib:/opt/guacamole/lib \
    HOME=/tmp \
    XDG_CONFIG_HOME=/tmp/.config \
    REMOTELINK_WEB_ROOT=/opt/remotelink/web \
    REMOTELINK_STATE_DIR=/var/lib/remotelink \
    REMOTELINK_TARGETS_FILE=/etc/remotelink/targets.json \
    REMOTELINK_ALLOWED_HOSTS=* \
    REMOTELINK_TLS_CERTIFICATE=/etc/remotelink/tls/fullchain.pem \
    REMOTELINK_TLS_PRIVATE_KEY=/etc/remotelink/tls/privkey.pem \
    REMOTELINK_GUACD_HOST=127.0.0.1 \
    REMOTELINK_GUACD_PORT=4822 \
    REMOTELINK_GUACD_LOG_LEVEL=info \
    REMOTELINK_ICE_UDP_PORT_MIN=50000 \
    REMOTELINK_ICE_UDP_PORT_MAX=50019

VOLUME ["/var/lib/remotelink"]
EXPOSE 18080/tcp
EXPOSE 50000-50019/udp
RUN chmod 0755 /usr/local/bin/remotelink-entrypoint /usr/local/bin/remotelink-generate-certificate \
    && ln -s /opt/guacamole/sbin/guacd /usr/sbin/guacd \
    && remotelink-generate-certificate \
    && chown -R remotelink:remotelink /etc/remotelink/tls
USER remotelink
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/remotelink-entrypoint"]
CMD ["/opt/remotelink/bin/remotelink"]
HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
  CMD test -s /tmp/remotelink-guacd.pid && kill -0 "$(cat /tmp/remotelink-guacd.pid)" && (curl --fail --silent --insecure https://127.0.0.1:18080/healthz || curl --fail --silent http://127.0.0.1:18080/healthz) || exit 1
