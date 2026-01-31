#!/usr/bin/env bash
set -euo pipefail

ADB_BIN="${ADB_BIN:-adb}"
SERIAL="${ANDROID_SERIAL:-}"
OUT_DIR="${OUT_DIR:-android_stress_artifacts}"
BIN_NAME="${BIN_NAME:-benchmark}"
BIN_PATH="${BIN_PATH:-dist/${BIN_NAME}}"
REMOTE_BIN="/data/local/tmp/${BIN_NAME}"
REMOTE_LOG="/data/local/tmp/${BIN_NAME}.log"
FPS="${FPS:-120}"
SHAPES="${SHAPES:-300}"
ACTIVE_MS="${ACTIVE_MS:-4}"
DURATION="${DURATION:-8}"

ADB=("${ADB_BIN}")
if [[ -n "${SERIAL}" ]]; then
  ADB+=("-s" "${SERIAL}")
fi

mkdir -p "${OUT_DIR}"

echo "[stress] waiting for device..."
"${ADB[@]}" wait-for-device

echo "[stress] device info:"
"${ADB[@]}" shell getprop ro.build.version.release || true
SDK_VER=$("${ADB[@]}" shell getprop ro.build.version.sdk | tr -d '\r')
echo "[stress] SDK=${SDK_VER}"

echo "[stress] trying adb root..."
if ! "${ADB[@]}" root >/dev/null 2>&1; then
  echo "[stress] adb root failed (device may not support)" >&2
  exit 3
fi

sleep 1
"${ADB[@]}" wait-for-device

if [[ ! -f "${BIN_PATH}" ]]; then
  echo "[stress] binary not found: ${BIN_PATH}" >&2
  exit 4
fi

echo "[stress] pushing ${BIN_NAME}..."
"${ADB[@]}" push "${BIN_PATH}" "${REMOTE_BIN}" >/dev/null
"${ADB[@]}" shell chmod 755 "${REMOTE_BIN}"

echo "[stress] running stress test (${SHAPES} shapes, ${FPS} FPS, ${ACTIVE_MS}ms active)..."
"${ADB[@]}" shell "MIDRAW_STRESS=1 MIDRAW_SHAPES=${SHAPES} MIDRAW_FPS=${FPS} MIDRAW_ACTIVE_MS=${ACTIVE_MS} timeout ${DURATION} ${REMOTE_BIN} > ${REMOTE_LOG} 2>&1" || true

echo "[stress] collecting logs..."
"${ADB[@]}" logcat -d -v threadtime > "${OUT_DIR}/logcat.txt" || true
"${ADB[@]}" shell cat "${REMOTE_LOG}" > "${OUT_DIR}/app_log.txt" || true

echo "[stress] done. artifacts in ${OUT_DIR}"
