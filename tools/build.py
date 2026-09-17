#!/usr/bin/env python3
"""Scons entry point for the Godot 3.6 renderer branch.

This branch is renderer-only: it builds the `the_gates` module into a
Godot 3.6 engine so gates declaring `godot_version = "3.6"` have a binary to
run. The Godot 4 launcher and current renderer live on `tg-4.5` and keep using
that branch's `tools/build.py`.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

GODOT_DIR = Path(__file__).resolve().parent.parent

GODOT_VERSION = "3.6"

PROFILES: dict[str, list[str]] = {
    "renderer": [
        "target=release_debug",
        "tools=no",
        "debug_symbols=yes",
        # libzmq needs exceptions; Godot disables them by default.
        "disable_exceptions=no",
        "tg_renderer=yes",
    ],
    "renderer-release": [
        "target=release",
        "tools=no",
        "lto=full",
        "disable_exceptions=no",
        "tg_renderer=yes",
    ],
    # FRT's SDL2 backend is the Wayland display path. The launcher asks for
    # --display-driver wayland on a Wayland session for sandbox isolation; the
    # x11 build cannot honour that and falls back to XWayland.
    "renderer-wayland": [
        "platform=frt",
        "target=release_debug",
        "tools=no",
        "debug_symbols=yes",
        "disable_exceptions=no",
        "tg_renderer=yes",
        "frt_desktop_gl=yes",
    ],
    "renderer-wayland-release": [
        "platform=frt",
        "target=release",
        "tools=no",
        "disable_exceptions=no",
        "tg_renderer=yes",
        "frt_desktop_gl=yes",
    ],
}

# Nothing a gate renders needs these, and dropping them takes a few minutes off
# a cold build. Add back with `-- module_<name>_enabled=yes` if a gate needs one.
DISABLED_MODULES = [
    "module_mono_enabled=no",
    "module_webm_enabled=no",
    "module_mobile_vr_enabled=no",
]


def default_jobs() -> int:
    cpu = os.cpu_count() or 4
    return max(1, cpu - 2)


# The file name the launcher's renderer_executable.tres looks up per platform.
STAGED_NAMES = {
    "x11": "Renderer-godot_v%s.x86_64" % GODOT_VERSION,
    "frt": "Renderer-godot_v%s.x86_64" % GODOT_VERSION,
    "osx": "Renderer-godot_v%s.universal" % GODOT_VERSION,
    "windows": "Renderer-godot_v%s.exe" % GODOT_VERSION,
}


def default_platform() -> str:
    return {"darwin": "osx", "win32": "windows"}.get(sys.platform, "x11")


def built_binary(profile: str, platform: str) -> Path | None:
    debug = "target=release" not in PROFILES[profile]
    prefix = "godot.%s.%s." % (platform, "opt.debug" if debug else "opt")
    # The arch suffix differs per platform: .64 on x11, .arm64 or .x86_64 on osx, .64.exe on windows.
    binaries = [p for p in (GODOT_DIR / "bin").glob(prefix + "*") if not p.name[len(prefix) :].startswith("debug")]
    return max(binaries, key=lambda p: p.stat().st_mtime, default=None)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("profile", choices=sorted(PROFILES), nargs="?", default="renderer")
    parser.add_argument("-j", "--jobs", type=int, default=default_jobs())
    parser.add_argument("--platform", choices=sorted(STAGED_NAMES), default=default_platform())
    parser.add_argument("--dry-run", action="store_true", help="print the scons command and exit")
    parser.add_argument("--stage-to", type=Path, help="copy the built binary here under the launcher's renderer name")
    parser.add_argument("extra", nargs="*", help="extra scons args (after --)")
    args = parser.parse_args()

    if not (GODOT_DIR / "SConstruct").is_file():
        print("tools/build.py must live in the engine checkout", file=sys.stderr)
        return 1

    profile_args = PROFILES[args.profile]
    # A profile may pin its own platform (the Wayland profiles need frt).
    profile_platform = next((a.split("=", 1)[1] for a in profile_args if a.startswith("platform=")), args.platform)

    cmd = ["scons", "-j", str(args.jobs)]
    if not any(a.startswith("platform=") for a in profile_args):
        cmd.append("platform=%s" % args.platform)
    cmd += profile_args
    cmd += DISABLED_MODULES
    cmd += args.extra

    print("+ cd %s && %s" % (GODOT_DIR, " ".join(cmd)))
    if args.dry_run:
        return 0

    result = subprocess.run(cmd, cwd=GODOT_DIR)
    if result.returncode != 0:
        return result.returncode

    binary = built_binary(args.profile, profile_platform)
    if binary is None:
        print("build reported success but no %s binary is in %s" % (profile_platform, GODOT_DIR / "bin"), file=sys.stderr)
        return 1
    print("built %s" % binary)

    if args.stage_to:
        args.stage_to.mkdir(parents=True, exist_ok=True)
        target = args.stage_to / STAGED_NAMES[profile_platform]
        shutil.copy2(binary, target)
        os.chmod(target, 0o755)
        print("staged %s" % target)

    return 0


if __name__ == "__main__":
    sys.exit(main())
