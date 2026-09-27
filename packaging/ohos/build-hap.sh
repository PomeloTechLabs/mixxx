#!/usr/bin/env bash
# Build the Mixxx OHOS HAP shell (TASK-003).
# Runs INSIDE the winehua-dev container with:
#   /data/src/mixxx            - repo (this packaging/ohos lives here)
#   /data/out (qt-out volume)  - Qt for OHOS install (qt-ohos) + host (qt-host)
#   /data/vcpkg (vcpkg volume) - vcpkg root (user ffmpeg 7.1.1 shared libs live
#                                in installed/arm64-ohos/usr/lib)
#   /apps/harmony              - HarmonyOS command-line-tools (linux-x64)
# Output: entry/build/default/outputs/default/entry-default-unsigned.hap
set -euo pipefail

REPO=/data/src/mixxx
PACK=${REPO}/packaging/ohos
QT_OHOS=/data/out/qt-ohos
QT_HOST=/data/out/qt-host
VCPKG_LIB=/data/vcpkg/vcpkg/installed/arm64-ohos/usr/lib
CLT=/apps/harmony
ARCH=arm64-v8a   # HarmonyOS ABI dir name (must match the device abilist)

log() { printf '\n===== [hap] %s =====\n' "$*"; }

log "1/4 building libqtmixxxboot.so (Qt boot shell)"
BUILD=/data/mixxx-build/hap-boot
cmake -S "${PACK}/entry/src/main/cpp" -B "${BUILD}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${CLT}/sdk/default/openharmony/native/build/cmake/ohos.toolchain.cmake" \
  -DOHOS_ARCH=arm64-v8a \
  -DCMAKE_BUILD_TYPE=Release \
  -DQT_OHOS_PREFIX="${QT_OHOS}" \
  -DQT_HOST_PREFIX="${QT_HOST}" \
  -DCMAKE_STAGING_PREFIX="${BUILD}/install"
cmake --build "${BUILD}" -j "$(nproc)"

log "2/4 collecting HAP libs (entry/libs/${ARCH})"
LIBS="${PACK}/entry/libs/${ARCH}"
rm -rf "${LIBS}"
mkdir -p "${LIBS}"

# Qt libraries (everything shipped in the cross install tree)
find "${QT_OHOS}/lib" -maxdepth 1 -name "*.so*" -exec cp -P {} "${LIBS}/" \;
# Qt plugins (platforms/, imageformats/, multimedia/, sqldrivers/, ...).
cp -r "${QT_OHOS}/plugins/." "${LIBS}/"
# The QPA plugin is also the ArkTS NAPI entry point ("import ... from
# 'libqohos.so'") and Qt sets QT_QPA_PLATFORM_PLUGIN_PATH to the libs root,
# so it must exist at the root, not only under platforms/.
cp -f "${QT_OHOS}/plugins/platforms/libqohos.so" "${LIBS}/"
# QML module tree: .so files live under libs (dlopen-able), qmldir next to them
cp -r "${QT_OHOS}/qml" "${LIBS}/qml"
# Boot shell
find "${BUILD}" -name "libqtmixxxboot.so" -exec cp {} "${LIBS}/" \;
# User FFmpeg 7.1.1 shared libraries (runtime deps of libmixxx.so; bundled
# from the next TASK-004 step, not needed by the boot shell but cheap to
# include so TASK-004 needs no repack)
cp -P "${VCPKG_LIB}"/libavcodec.so* "${VCPKG_LIB}"/libavformat.so* \
   "${VCPKG_LIB}"/libavutil.so* "${VCPKG_LIB}"/libswresample.so* \
   "${VCPKG_LIB}"/libswscale.so* "${VCPKG_LIB}"/libavfilter.so* \
   "${VCPKG_LIB}"/libavdevice.so* "${LIBS}/" 2>/dev/null || true
cp "${REPO}/build/ohos-lib/libmixxx.so" "${LIBS}/" 2>/dev/null || \
   echo "NOTE: libmixxx.so not staged yet (TASK-004 will add it)"

log "libs summary: $(ls "${LIBS}" | wc -l) entries, $(du -sh "${LIBS}" | cut -f1)"

log "3/4 node/hvigor environment"
export NODE_HOME="${CLT}/tool/node"
export PATH="${NODE_HOME}/bin:${CLT}/bin:${PATH}"
cd "${PACK}"
hvigorw --mode module -p module=entry@default -p product=default \
  assembleHap --no-daemon 2>&1 | tail -25

log "4/4 artifact"
HAP=$(find entry/build -name "*unsigned.hap" | head -1)
[ -n "${HAP}" ] && ls -la "${HAP}" || echo "WARNING: no HAP produced"
