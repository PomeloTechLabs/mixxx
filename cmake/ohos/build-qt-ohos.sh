#!/usr/bin/env bash
# Build Qt 6 (pinned dev branch) for the Mixxx HarmonyOS/OHOS port:
#   stage "host"  : Linux host build of qtbase + qtshadertools + qtdeclarative
#                   (cross-tools: moc/rcc, qsb, qmlcachegen)
#   stage "cross" : cross build of qtbase + qtshadertools + qtsvg +
#                   qtimageformats + qtdeclarative for OHOS (arm64-v8a)
#   stage "all"   : both, in order
# Runs INSIDE a container; see cmake/ohos/BUILD_QT_OHOS.md for the host-side
# docker invocation. Build trees live in a persistent volume so reruns are
# incremental.
set -euo pipefail

OHOS_SDK="${OHOS_SDK:-/apps/harmony/sdk/default/openharmony}"
OHOS_ARCH="${OHOS_ARCH:-arm64-v8a}"
QT_SRC_ROOT="${QT_SRC_ROOT:-/data/qt6}"
QT_BUILD_ROOT="${QT_BUILD_ROOT:-/data/build}"
QT_OUT_ROOT="${QT_OUT_ROOT:-/data/out}"
QT_HOST_PREFIX="${QT_OUT_ROOT}/qt-host"
QT_CROSS_PREFIX="${QT_OUT_ROOT}/qt-ohos"
QT_CROSS_DEVICE_PREFIX="${QT_OUT_ROOT}/qt-ohos-device"
# node-addon-api (header-only) is required by Qt's ohos QPA (NODE_ADDON_API_ROOT)
NODE_ADDON_API_TAG="${NODE_ADDON_API_TAG:-v8.9.2}"
NODE_ADDON_API_DIR="${QT_OUT_ROOT}/extras/node-addon-api-${NODE_ADDON_API_TAG}"
# vcpkg arm64-ohos installed tree (mount the mixxx-ohos-vcpkg volume here).
# Qt's ohos QPA unconditionally needs fontconfig, which the public NDK
# sysroot lacks; vcpkg provides it.
QT_VCPKG_ROOT="${QT_VCPKG_ROOT:-/data/vcpkg/vcpkg}"
QT_VCPKG_TRIPLET="${QT_VCPKG_TRIPLET:-arm64-ohos}"
QT_VCPKG_INSTALLED="${QT_VCPKG_ROOT}/installed/${QT_VCPKG_TRIPLET}"
PROXY="${PROXY:-http://host.docker.internal:8080}"
STAGE="${STAGE:-all}"
JOBS="${JOBS:-$(nproc)}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PINS_FILE="${SCRIPT_DIR}/qt-ohos-pins.txt"
OHOS_TOOLCHAIN="${OHOS_SDK}/native/build/cmake/ohos.toolchain.cmake"

log() { printf '\n===== [qt-ohos] %s =====\n' "$*"; }

verify_pins() {
  log "verifying pinned module SHAs"
  local mismatch=0
  while read -r module sha; do
    case "${module}" in ''|\#*) continue ;; esac
    local current
    current="$(git -C "${QT_SRC_ROOT}/${module}" rev-parse HEAD 2>/dev/null || echo MISSING)"
    if [ "${current}" != "${sha}" ]; then
      echo "ERROR: ${module} is at ${current}, pinned ${sha}" >&2
      mismatch=1
    fi
  done < "${PINS_FILE}"
  [ "${mismatch}" -eq 0 ] || { echo "Pin check failed; run cmake/ohos/checkout-qt-pins.sh on the host." >&2; exit 1; }
}

# Host Gui needs GL headers for qtdeclarative's Quick module; when apt can
# provide them we build Quick too, otherwise we fall back to tools-only.
ensure_host_gl() {
  if printf '#include <GL/gl.h>\nint main(){return 0;}' | cc -x c - -o /dev/null 2>/dev/null; then
    echo 0; return
  fi
  if command -v apt-get >/dev/null 2>&1; then
    apt-get update >/dev/null 2>&1 || true
    apt-get install -y --no-install-recommends libgl1-mesa-dev libegl1-mesa-dev \
      >/dev/null 2>&1 || true
  fi
  if printf '#include <GL/gl.h>\nint main(){return 0;}' | cc -x c - -o /dev/null 2>/dev/null; then
    echo 0; return
  fi
  echo 1
}

host_common_cmake_args() {
  local no_gl="$1"
  printf '%s\n' \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DFEATURE_opengl=$([ "${no_gl}" = "1" ] && echo OFF || echo ON) \
    -DFEATURE_xcb=OFF \
    -DFEATURE_dbus=OFF \
    -DFEATURE_vulkan=OFF \
    -DFEATURE_glib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_zstd=OFF \
    -DFEATURE_brotli=OFF \
    -DFEATURE_journald=OFF \
    -DFEATURE_sql_mysql=OFF \
    -DFEATURE_sql_psql=OFF \
    -DFEATURE_sql_odbc=OFF
}

build_host() {
  local no_gl
  no_gl="$(ensure_host_gl)"
  log "host stage (no_gl=${no_gl}, jobs=${JOBS})"

  log "host: qtbase"
  cmake -S "${QT_SRC_ROOT}/qtbase" -B "${QT_BUILD_ROOT}/host/qtbase" \
    $(host_common_cmake_args "${no_gl}") \
    -DCMAKE_INSTALL_PREFIX="${QT_HOST_PREFIX}"
  cmake --build "${QT_BUILD_ROOT}/host/qtbase" -j "${JOBS}"
  cmake --install "${QT_BUILD_ROOT}/host/qtbase"

  log "host: qtshadertools"
  cmake -S "${QT_SRC_ROOT}/qtshadertools" -B "${QT_BUILD_ROOT}/host/qtshadertools" \
    -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_HOST_PREFIX}" \
    -DCMAKE_INSTALL_PREFIX="${QT_HOST_PREFIX}" \
    -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
  cmake --build "${QT_BUILD_ROOT}/host/qtshadertools" -j "${JOBS}"
  cmake --install "${QT_BUILD_ROOT}/host/qtshadertools"

  log "host: qtdeclarative"
  local qd_args=()
  if [ "${no_gl}" = "1" ]; then
    # Tools-only fallback: Quick needs host GL; qmlcachegen/qmllint do not.
    qd_args=(-DFEATURE_quick=OFF -DFEATURE_quick_controls2=OFF
             -DFEATURE_quick_shapes=OFF -DFEATURE_quick_particles=OFF)
    echo "WARNING: host GL unavailable; building qtdeclarative tools-only" >&2
  fi
  cmake -S "${QT_SRC_ROOT}/qtdeclarative" -B "${QT_BUILD_ROOT}/host/qtdeclarative" \
    -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_HOST_PREFIX}" \
    -DCMAKE_INSTALL_PREFIX="${QT_HOST_PREFIX}" \
    -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
    "${qd_args[@]}"
  cmake --build "${QT_BUILD_ROOT}/host/qtdeclarative" -j "${JOBS}"
  cmake --install "${QT_BUILD_ROOT}/host/qtdeclarative"

  log "host stage done: ${QT_HOST_PREFIX}"
}

ensure_node_addon_api() {
  if [ -f "${NODE_ADDON_API_DIR}/napi.h" ]; then
    return 0
  fi
  log "fetching node-addon-api ${NODE_ADDON_API_TAG}"
  mkdir -p "${NODE_ADDON_API_DIR}"
  curl -fsSL -x "${PROXY}" \
    "https://github.com/nodejs/node-addon-api/archive/refs/tags/${NODE_ADDON_API_TAG}.tar.gz" \
    | tar xz -C "${NODE_ADDON_API_DIR}" --strip-components=1
}

cross_common_cmake_args() {
  # NOTE: the OHOS toolchain forces FIND_ROOT_PATH_MODE_INCLUDE=ONLY, which
  # re-roots every find_path/library HINTS into the sysroot. Extra roots
  # (node-addon-api now, cross Qt prefix for later modules) must be listed
  # explicitly via CMAKE_FIND_ROOT_PATH or nothing outside the NDK is found.
  printf '%s\n' \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${OHOS_TOOLCHAIN}" \
    -DOHOS_ARCH="${OHOS_ARCH}" \
    -DQT_HOST_PATH="${QT_HOST_PREFIX}" \
    -DNODE_ADDON_API_ROOT="${NODE_ADDON_API_DIR}" \
    -DCMAKE_FIND_ROOT_PATH="${NODE_ADDON_API_DIR};${QT_VCPKG_INSTALLED}" \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DFEATURE_vulkan=OFF \
    -DFEATURE_glib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_zstd=OFF \
    -DFEATURE_brotli=OFF \
    -DFEATURE_dbus=OFF \
    -DFEATURE_openssl=OFF \
    -DFEATURE_system_zlib=OFF \
    -DFEATURE_system_pcre2=OFF \
    -DFEATURE_system_sqlite=OFF \
    -DFEATURE_sql_mysql=OFF \
    -DFEATURE_sql_psql=OFF \
    -DFEATURE_sql_odbc=OFF
}

build_cross() {
  ensure_node_addon_api
  log "cross stage for ${OHOS_ARCH} (jobs=${JOBS})"

  log "cross: qtbase"
  cmake -S "${QT_SRC_ROOT}/qtbase" -B "${QT_BUILD_ROOT}/cross/qtbase" \
    $(cross_common_cmake_args) \
    -DCMAKE_STAGING_PREFIX="${QT_CROSS_PREFIX}" \
    -DCMAKE_INSTALL_PREFIX="${QT_CROSS_DEVICE_PREFIX}"
  cmake --build "${QT_BUILD_ROOT}/cross/qtbase" -j "${JOBS}"
  cmake --install "${QT_BUILD_ROOT}/cross/qtbase"

  for module in qtshadertools qtsvg qtimageformats qtdeclarative; do
    log "cross: ${module}"
    cmake -S "${QT_SRC_ROOT}/${module}" -B "${QT_BUILD_ROOT}/cross/${module}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="${OHOS_TOOLCHAIN}" \
      -DOHOS_ARCH="${OHOS_ARCH}" \
      -DCMAKE_PREFIX_PATH="${QT_CROSS_PREFIX}" \
      -DQT_HOST_PATH="${QT_HOST_PREFIX}" \
      -DCMAKE_FIND_ROOT_PATH="${QT_CROSS_PREFIX};${NODE_ADDON_API_DIR};${QT_VCPKG_INSTALLED}" \
      -DCMAKE_STAGING_PREFIX="${QT_CROSS_PREFIX}" \
      -DCMAKE_INSTALL_PREFIX="${QT_CROSS_DEVICE_PREFIX}" \
      -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    cmake --build "${QT_BUILD_ROOT}/cross/${module}" -j "${JOBS}"
    cmake --install "${QT_BUILD_ROOT}/cross/${module}"
  done

  log "cross stage done: ${QT_CROSS_PREFIX}"
}

verify_pins

case "${STAGE}" in
  host)  build_host ;;
  cross) build_cross ;;
  all)   build_host; build_cross ;;
  *) echo "STAGE must be host|cross|all" >&2; exit 2 ;;
esac

log "ALL DONE"
