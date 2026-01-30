#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_ROOT="${ANDROID_SDK_ROOT:-${ROOT_DIR}/.android_sdk}"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/generated/syslibs}"
APIS="${ANDROID_APIS:-29 30 31 32 33 34 35 36}"

PYTHON_BIN="${PYTHON_BIN:-python3}"

mkdir -p "${OUT_DIR}"

for api in ${APIS}; do
  sysimg_dir="${SDK_ROOT}/system-images/android-${api}/google_apis/x86_64"
  system_img="${sysimg_dir}/system.img"
  if [[ ! -f "${system_img}" ]]; then
    echo "Missing system image for API ${api}: ${system_img}" >&2
    exit 1
  fi

  api_out="${OUT_DIR}/android-${api}"
  mkdir -p "${api_out}/lib64"

  raw_img="${api_out}/system.raw.img"
  "${PYTHON_BIN}" "${ROOT_DIR}/tools/sparse_img_to_raw.py" "${system_img}" "${raw_img}"

  if ! debugfs -R "dump /system/lib64/libandroid.so ${api_out}/lib64/libandroid.so" "${raw_img}" \
    && ! debugfs -R "dump /lib64/libandroid.so ${api_out}/lib64/libandroid.so" "${raw_img}"; then
    echo "Failed to extract libandroid.so for API ${api}" >&2
    exit 1
  fi
  if ! debugfs -R "dump /system/lib64/libgui.so ${api_out}/lib64/libgui.so" "${raw_img}" \
    && ! debugfs -R "dump /lib64/libgui.so ${api_out}/lib64/libgui.so" "${raw_img}"; then
    echo "Failed to extract libgui.so for API ${api}" >&2
    exit 1
  fi
  if ! debugfs -R "dump /system/lib64/libutils.so ${api_out}/lib64/libutils.so" "${raw_img}" \
    && ! debugfs -R "dump /lib64/libutils.so ${api_out}/lib64/libutils.so" "${raw_img}"; then
    echo "Failed to extract libutils.so for API ${api}" >&2
    exit 1
  fi

  rm -f "${raw_img}"
done
