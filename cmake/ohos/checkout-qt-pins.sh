#!/usr/bin/env bash
# Host-side helper (Git Bash on Windows or Linux): clone the pinned Qt dev
# modules used by the Mixxx OHOS port and check out the pinned SHAs.
# Usage: cmake/ohos/checkout-qt-pins.sh [target-dir]
# The proxy is only needed where GitHub is unreachable directly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PINS_FILE="${SCRIPT_DIR}/qt-ohos-pins.txt"
TARGET_DIR="${1:-$(pwd)/qt6}"
PROXY="${MIXXX_GIT_PROXY:-http://127.0.0.1:8080}"

mkdir -p "${TARGET_DIR}"
cd "${TARGET_DIR}"

while read -r module sha; do
  case "${module}" in ''|\#*) continue ;; esac
  if [ ! -d "${module}/.git" ]; then
    echo "==> cloning ${module}"
    if git -c http.proxy="${PROXY}" clone --filter=blob:none \
        "https://github.com/qt/${module}.git" "${module}" 2>/dev/null; then
      echo "    cloned via proxy"
    else
      git clone --filter=blob:none "https://github.com/qt/${module}.git" "${module}"
      echo "    cloned directly (proxy unavailable)"
    fi
  fi
  current="$(git -C "${module}" rev-parse HEAD)"
  if [ "${current}" != "${sha}" ]; then
    echo "==> checking out ${module} @ ${sha} (was ${current})"
    git -C "${module}" fetch -q origin "${sha}" || true
    git -C "${module}" checkout -q --detach "${sha}"
  else
    echo "==> ${module} already at pinned SHA"
  fi
done < "${PINS_FILE}"

echo "All Qt modules ready under ${TARGET_DIR}"
