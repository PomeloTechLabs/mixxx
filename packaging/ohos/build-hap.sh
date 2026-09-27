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
find "${QT_OHOS}/lib" -maxdepth 1 -name "*.so*" -exec cp -L {} "${LIBS}/" \;
# C++ runtime: every Qt library is linked against libc++_shared.so
cp -f "${CLT}/sdk/default/openharmony/native/llvm/lib/aarch64-linux-ohos/libc++_shared.so" \
   "${LIBS}/"
# Qt plugins (platforms/, imageformats/, multimedia/, sqldrivers/, ...).
cp -r "${QT_OHOS}/plugins/." "${LIBS}/"
# The QPA plugin is also the ArkTS NAPI entry point ("import ... from
# 'libqohos.so'") and Qt sets QT_QPA_PLATFORM_PLUGIN_PATH to the libs root,
# so it must exist at the root, not only under platforms/.
cp -f "${QT_OHOS}/plugins/platforms/libqohos.so" "${LIBS}/"
# Do NOT keep a second copy under platforms/: two mappings of the same
# plugin mean two independent static states (peer registries) and Qt's
# window-proxy lookup then fails in makeWindowProxyDataForExistingMainWindow.
rm -f "${LIBS}/platforms/libqohos.so"
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
# Mixxx application library (built by the Docker cmake build)
cp -f /data/mixxx-build/ohos/libmixxx.so "${LIBS}/" 2>/dev/null || \
   echo "NOTE: libmixxx.so not available in the build volume"

# Mixxx resources (skins/qml/controllers/...). Non-.so files inside the HAP's
# libs/ directory are NOT extracted on install, so they are shipped through
# resources/resfile/, which the system provides as real files (reachable via
# the app's resourceDir). Translations are skipped in bring-up.
RESFILE="${PACK}/entry/src/main/resources/resfile/res"
rm -rf "${RESFILE}"
mkdir -p "${RESFILE}"
for d in qml skins controllers fonts images keyboard shaders effects; do
  if [ -e "${REPO}/res/${d}" ]; then cp -r "${REPO}/res/${d}" "${RESFILE}/"; fi
done
[ -f "${REPO}/res/schema.xml" ] && cp -f "${REPO}/res/schema.xml" "${RESFILE}/"

# Windows-side hvigor cannot stat Linux symlinks, so materialise every one.
find "${LIBS}" -type l 2>/dev/null | while read -r f; do
  cp -L "$f" "$f.__tmp" 2>/dev/null && mv -f "$f.__tmp" "$f"
done

log "libs summary: $(ls "${LIBS}" | wc -l) entries, $(du -sh "${LIBS}" | cut -f1)"

if [ "${STAGE_ONLY:-0}" = "1" ]; then
  log "STAGE_ONLY=1: skipping hvigor (run it with the DevEco CLI to sign)"
  exit 0
fi

log "3/4 node/hvigor environment"
export NODE_HOME="${CLT}/tool/node"
export PATH="${NODE_HOME}/bin:${CLT}/bin:${PATH}"
cd "${PACK}"
hvigorw --mode module -p module=entry@default -p product=default \
  assembleHap --no-daemon 2>&1 | tail -25

log "4/4 artifact"
HAP=$(find entry/build -name "*unsigned.hap" | head -1)
[ -n "${HAP}" ] && ls -la "${HAP}" || echo "WARNING: no HAP produced"
