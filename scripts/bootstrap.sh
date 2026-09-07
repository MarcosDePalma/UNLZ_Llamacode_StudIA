#!/usr/bin/env bash
# LlamaCode zero-to-running bootstrap for Linux.
#
# Installs every dependency (git, cmake, ninja, g++, Python, and Qt 6.8.3 via
# aqtinstall), clones the repo into an isolated folder, builds and launches.
#
#   curl -fsSL https://raw.githubusercontent.com/cristianlukas/UNLZ_Llamacode/main/scripts/bootstrap.sh | bash
#
# Qt is installed via aqtinstall (not distro packages) because the code needs
# Qt >= 6.5 (QQmlApplicationEngine::loadFromModule) and most LTS distros ship an
# older Qt (e.g. Ubuntu 24.04 = 6.4.2). aqt guarantees a consistent 6.8.3.
#
# Override defaults via env vars:
#   LC_DIR=/path/to/install   (default: $HOME/LlamaCode)
#   LC_BRANCH=main
#   LC_CONFIG=Debug           (Debug default; Release is the stable channel)
#   LC_QTVER=6.8.3
#   LC_QTROOT=$HOME/Qt
#   LC_NORUN=1                (skip launching)

set -euo pipefail

REPO="https://github.com/cristianlukas/UNLZ_Llamacode.git"
DIR="${LC_DIR:-$HOME/LlamaCode}"
BRANCH="${LC_BRANCH:-main}"
CONFIG="${LC_CONFIG:-Debug}"
QTVER="${LC_QTVER:-6.8.3}"
QTROOT="${LC_QTROOT:-$HOME/Qt}"

c_info() { printf '\033[36m[*] %s\033[0m\n' "$*"; }
c_ok()   { printf '\033[32m[OK] %s\033[0m\n' "$*"; }
c_die()  { printf '\033[31m[ERROR] %s\033[0m\n' "$*" >&2; exit 1; }

echo ""
printf '\033[35m=== LlamaCode bootstrap (Linux) ===\033[0m\n'
echo "Target: $DIR  branch=$BRANCH  config=$CONFIG  Qt=$QTVER"
echo ""

# ── Privilege escalation helper ──────────────────────────────────────────────
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then SUDO="sudo"; else
        c_die "Need root or sudo to install system packages."
    fi
fi

# ── Toolchain + Qt runtime/link libraries (distro packages) ──────────────────
# We install only the C++ toolchain and the system libraries that the aqt-built
# Qt links against at build/run time -- NOT distro Qt itself.
# libsecret (-dev) is needed by QtKeychain (Secret Service backend) to encrypt the
# cloud API keys at rest. Without it the QtKeychain FetchContent build fails; you can
# also skip it by configuring CMake with -DLC_USE_QTKEYCHAIN=OFF (file fallback).
install_deps() {
    if command -v apt-get >/dev/null 2>&1; then
        c_info "Installing toolchain + Qt runtime libs via apt..."
        $SUDO apt-get update -y
        $SUDO apt-get install -y --no-install-recommends \
            git curl ca-certificates cmake ninja-build build-essential pkg-config \
            python3 python3-pip python3-venv \
            libglib2.0-0 libgl1-mesa-dev libegl1 libxkbcommon0 libxkbcommon-x11-0 \
            libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
            libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 \
            libfontconfig1 libfreetype6 libdbus-1-3 \
            libsecret-1-dev
    elif command -v dnf >/dev/null 2>&1; then
        c_info "Installing toolchain + Qt runtime libs via dnf..."
        $SUDO dnf install -y \
            git curl cmake ninja-build gcc-c++ make pkgconf-pkg-config \
            python3 python3-pip \
            glib2 mesa-libGL-devel mesa-libEGL libxkbcommon libxkbcommon-x11 \
            xcb-util-cursor fontconfig freetype dbus-libs libsecret-devel
    elif command -v pacman >/dev/null 2>&1; then
        c_info "Installing toolchain + Qt runtime libs via pacman..."
        $SUDO pacman -Sy --needed --noconfirm \
            git curl cmake ninja base-devel python python-pip \
            glib2 mesa libxkbcommon libxkbcommon-x11 xcb-util-cursor fontconfig freetype2 dbus libsecret
    elif command -v zypper >/dev/null 2>&1; then
        c_info "Installing toolchain + Qt runtime libs via zypper..."
        $SUDO zypper install -y \
            git curl cmake ninja gcc-c++ pkg-config python3 python3-pip \
            libglib-2_0-0 Mesa-libGL-devel libxkbcommon0 libxkbcommon-x11-0 \
            xcb-util-cursor0 fontconfig freetype2 libdbus-1-3 libsecret-devel
    else
        c_die "Unsupported distro: install git, cmake, ninja, g++, python3 and Qt6 runtime libs manually."
    fi
}
install_deps

command -v cmake  >/dev/null 2>&1 || c_die "cmake not found after install."
command -v git    >/dev/null 2>&1 || c_die "git not found after install."
command -v ninja  >/dev/null 2>&1 || c_die "ninja not found after install."
c_ok "toolchain"

# ── Qt 6.8.3 via aqtinstall ──────────────────────────────────────────────────
QTDIR="$QTROOT/$QTVER/gcc_64"
if [ ! -f "$QTDIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    c_info "Installing Qt $QTVER (linux gcc_64) via aqtinstall..."
    AQT="aqt"
    if ! command -v aqt >/dev/null 2>&1; then
        # Isolated venv so we never fight system pip / PEP 668.
        python3 -m venv "$HOME/.lc-aqt-venv"
        # shellcheck disable=SC1091
        . "$HOME/.lc-aqt-venv/bin/activate"
        pip install --upgrade pip >/dev/null
        pip install aqtinstall >/dev/null
        AQT="$HOME/.lc-aqt-venv/bin/aqt"
    fi
    "$AQT" install-qt linux desktop "$QTVER" linux_gcc_64 -O "$QTROOT"
fi
[ -f "$QTDIR/lib/cmake/Qt6/Qt6Config.cmake" ] || c_die "Qt6 not found at $QTDIR after install."
c_ok "Qt6: $QTDIR"

# ── Clone / update ───────────────────────────────────────────────────────────
if [ -d "$DIR/.git" ]; then
    c_info "Repo exists -- pulling latest..."
    git -C "$DIR" fetch --depth 1 origin "$BRANCH"
    git -C "$DIR" checkout "$BRANCH"
    git -C "$DIR" reset --hard "origin/$BRANCH"
else
    c_info "Cloning into $DIR ..."
    git clone --depth 1 --branch "$BRANCH" "$REPO" "$DIR"
fi
c_ok "source ready"

# ── Build ────────────────────────────────────────────────────────────────────
BUILD_DIR="$DIR/build"
c_info "Configuring..."
cmake -S "$DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_PREFIX_PATH="$QTDIR"

c_info "Building..."
cmake --build "$BUILD_DIR" -j "$(nproc)"

EXE="$BUILD_DIR/LlamaCode"
[ -x "$EXE" ] || c_die "Built binary not found at $EXE"

echo ""
c_ok "Done. Binary: $EXE"
echo ""

if [ -z "${LC_NORUN:-}" ]; then
    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
        c_info "Launching LlamaCode..."
        ( cd "$BUILD_DIR" && LD_LIBRARY_PATH="$QTDIR/lib:${LD_LIBRARY_PATH:-}" "$EXE" & )
    else
        c_info "No display detected. Run later with:"
        echo "  LD_LIBRARY_PATH=$QTDIR/lib $EXE"
    fi
fi
