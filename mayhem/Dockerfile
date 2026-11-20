# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form, PREFERRED) — builds ON TOP of the org C/C++ base
# image (ghcr.io/savantenvs/base). The base already provides debian + clang/llvm +
# libFuzzer/afl++ + the identities + the build-contract ENV, so this only adds the
# per-commit build (built on top of the prebuilt base, rather than building the toolchain in-image).
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# BTstack (the Bluetooth stack the firmware links on the Pico): the harnesses compile the firmware
# against its real headers (event getters, L2CAP/HCI constants) and link its btstack_util.c/hci_cmd.c.
# Pinned release tarball, sha256-checked, cached for the offline re-run (SPEC §6.2 item 19).
USER root
ARG BTSTACK_VER=1.6.2
ARG BTSTACK_SHA256=a6968beff1e18b82baa77662dbb2dce6cc295dafbd23fcd31b99b9d541bcc5ce
RUN d=/opt/toolchains/deps/btstack-${BTSTACK_VER} && mkdir -p "$d/src" \
 && curl -fsSL -o /tmp/btstack.tgz https://github.com/bluekitchen/btstack/archive/refs/tags/v${BTSTACK_VER}.tar.gz \
 && echo "${BTSTACK_SHA256}  /tmp/btstack.tgz" | sha256sum -c - \
 && tar -xzf /tmp/btstack.tgz --strip-components=1 -C "$d/src" \
 && rm -f /tmp/btstack.tgz && chmod -R a+rX,go-w "$d"
ENV BTSTACK_DIR=/opt/toolchains/deps/btstack-1.6.2/src

# WDL (resampler) and libopus at the commits upstream's lib/WDL and lib/opus submodules pin.
# src/wdl_resample.cpp includes "../lib/WDL/WDL/resample.cpp"; the lib/WDL link lets that include
# resolve from this cache when the checkout's submodules are not populated.
ARG WDL_COMMIT=afc0f78dca4a1743a948839ac75f4d7059d739c3
ARG WDL_SHA256=26fc1fea8e587b41a83da4a9d68da63b14723bbc756371f133f47bc37506210c
RUN d=/opt/toolchains/deps/wdl-afc0f78 && mkdir -p "$d/src" "$d/lib" \
 && curl -fsSL -o /tmp/wdl.tgz https://github.com/justinfrankel/WDL/archive/${WDL_COMMIT}.tar.gz \
 && echo "${WDL_SHA256}  /tmp/wdl.tgz" | sha256sum -c - \
 && tar -xzf /tmp/wdl.tgz --strip-components=1 -C "$d/src" && ln -s ../src "$d/lib/WDL" \
 && rm -f /tmp/wdl.tgz && chmod -R a+rX,go-w "$d"
ARG OPUS_COMMIT=2d862ea14b233e5a3f3afaf74d96050691af3cd5
ARG OPUS_SHA256=cf304975640de3a15d59d3e19e9e829ae675aaadb3216d17ad9f2b0d99750b23
RUN d=/opt/toolchains/deps/opus-2d862ea && mkdir -p "$d/src" /tmp/opus-build \
 && curl -fsSL -o /tmp/opus.tgz https://github.com/xiph/opus/archive/${OPUS_COMMIT}.tar.gz \
 && echo "${OPUS_SHA256}  /tmp/opus.tgz" | sha256sum -c - \
 && tar -xzf /tmp/opus.tgz --strip-components=1 -C "$d/src" \
 && cmake -S "$d/src" -B /tmp/opus-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$d" \
      -DBUILD_SHARED_LIBS=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_DISABLE_INTRINSICS=ON \
      -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_C_FLAGS="-O2 -g -gdwarf-3" \
 && cmake --build /tmp/opus-build -j"$(nproc)" && cmake --install /tmp/opus-build \
 && rm -rf /tmp/opus-build /tmp/opus.tgz && chmod -R a+rX,go-w "$d"
ENV WDL_DIR=/opt/toolchains/deps/wdl-afc0f78 OPUS_DIR=/opt/toolchains/deps/opus-2d862ea

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Compile-cache pre-fill (SPEC §6.2 item 18) when the base provides it.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
