#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def find_readelf(ndk_home):
    candidates = []
    if ndk_home:
        candidates.append(
            os.path.join(
                ndk_home,
                "toolchains",
                "llvm",
                "prebuilt",
                "linux-x86_64",
                "bin",
                "llvm-readelf",
            )
        )
    candidates.extend(["llvm-readelf", "readelf"])
    for c in candidates:
        if os.path.isabs(c) and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
        if shutil.which(c):
            return c
    return None


def find_nm(ndk_home):
    candidates = []
    if ndk_home:
        candidates.append(
            os.path.join(
                ndk_home,
                "toolchains",
                "llvm",
                "prebuilt",
                "linux-x86_64",
                "bin",
                "llvm-nm",
            )
        )
    candidates.extend(["llvm-nm", "nm"])
    for c in candidates:
        if os.path.isabs(c) and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
        if shutil.which(c):
            return c
    return None


def run_readelf(readelf, args, path):
    cmd = [readelf] + args + [path]
    try:
        return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as exc:
        die(f"readelf failed for {path}: {exc.output}")


def check_aarch64(readelf, path):
    out = run_readelf(readelf, ["-h"], path)
    if "AArch64" not in out:
        die(f"{path} is not AArch64\n{out}")


def check_elf(readelf, path):
    out = run_readelf(readelf, ["-h"], path)
    if "ELF" not in out:
        die(f"{path} is not an ELF file\n{out}")


def check_archive(path):
    with open(path, "rb") as f:
        magic = f.read(8)
    if magic != b"!<arch>\n":
        die(f"{path} is not an archive (.a)")


def check_needed(readelf, path):
    out = run_readelf(readelf, ["-d"], path)
    for bad in ("libstdc++.so.6", "libgcc_s.so.1", "libc.so.6"):
        if bad in out:
            die(f"{path} links against host library {bad}")


def run_nm(nm, args, path):
    cmd = [nm] + args + [path]
    try:
        return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as exc:
        die(f"nm failed for {path}: {exc.output}")


def scan_symbols(nm, path):
    out = run_nm(nm, ["-D", "--defined-only"], path)
    return out


def check_lib_symbols(nm, libgui, libandroid):
    gui_syms = scan_symbols(nm, libgui)
    android_syms = scan_symbols(nm, libandroid)

    if "createSurfaceChecked" not in gui_syms:
        die(f"{libgui} missing createSurfaceChecked symbol")
    if "SurfaceControl" not in gui_syms or "getSurface" not in gui_syms:
        die(f"{libgui} missing SurfaceControl::getSurface symbol")
    if "ANativeWindow_lock" not in android_syms:
        die(f"{libandroid} missing ANativeWindow_lock symbol")
    if "ANativeWindow_unlockAndPost" not in android_syms:
        die(f"{libandroid} missing ANativeWindow_unlockAndPost symbol")


def scan_lib_dump(nm, lib_root):
    if not lib_root or not os.path.isdir(lib_root):
        return
    libgui_paths = []
    libandroid_paths = []
    for root, _, files in os.walk(lib_root):
        for name in files:
            if name == "libgui.so":
                libgui_paths.append(os.path.join(root, name))
            elif name == "libandroid.so":
                libandroid_paths.append(os.path.join(root, name))
    if not libgui_paths or not libandroid_paths:
        print("No libgui.so/libandroid.so found under lib root; skip symbol scan.")
        return

    for gui in libgui_paths:
        rel = os.path.relpath(gui, lib_root)
        base = os.path.dirname(rel)
        android_match = None
        for candidate in libandroid_paths:
            if os.path.dirname(os.path.relpath(candidate, lib_root)) == base:
                android_match = candidate
                break
        if android_match:
            check_lib_symbols(nm, gui, android_match)
        else:
            print(f"Skip {gui}: matching libandroid.so not found in same dir.")


def check_generated_header(path):
    if not os.path.isfile(path):
        die(f"Missing header: {path}")
    text = open(path, "r", encoding="utf-8", errors="ignore").read()
    if "struct SymbolVersion" not in text or "kSymbolVersions" not in text:
        die(f"generated_symbols.h missing expected content: {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dist", default="dist")
    ap.add_argument("--ndk", default="")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--lib-root", default="")
    args = ap.parse_args()

    dist = os.path.abspath(args.dist)
    if not os.path.isdir(dist):
        die(f"dist directory not found: {dist}")

    required = ["libmidraw.so", "libmidraw.a", "overlay_engine", "benchmark"]
    for name in required:
        path = os.path.join(dist, name)
        if not os.path.isfile(path) or os.path.getsize(path) == 0:
            die(f"Missing or empty artifact: {path}")

    readelf = find_readelf(args.ndk)
    if not readelf:
        die("readelf/llvm-readelf not found")
    nm = find_nm(args.ndk)
    if not nm:
        die("nm/llvm-nm not found")

    check_elf(readelf, os.path.join(dist, "overlay_engine"))
    check_elf(readelf, os.path.join(dist, "benchmark"))
    check_elf(readelf, os.path.join(dist, "libmidraw.so"))
    check_aarch64(readelf, os.path.join(dist, "overlay_engine"))
    check_aarch64(readelf, os.path.join(dist, "benchmark"))
    check_aarch64(readelf, os.path.join(dist, "libmidraw.so"))
    check_archive(os.path.join(dist, "libmidraw.a"))
    check_needed(readelf, os.path.join(dist, "overlay_engine"))
    check_needed(readelf, os.path.join(dist, "benchmark"))
    check_needed(readelf, os.path.join(dist, "libmidraw.so"))

    header_path = os.path.join(os.path.abspath(args.repo), "include", "generated_symbols.h")
    check_generated_header(header_path)
    scan_lib_dump(nm, args.lib_root)

    print("CI test checks passed.")


if __name__ == "__main__":
    main()
