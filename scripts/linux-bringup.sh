#!/usr/bin/env bash
#
# First run of RendeerAlpha on a Linux machine.
#
# Installs what is missing, builds, runs the tests, then starts the Sandbox and reports
# what happened. Everything it prints is meant to be readable on its own, so the output
# can be pasted back somewhere without the machine being present.
#
#   ./scripts/linux-bringup.sh              build and run
#   ./scripts/linux-bringup.sh --no-run     build only, do not start anything
#   ./scripts/linux-bringup.sh --jobs 1     for a machine with very little memory
#
# It asks for sudo once, near the start, and only to install packages. It writes nothing
# outside this source tree and the packages it installs.
#
# On a machine without a Vulkan-capable GPU -- which includes most integrated graphics
# from before about 2014 -- this installs Mesa's software renderer (lavapipe) and uses
# it. That is slow, and entirely adequate for an interface made of rectangles and text.

set -u

# ---- options -----------------------------------------------------------------------

RUN_APP=1
JOBS=""
for arg in "$@"; do
	case "$arg" in
		--no-run) RUN_APP=0 ;;
		--jobs)   ;;                       # value is read below
		[0-9]*)   JOBS="$arg" ;;
		--help|-h)
			sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
			exit 0 ;;
	esac
done

cd "$(dirname "$0")/.." || exit 1
SOURCE_DIR="$PWD"

# An old machine with two cores and four gigabytes will thrash if every core compiles a
# translation unit that pulls in vulkan.hpp. Two is a safe default; --jobs overrides it.
if [ -z "$JOBS" ]; then
	CORES="$(nproc 2>/dev/null || echo 2)"
	JOBS=$(( CORES > 2 ? 2 : CORES ))
fi

say() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
note() { printf '   %s\n' "$1"; }

# ---- 1. the machine ------------------------------------------------------------------

say "Machine"
note "$(uname -srm)"
note "$( (grep -m1 'model name' /proc/cpuinfo 2>/dev/null || echo 'cpu: unknown') | sed 's/.*: //')"
note "cores: $(nproc 2>/dev/null || echo '?')   building with -j$JOBS"
note "memory: $(free -h 2>/dev/null | awk '/^Mem:/{print $2" total, "$7" available"}')"
note "source: $SOURCE_DIR"
case "$SOURCE_DIR" in
	/mnt/*)
		note ""
		note "WARNING: this tree is on a mounted Windows drive. Builds there are very"
		note "slow and fill the Linux page cache. Copy it to your home directory first."
		;;
esac

if [ ! -f CMakeLists.txt ]; then
	note "This does not look like the RendeerAlpha source tree. Stopping."
	exit 1
fi

# ---- 2. packages ---------------------------------------------------------------------

say "Dependencies"

NEEDED=""
have() { command -v "$1" >/dev/null 2>&1; }
want_pkg() { dpkg -s "$1" >/dev/null 2>&1 || NEEDED="$NEEDED $1"; }

have cmake  || NEEDED="$NEEDED cmake"
have g++    || NEEDED="$NEEDED build-essential"
have ninja  || NEEDED="$NEEDED ninja-build"
want_pkg pkg-config
want_pkg libvulkan-dev
want_pkg libglfw3-dev
want_pkg libglm-dev
want_pkg libshaderc-dev
# The engine turns validation on in a Debug build, and instance creation fails outright
# if the layer is not installed.
want_pkg vulkan-validationlayers
# lavapipe, the software Vulkan driver, plus vulkaninfo to see what was found.
want_pkg mesa-vulkan-drivers
want_pkg vulkan-tools

if [ -n "$NEEDED" ]; then
	note "installing:$NEEDED"
	note "(sudo is needed for this, and only for this)"
	sudo apt-get update -qq || { note "apt update failed"; exit 1; }
	# shellcheck disable=SC2086
	sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $NEEDED \
		|| { note "apt install failed"; exit 1; }
else
	note "everything already present"
fi

for t in cmake ninja g++; do
	have "$t" && note "$(printf '%-8s %s' "$t" "$("$t" --version 2>&1 | head -1)")"
done

# ---- 3. esbuild, if node is here ------------------------------------------------------

say "Layout toolchain"

COMPILE_LAYOUTS=OFF
ESBUILD=""
if have node && have npm; then
	TOOLS="$HOME/.rda-tools"
	if [ ! -x "$TOOLS/node_modules/.bin/esbuild" ]; then
		note "installing esbuild into $TOOLS"
		mkdir -p "$TOOLS"
		( cd "$TOOLS" && npm install --no-audit --no-fund --silent esbuild@0.28.2 ) >/dev/null 2>&1
	fi
	if [ -x "$TOOLS/node_modules/.bin/esbuild" ]; then
		ESBUILD="$TOOLS/node_modules/.bin/esbuild"
		export RDA_ESBUILD="$ESBUILD"
		COMPILE_LAYOUTS=ON
		note "esbuild $("$ESBUILD" --version 2>&1) at $ESBUILD"
		note "layouts will be recompiled from their .tsx"
	fi
fi
if [ "$COMPILE_LAYOUTS" = "OFF" ]; then
	note "no node/esbuild here, so layouts are not recompiled."
	note "The blueprints committed to the tree are used instead, which is enough to run."
fi

# ---- 4. build --------------------------------------------------------------------------

say "Build"
BUILD_DIR="$SOURCE_DIR/build/linux"
LOG="$BUILD_DIR/bringup.log"
mkdir -p "$BUILD_DIR"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE=Debug \
	-DRDA_COMPILE_LAYOUTS=$COMPILE_LAYOUTS \
	> "$LOG" 2>&1
if [ $? -ne 0 ]; then
	note "configure failed:"
	tail -30 "$LOG" | sed 's/^/   | /'
	exit 1
fi
grep -E "^-- (Vulkan|GLFW|shaderc|glm|Layouts|State|Tests)" "$LOG" | sed 's/^-- /   /'

note "compiling with -j$JOBS (this takes a while on an older machine)"
START=$(date +%s)
cmake --build "$BUILD_DIR" -j "$JOBS" >> "$LOG" 2>&1
BUILD_OK=$?
ELAPSED=$(( $(date +%s) - START ))

if [ $BUILD_OK -ne 0 ]; then
	note "build FAILED after ${ELAPSED}s. The errors, with vendored code filtered out:"
	grep -E "error:|FAILED" "$LOG" | grep -v "/vendor/" | head -25 | sed 's/^/   | /'
	note ""
	note "full log: $LOG"
	exit 1
fi
note "built in ${ELAPSED}s"

OURS=$(grep "warning:" "$LOG" | grep -v "/vendor/" | wc -l)
note "warnings in our own code: $OURS"
[ "$OURS" -gt 0 ] && grep "warning:" "$LOG" | grep -v "/vendor/" | head -10 | sed 's/^/   | /'

note "artifacts:"
ls -1 "$BUILD_DIR/bin/Debug" 2>/dev/null | sed 's/^/   | /'

# ---- 5. tests ---------------------------------------------------------------------------

say "Tests"
if [ -x "$BUILD_DIR/bin/Debug/rendeer_tests" ]; then
	( cd "$BUILD_DIR/bin/Debug" && ./rendeer_tests 2>&1 | tail -3 ) | sed 's/^/   /'
else
	note "no test binary was produced"
fi

# ---- 6. what Vulkan is available ---------------------------------------------------------

say "Vulkan"
LAVAPIPE=""
for candidate in /usr/share/vulkan/icd.d/lvp_icd*.json; do
	[ -f "$candidate" ] && LAVAPIPE="$candidate"
done

if have vulkaninfo; then
	DEVICES=$(vulkaninfo --summary 2>/dev/null | grep -A2 "deviceName" | head -6)
	if [ -n "$DEVICES" ]; then
		echo "$DEVICES" | sed 's/^/   /'
	else
		note "vulkaninfo found no device with the default driver"
	fi
fi
if [ -n "$LAVAPIPE" ]; then
	note "software driver available: $LAVAPIPE"
else
	note "lavapipe was not found; the run below uses whatever driver is installed"
fi

# ---- 7. run -----------------------------------------------------------------------------

if [ "$RUN_APP" -eq 0 ]; then
	say "Done (build only, as asked)"
	exit 0
fi

say "Run"
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
	note "no DISPLAY or WAYLAND_DISPLAY, so there is no screen to open a window on."
	note "Run this from the desktop session rather than over SSH, or use --no-run."
	exit 0
fi

RUN_DIR="$BUILD_DIR/bin/Debug"
if [ ! -x "$RUN_DIR/sandbox" ]; then
	note "no sandbox binary to run"
	exit 1
fi

# Assets resolve relative to the working directory, so it has to start from there.
cd "$RUN_DIR" || exit 1
rm -f RDA_DEBUG.txt

# Prefer the software driver when there is one: an old integrated GPU often advertises
# Vulkan and then fails at something basic, and a clean software run is a better answer
# than a confusing hardware one. Both variable names are set because the loader renamed
# it and which one is honoured depends on the version.
if [ -n "$LAVAPIPE" ]; then
	export VK_ICD_FILENAMES="$LAVAPIPE"
	export VK_DRIVER_FILES="$LAVAPIPE"
	note "using the software driver"
fi

note "starting the Sandbox for 20 seconds"
timeout 20 ./sandbox >/dev/null 2>&1
EXIT=$?
case $EXIT in
	124) note "it was still running after 20s, which is what a working window does" ;;
	0)   note "it exited on its own" ;;
	*)   note "it exited with code $EXIT" ;;
esac

say "Report"
if [ -f RDA_DEBUG.txt ]; then
	note "window created:   $(grep -c 'Window Created' RDA_DEBUG.txt)"
	note "GUI renderer:     $(grep -c 'GUI renderer initialized' RDA_DEBUG.txt)"
	note "font atlas baked: $(grep -c 'Font atlas baked' RDA_DEBUG.txt)"
	note "layout loaded:    $(grep -c 'interface loaded' RDA_DEBUG.txt)"
	note "validation (VUID):$(grep -c 'VUID-' RDA_DEBUG.txt)"
	note "errors/warnings:  $(grep -cE '^\[ERROR\]|^\[WARNING\]' RDA_DEBUG.txt)"
	note ""
	note "the interesting lines:"
	grep -E 'SUCCESS|ERROR|WARNING|sandbox:|layout:' RDA_DEBUG.txt | head -25 | sed 's/^/   | /'
else
	note "no RDA_DEBUG.txt was written, which means the engine stopped before logging"
	note "anything -- usually a Vulkan instance or device failure."
fi

note ""
note "full build log: $LOG"
note "run log:        $RUN_DIR/RDA_DEBUG.txt"
