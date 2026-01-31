#!/usr/bin/env bash
set -euo pipefail

ADB_BIN="${ADB_BIN:-adb}"
SERIAL="${ANDROID_SERIAL:-}"
OUT_DIR="${OUT_DIR:-android16_root_artifacts}"
REQUIRED_SDK="${REQUIRED_SDK:-36}"
BIN_NAME="${BIN_NAME:-overlay_engine}"
BIN_PATH="${BIN_PATH:-dist/${BIN_NAME}}"
REMOTE_BIN="/data/local/tmp/${BIN_NAME}"
REMOTE_LOG="/data/local/tmp/${BIN_NAME}.log"

ADB=("${ADB_BIN}")
if [[ -n "${SERIAL}" ]]; then
  ADB+=("-s" "${SERIAL}")
fi

mkdir -p "${OUT_DIR}"

echo "[demo] waiting for device..."
"${ADB[@]}" wait-for-device

echo "[demo] device info:"
"${ADB[@]}" shell getprop ro.build.version.release || true
SDK_VER=$("${ADB[@]}" shell getprop ro.build.version.sdk | tr -d '\r')
echo "[demo] SDK=${SDK_VER}"
if [[ -n "${REQUIRED_SDK}" ]]; then
  if [[ "${SDK_VER}" -lt "${REQUIRED_SDK}" ]]; then
    echo "[demo] SDK ${SDK_VER} < required ${REQUIRED_SDK}" >&2
    exit 2
  fi
fi

echo "[demo] trying adb root..."
if ! "${ADB[@]}" root >/dev/null 2>&1; then
  echo "[demo] adb root failed (device may not support)" >&2
  exit 3
fi

sleep 1
"${ADB[@]}" wait-for-device

if [[ ! -f "${BIN_PATH}" ]]; then
  echo "[demo] binary not found: ${BIN_PATH}" >&2
  exit 4
fi

echo "[demo] pushing binary..."
"${ADB[@]}" push "${BIN_PATH}" "${REMOTE_BIN}" >/dev/null
"${ADB[@]}" shell chmod 755 "${REMOTE_BIN}"

echo "[demo] running ${BIN_NAME}..."
"${ADB[@]}" shell "nohup ${REMOTE_BIN} > ${REMOTE_LOG} 2>&1 &"
sleep 3

echo "[demo] collecting logs..."
"${ADB[@]}" logcat -d -v threadtime > "${OUT_DIR}/logcat.txt" || true
"${ADB[@]}" shell cat "${REMOTE_LOG}" > "${OUT_DIR}/app_log.txt" || true

echo "[demo] grabbing tombstone (if any)..."
TOMBSTONE=$("${ADB[@]}" shell "ls -t /data/tombstones 2>/dev/null | head -1" | tr -d '\r')
if [[ -n "${TOMBSTONE}" ]]; then
  "${ADB[@]}" shell cat "/data/tombstones/${TOMBSTONE}" > "${OUT_DIR}/tombstone.txt" || true
fi

echo "[demo] cleanup..."
"${ADB[@]}" shell pkill -f "${REMOTE_BIN}" || true

echo "[demo] done. artifacts in ${OUT_DIR}"
