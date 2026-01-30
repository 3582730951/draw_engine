#!/usr/bin/env python3
import argparse
import os
import re
import subprocess
from collections import defaultdict


SDK_MAP = {
    10: 29,
    11: 30,
    12: 31,
    13: 33,
    14: 34,
    15: 35,
    16: 36,
}


PATTERNS = {
    "SurfaceComposerClient_ctor": [
        r".*SurfaceComposerClient.*C[12]E.*",
    ],
    "SurfaceComposerClient_dtor": [
        r".*SurfaceComposerClient.*D[012]E.*",
    ],
    "SurfaceComposerClient_getDefault": [
        r".*SurfaceComposerClient.*getDefault.*",
    ],
    "SurfaceComposerClient_openGlobalTransaction": [
        r".*SurfaceComposerClient.*openGlobalTransaction.*",
    ],
    "SurfaceComposerClient_closeGlobalTransaction": [
        r".*SurfaceComposerClient.*closeGlobalTransaction.*",
    ],
    "SurfaceComposerClient_createSurface": [
        r".*SurfaceComposerClient.*createSurface(?!Checked).*",
    ],
    "SurfaceComposerClient_createSurfaceChecked": [
        r".*SurfaceComposerClient.*createSurfaceChecked.*",
    ],
    "SurfaceControl_getSurface": [r".*SurfaceControl.*getSurface.*"],
    "SurfaceControl_setLayer": [r".*SurfaceControl.*setLayer.*"],
    "SurfaceControl_setPosition": [r".*SurfaceControl.*setPosition.*"],
    "SurfaceControl_setSize": [r".*SurfaceControl.*setSize.*"],
    "SurfaceControl_setAlpha": [r".*SurfaceControl.*setAlpha.*"],
    "SurfaceControl_setFlags": [r".*SurfaceControl.*setFlags.*"],
    "SurfaceControl_setBufferSize": [r".*SurfaceControl.*setBufferSize.*"],
    "String8_ctor": [r".*String8.*C[12].*PKc.*"],
    "String8_dtor": [r".*String8.*D[012]Ev.*"],
    "ASurfaceControl_create": [r"^ASurfaceControl_create$"],
    "ASurfaceControl_release": [r"^ASurfaceControl_release$"],
    "ASurfaceTransaction_setAlpha": [r"^ASurfaceTransaction_setAlpha$"],
    "ASurfaceTransaction_setOpaque": [r"^ASurfaceTransaction_setOpaque$"],
    "ASurfaceTransaction_setVisibility": [r"^ASurfaceTransaction_setVisibility$"],
    "ASurfaceTransaction_setLayer": [r"^ASurfaceTransaction_setLayer$"],
    "ANativeWindow_setBuffersGeometry": [r"^ANativeWindow_setBuffersGeometry$"],
}

LIB_HINTS = {
    "String8_ctor": "libutils",
    "String8_dtor": "libutils",
    "ANativeWindow_setBuffersGeometry": "libandroid",
}


def parse_sdk_from_dir(name: str) -> int:
    lower = name.lower()
    if "12l" in lower or "12.1" in lower:
        return 32
    m = re.search(r"(?:android-)?(\d{2})", lower)
    if m:
        major = int(m.group(1))
        if major in SDK_MAP:
            return SDK_MAP[major]
        if 29 <= major <= 36:
            return major
        if 10 <= major <= 16:
            return major + 19
    return 0


def run_nm(nm_path: str, so_path: str) -> set:
    try:
        output = subprocess.check_output(
            [nm_path, "-D", "--defined-only", so_path],
            stderr=subprocess.DEVNULL,
            text=True,
            errors="ignore",
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return set()
    symbols = set()
    for line in output.splitlines():
        parts = line.split()
        if not parts:
            continue
        name = parts[-1]
        if "@" in name:
            name = name.split("@", 1)[0]
        symbols.add(name)
    return symbols


def collect_versions(roots):
    versions = {}
    for root in roots:
        for dirpath, _, filenames in os.walk(root):
            if "libgui.so" in filenames or "libandroid.so" in filenames or "libutils.so" in filenames:
                sdk = parse_sdk_from_dir(os.path.basename(dirpath))
                versions[dirpath] = sdk
    return versions


def filter_symbols(symbols: set, pattern_list: list) -> list:
    matches = []
    regexes = [re.compile(pat) for pat in pattern_list]
    for sym in symbols:
        for rgx in regexes:
            if rgx.match(sym):
                matches.append(sym)
                break
    return sorted(set(matches))


def generate_header(out_path: str, versions: dict):
    version_entries = []
    for path, sdk in sorted(versions.items(), key=lambda kv: kv[1]):
        libgui = os.path.join(path, "libgui.so")
        libandroid = os.path.join(path, "libandroid.so")
        libutils = os.path.join(path, "libutils.so")
        if not os.path.exists(libgui) and not os.path.exists(libandroid) and not os.path.exists(libutils):
            continue
        version_entries.append((path, sdk, libgui, libandroid, libutils))

    all_results = []
    for path, sdk, libgui, libandroid, libutils in version_entries:
        symbols_gui = run_nm(NM_PATH, libgui) if os.path.exists(libgui) else set()
        symbols_android = run_nm(NM_PATH, libandroid) if os.path.exists(libandroid) else set()
        symbols_utils = run_nm(NM_PATH, libutils) if os.path.exists(libutils) else set()
        results = {}
        for logical, patterns in PATTERNS.items():
            hint = LIB_HINTS.get(logical)
            if hint == "libutils":
                src = symbols_utils
            elif hint == "libandroid":
                src = symbols_android
            elif logical.startswith("A") or logical.startswith("ANativeWindow"):
                src = symbols_android if "ANativeWindow" in logical else (symbols_gui | symbols_android)
            else:
                src = symbols_gui
            results[logical] = filter_symbols(src, patterns)
        all_results.append((sdk, results))

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write("#include <stddef.h>\n\n")
        f.write("struct SymbolNameList {\n")
        f.write("  const char* const* names;\n")
        f.write("  size_t count;\n")
        f.write("};\n\n")
        f.write("struct SymbolVersion {\n")
        f.write("  int sdk;\n")
        for logical in PATTERNS.keys():
            f.write(f"  SymbolNameList {logical};\n")
        f.write("};\n\n")

        for sdk, results in all_results:
            for logical, names in results.items():
                arr_name = f"kNames_{sdk}_{logical}"
                if names:
                    items = ", ".join([f\"\\\"{n}\\\"\" for n in names])
                    f.write(f"static const char* {arr_name}[] = {{{items}}};\n")
                else:
                    f.write(f"static const char* {arr_name}[] = {{}};\n")
            f.write("\n")

        f.write("static const SymbolVersion kSymbolVersions[] = {\n")
        if all_results:
            for sdk, results in all_results:
                f.write(f"  {{{sdk},\n")
                for logical in PATTERNS.keys():
                    arr_name = f"kNames_{sdk}_{logical}"
                    f.write(f"   {{{arr_name}, sizeof({arr_name}) / sizeof({arr_name}[0])}},\n")
                f.write("  },\n")
        else:
            f.write("  {0,\n")
            for logical in PATTERNS.keys():
                f.write("   {nullptr, 0},\n")
            f.write("  },\n")
        f.write("};\n\n")
        f.write("static const size_t kSymbolVersionCount =\n")
        f.write("    sizeof(kSymbolVersions) / sizeof(kSymbolVersions[0]);\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        required=True,
        action="append",
        help="root directory containing version folders (repeatable)",
    )
    parser.add_argument("--out", required=True, help="output header path")
    parser.add_argument("--nm", default="nm", help="nm executable path")
    args = parser.parse_args()

    NM_PATH = args.nm
    versions = collect_versions(args.root)
    generate_header(args.out, versions)
