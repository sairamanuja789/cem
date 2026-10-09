# syntax=docker/dockerfile:1
# check=skip=InvalidDefaultArgInFrom
# Fan CEM development container (T01; REPRO-002, PORT-001, LIFE-001; ADR-006).
# Every pin comes from docker/pins.env via scripts/docker-build.sh (--build-arg); there are no defaults here.
# Stages: base -> toolchain -> ci            (compilers, vcpkg, gitleaks; what T02 checks need; ADR-008)
#         base -> toolchain -> dev           (ci plus OpenFOAM, CalculiX, Gmsh)
# In dev, OpenFOAM is its own layer; a toolchain change rebuilds the layers after it.

ARG UBUNTU_BASE

# ---------------------------------------------------------------------------------------------
FROM ${UBUNTU_BASE} AS base
ARG UBUNTU_SNAPSHOT
ARG APT_CA_CERTIFICATES
ARG APT_CURL
ARG APT_GNUPG
ARG UV_VERSION
ARG UV_SHA256
ARG PYTHON_VERSION
ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    UV_PYTHON_INSTALL_DIR=/opt/python \
    UV_PYTHON_PREFERENCE=only-managed \
    UV_PYTHON_DOWNLOADS=manual \
    UV_LINK_MODE=copy

# The snapshot service is HTTPS-only and the base image has no CA bundle, so ca-certificates is first
# fetched from the plain archive (signed metadata), then reinstalled at its pinned snapshot version.
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && printf 'Types: deb\nURIs: https://snapshot.ubuntu.com/ubuntu/%s/\nSuites: noble noble-updates noble-security\nComponents: main universe\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n' \
      "${UBUNTU_SNAPSHOT}" > /etc/apt/sources.list.d/ubuntu.sources \
 && rm -rf /var/lib/apt/lists/* \
 && apt-get update \
 && apt-get install -y --no-install-recommends --allow-downgrades \
      "ca-certificates=${APT_CA_CERTIFICATES}" "curl=${APT_CURL}" "gnupg=${APT_GNUPG}" \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL "https://github.com/astral-sh/uv/releases/download/${UV_VERSION}/uv-x86_64-unknown-linux-gnu.tar.gz" -o /tmp/uv.tgz \
 && echo "${UV_SHA256}  /tmp/uv.tgz" | sha256sum -c - \
 && tar -xzf /tmp/uv.tgz -C /usr/local/bin --strip-components=1 uv-x86_64-unknown-linux-gnu/uv uv-x86_64-unknown-linux-gnu/uvx \
 && rm /tmp/uv.tgz \
 && uv python install "${PYTHON_VERSION}" \
 && chmod -R a+rX /opt/python

# ---------------------------------------------------------------------------------------------
FROM base AS toolchain
ARG APT_GIT
ARG APT_XZ_UTILS
ARG APT_ZIP
ARG APT_UNZIP
ARG APT_TAR
ARG APT_PKG_CONFIG
ARG APT_MAKE
ARG APT_GCC_13
ARG APT_GPP_13
ARG APT_CLANG_20
ARG APT_CLANG_FORMAT_20
ARG APT_CLANG_TIDY_20
ARG APT_LIBCLANG_RT_20_DEV
ARG APT_LLVM_20
ARG APT_CMAKE
ARG APT_NINJA_BUILD
ARG APT_CCACHE
ARG APT_JQ
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      "git=${APT_GIT}" "xz-utils=${APT_XZ_UTILS}" "zip=${APT_ZIP}" "unzip=${APT_UNZIP}" "tar=${APT_TAR}" \
      "pkg-config=${APT_PKG_CONFIG}" "make=${APT_MAKE}" \
      "gcc-13=${APT_GCC_13}" "g++-13=${APT_GPP_13}" \
      "clang-20=${APT_CLANG_20}" "clang-format-20=${APT_CLANG_FORMAT_20}" "clang-tidy-20=${APT_CLANG_TIDY_20}" \
      "libclang-rt-20-dev=${APT_LIBCLANG_RT_20_DEV}" "llvm-20=${APT_LLVM_20}" \
      "cmake=${APT_CMAKE}" "ninja-build=${APT_NINJA_BUILD}" "ccache=${APT_CCACHE}" "jq=${APT_JQ}" \
 && rm -rf /var/lib/apt/lists/*

ARG VCPKG_TAG
ARG VCPKG_COMMIT
ENV VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1
RUN git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
 && git -C "${VCPKG_ROOT}" checkout "${VCPKG_COMMIT}" \
 && test "$(git -C "${VCPKG_ROOT}" rev-parse "${VCPKG_TAG}^{commit}")" = "${VCPKG_COMMIT}" \
 && git config --system --add safe.directory "${VCPKG_ROOT}" \
 && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics \
 && chmod -R a+rX "${VCPKG_ROOT}"

RUN for t in clang clang++ clang-format clang-tidy run-clang-tidy; do ln -s "/usr/bin/$t-20" "/usr/local/bin/$t"; done

ARG GITLEAKS_VERSION
ARG GITLEAKS_SHA256
RUN curl -fsSL "https://github.com/gitleaks/gitleaks/releases/download/v${GITLEAKS_VERSION}/gitleaks_${GITLEAKS_VERSION}_linux_x64.tar.gz" -o /tmp/gitleaks.tgz \
 && echo "${GITLEAKS_SHA256}  /tmp/gitleaks.tgz" | sha256sum -c - \
 && tar -xzf /tmp/gitleaks.tgz -C /usr/local/bin gitleaks \
 && rm /tmp/gitleaks.tgz

# ---------------------------------------------------------------------------------------------
FROM toolchain AS ci
COPY docker/pins.env /opt/cemkit/pins.env
RUN mkdir -p /home/dev /workspace && chmod 1777 /home/dev
ENV HOME=/home/dev \
    CC=gcc-13 \
    CXX=g++-13 \
    CEMKIT_IMAGE_TARGET=ci
WORKDIR /workspace
CMD ["bash"]

# ---------------------------------------------------------------------------------------------
FROM toolchain AS dev
ARG OPENFOAM_VERSION
ARG OPENFOAM_DEB_VERSION
ARG OPENFOAM_REPO_URL
ARG OPENFOAM_PUBKEY_URL
ARG OPENFOAM_PUBKEY_SHA256
ARG OPENFOAM_PUBKEY_FPR

# OpenFOAM (own layer). The OpenCFD repository is not covered by the Ubuntu snapshot, so the key is
# checked by hash and fingerprint and the packages are pinned to exact versions.
RUN curl -fsSL "${OPENFOAM_PUBKEY_URL}" -o /tmp/openfoam-pubkey.gpg \
 && echo "${OPENFOAM_PUBKEY_SHA256}  /tmp/openfoam-pubkey.gpg" | sha256sum -c - \
 && gpg --show-keys --with-colons /tmp/openfoam-pubkey.gpg | awk -F: '/^fpr/{print $10; exit}' | grep -qx "${OPENFOAM_PUBKEY_FPR}" \
 && gpg --dearmor < /tmp/openfoam-pubkey.gpg > /usr/share/keyrings/openfoam.gpg \
 && rm /tmp/openfoam-pubkey.gpg \
 && echo "deb [arch=amd64 signed-by=/usr/share/keyrings/openfoam.gpg] ${OPENFOAM_REPO_URL} noble main" > /etc/apt/sources.list.d/openfoam.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      "openfoam${OPENFOAM_VERSION}-default=${OPENFOAM_DEB_VERSION}" \
      "openfoam${OPENFOAM_VERSION}-dev=${OPENFOAM_DEB_VERSION}" \
      "openfoam${OPENFOAM_VERSION}-tutorials=${OPENFOAM_DEB_VERSION}" \
      "openfoam${OPENFOAM_VERSION}=${OPENFOAM_DEB_VERSION}" \
      "openfoam${OPENFOAM_VERSION}-common=${OPENFOAM_DEB_VERSION}" \
 && rm -rf /var/lib/apt/lists/*

ARG APT_CALCULIX_CCX
RUN apt-get update \
 && apt-get install -y --no-install-recommends "calculix-ccx=${APT_CALCULIX_CCX}" \
 && rm -rf /var/lib/apt/lists/*

# GNU time (/usr/bin/time -v) for the T03 memory measurement (scripts/measure_openfoam_memory.sh).
ARG APT_TIME
RUN apt-get update \
 && apt-get install -y --no-install-recommends "time=${APT_TIME}" \
 && rm -rf /var/lib/apt/lists/*

# Gmsh is installed once, here. T02's Python project must reuse /opt/gmsh-venv, not pin a second copy.
# The wheel's libgmsh needs these shared libraries even headless (each was proven missing with ldd).
ARG APT_LIBGLU1_MESA
ARG APT_LIBGL1
ARG APT_LIBXRENDER1
ARG APT_LIBXCURSOR1
ARG APT_LIBXFIXES3
ARG APT_LIBXFT2
ARG APT_LIBFONTCONFIG1
ARG APT_LIBXINERAMA1
RUN apt-get update \
 && apt-get install -y --no-install-recommends "libglu1-mesa=${APT_LIBGLU1_MESA}" "libgl1=${APT_LIBGL1}" "libxrender1=${APT_LIBXRENDER1}" "libxcursor1=${APT_LIBXCURSOR1}" "libxfixes3=${APT_LIBXFIXES3}" "libxft2=${APT_LIBXFT2}" "libfontconfig1=${APT_LIBFONTCONFIG1}" "libxinerama1=${APT_LIBXINERAMA1}" \
 && rm -rf /var/lib/apt/lists/*
ARG GMSH_VERSION
ARG GMSH_WHEEL_SHA256
RUN uv venv /opt/gmsh-venv --python "${PYTHON_VERSION}" \
 && printf 'gmsh==%s --hash=sha256:%s\n' "${GMSH_VERSION}" "${GMSH_WHEEL_SHA256}" > /tmp/gmsh-req.txt \
 && uv pip install --python /opt/gmsh-venv/bin/python --require-hashes --no-deps -r /tmp/gmsh-req.txt \
 && rm /tmp/gmsh-req.txt \
 && printf '#!/bin/sh\nexec /opt/gmsh-venv/bin/python /opt/gmsh-venv/bin/gmsh "$@"\n' > /usr/local/bin/gmsh \
 && chmod 0755 /usr/local/bin/gmsh \
 && chmod -R a+rX /opt/gmsh-venv


COPY docker/pins.env /opt/cemkit/pins.env
RUN mkdir -p /home/dev /workspace && chmod 1777 /home/dev \
 && echo '[ -f /usr/lib/openfoam/openfoam*/etc/bashrc ] && . /usr/lib/openfoam/openfoam*/etc/bashrc' >> /etc/bash.bashrc
ENV HOME=/home/dev \
    CC=gcc-13 \
    CXX=g++-13 \
    CEMKIT_IMAGE_TARGET=dev
WORKDIR /workspace
CMD ["bash"]
