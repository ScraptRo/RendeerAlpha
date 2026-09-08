#!/usr/bin/env bash
#
# First run of RendeerAlpha on a Linux machine.
#
# Installs what is missing, builds the engine, runs its tests, and lays the result out
# in bin/ at the root of this checkout -- the one folder a Python, Node, C# or any other
# non-C++ backend points at. Then it registers the Python package with your interpreter
# and installs the Node package's one dependency, so `import rda` and `import 'rda'`
# work from a project without building anything in it.
#
# It builds no application: this repository is the engine alone, and how to write an
# application against it is docs/setup/first-interface/.
#
# Everything it prints is meant to be readable on its own, so the output can be pasted
# back somewhere without the machine being present.
#
#   ./scripts/linux-bringup.sh              install what is missing, build, test, stage
#   ./scripts/linux-bringup.sh --jobs 1     for a machine with very little memory
#   ./scripts/linux-bringup.sh --release    stage a Release engine instead of Debug
#
# It asks for sudo once, near the start, and only to install packages. Do not run the
# script itself with sudo: everything it writes would then belong to root, and a window
# opened as root under your desktop session is refused by the X server.
#
# On a machine without a Vulkan-capable GPU -- which includes most integrated graphics
# from before about 2014 -- this installs Mesa's software renderer (lavapipe) so that an
# application has a device. That is slow, and entirely adequate for an interface made of
# rectangles and text.

set -u

# ---- options -----------------------------------------------------------------------

JOBS=""
CONFIG="Debug"
for arg in "$@"; do
	case "$arg" in
		--jobs)     ;;                       # value is read below
		[0-9]*)     JOBS="$arg" ;;
		--release)  CONFIG="Release" ;;
		--help|-h)
			sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'
			exit 0 ;;
	esac
done

cd "$(dirname "$0")/.." || exit 1
SOURCE_DIR="$PWD"
STAGE_DIR="$SOURCE_DIR/bin"

say() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
note() { printf '   %s\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---- 0. not as root ----------------------------------------------------------------
#
# The most common way this goes wrong, so it is checked before anything is written.
# Once one build directory belongs to root, every later build needs sudo too, and a
# root-owned window is refused by a user-owned X server with "BadAccess".
if [ "$(id -u)" -eq 0 ]; then
	say "Stop"
	note "This is running as root. Run it as yourself; it asks for sudo when it needs it."
	note "If an earlier run already left root-owned files here, reclaim them first:"
	note "   sudo chown -R \$USER:\$USER \"$SOURCE_DIR\""
	exit 1
fi

# An old machine with two cores and four gigabytes will thrash if every core compiles a
# translation unit that pulls in vulkan.hpp. Two is a safe default; --jobs overrides it.
if [ -z "$JOBS" ]; then
	CORES="$(nproc 2>/dev/null || echo 2)"
	JOBS=$(( CORES > 2 ? 2 : CORES ))
fi

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
ROOT_OWNED="$(find "$SOURCE_DIR" -maxdepth 2 -user root 2>/dev/null | head -1)"
if [ -n "$ROOT_OWNED" ]; then
	note ""
	note "WARNING: $ROOT_OWNED belongs to root -- an earlier run used sudo. This run"
	note "will fail to write there. Reclaim the tree first:"
	note "   sudo chown -R \$USER:\$USER \"$SOURCE_DIR\""
	exit 1
fi

# ---- 2. packages ---------------------------------------------------------------------

say "Dependencies"

# Two lists. NEEDED is what the build cannot do without; WANTED is what a Debug engine
# likes to have at run time and can run without -- the validation layer (it warns and
# goes on), lavapipe for a machine with no usable GPU, and vulkaninfo to see what the
# loader found. Missing NEEDED stops this script; missing WANTED is installed when
# sudo is available and reported when it is not.
NEEDED=""
WANTED=""
need_pkg() { dpkg -s "$1" >/dev/null 2>&1 || NEEDED="$NEEDED $1"; }
want_pkg() { dpkg -s "$1" >/dev/null 2>&1 || WANTED="$WANTED $1"; }

have cmake  || NEEDED="$NEEDED cmake"
have g++    || NEEDED="$NEEDED build-essential"
have ninja  || NEEDED="$NEEDED ninja-build"
have curl   || NEEDED="$NEEDED curl"
need_pkg pkg-config
need_pkg libvulkan-dev
need_pkg libglfw3-dev
need_pkg libglm-dev
need_pkg libshaderc-dev
want_pkg vulkan-validationlayers
want_pkg mesa-vulkan-drivers
want_pkg vulkan-tools

if [ -n "$NEEDED$WANTED" ]; then
	if ! have apt-get; then
		note "missing:$NEEDED$WANTED"
		note "This is not a Debian or Ubuntu machine, so they are not installed here."
		note "Install the same things with your package manager -- the names above are"
		note "Debian's -- then run this again."
		[ -n "$NEEDED" ] && exit 1
	elif [ -t 0 ] || sudo -n true 2>/dev/null; then
		note "installing:$NEEDED$WANTED"
		note "(sudo is needed for this, and only for this)"
		sudo apt-get update -qq || { note "apt update failed"; exit 1; }
		# shellcheck disable=SC2086
		sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $NEEDED $WANTED \
			|| { note "apt install failed"; exit 1; }
	else
		# No terminal to ask for a password on, and no password-less sudo.
		note "missing:$NEEDED$WANTED"
		note "sudo cannot ask for a password here. Install them yourself:"
		note "   sudo apt install$NEEDED$WANTED"
		if [ -n "$NEEDED" ]; then
			note "then run this again."
			exit 1
		fi
		note "The build does not need them; a Debug engine will warn about the missing"
		note "validation layer and run without it."
	fi
else
	note "everything already present"
fi

for t in cmake ninja g++; do
	have "$t" && note "$(printf '%-8s %s' "$t" "$("$t" --version 2>&1 | head -1)")"
done

# ---- 3. esbuild --------------------------------------------------------------------------
#
# The toolchain transforms TypeScript with esbuild, so a project's interface cannot be
# compiled without one. It is a single static binary; npm is one way to get it and not
# the only one. It goes into bin/ beside the tool, which is where the tool looks first --
# so a Python project with no node_modules anywhere still compiles its layouts.

say "esbuild"
ESBUILD_VERSION="0.28.2"
mkdir -p "$STAGE_DIR"
ESBUILD="$STAGE_DIR/esbuild"

if [ -x "$ESBUILD" ] && "$ESBUILD" --version >/dev/null 2>&1; then
	note "already in bin/: esbuild $("$ESBUILD" --version 2>&1)"
elif [ -x "$SOURCE_DIR/node_modules/@esbuild/linux-x64/bin/esbuild" ]; then
	cp "$SOURCE_DIR/node_modules/@esbuild/linux-x64/bin/esbuild" "$ESBUILD"
	note "copied from this checkout's node_modules: esbuild $("$ESBUILD" --version 2>&1)"
else
	ARCH="$(uname -m)"
	case "$ARCH" in
		x86_64)  PLATFORM="linux-x64" ;;
		aarch64) PLATFORM="linux-arm64" ;;
		*)       PLATFORM="" ;;
	esac
	if [ -z "$PLATFORM" ]; then
		note "no prebuilt esbuild for $ARCH; install node and run 'npm install' here, then re-run"
	else
		TARBALL="https://registry.npmjs.org/@esbuild/$PLATFORM/-/$PLATFORM-$ESBUILD_VERSION.tgz"
		note "downloading esbuild $ESBUILD_VERSION for $PLATFORM"
		TMP="$(mktemp -d)"
		if curl -fsSL "$TARBALL" -o "$TMP/esbuild.tgz" && tar -xzf "$TMP/esbuild.tgz" -C "$TMP"; then
			cp "$TMP/package/bin/esbuild" "$ESBUILD" && chmod +x "$ESBUILD"
			note "esbuild $("$ESBUILD" --version 2>&1) -> bin/esbuild"
		else
			note "download failed. Without esbuild the engine still builds, but no"
			note "application's interface can be compiled. Install node, run 'npm install'"
			note "in this checkout, and run this again."
		fi
		rm -rf "$TMP"
	fi
fi
[ -x "$ESBUILD" ] || ESBUILD=""

# ---- 4. build --------------------------------------------------------------------------

say "Build ($CONFIG)"
BUILD_DIR="$SOURCE_DIR/build/linux"
LOG="$BUILD_DIR/bringup.log"
mkdir -p "$BUILD_DIR"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE="$CONFIG" \
	-DRDA_ESBUILD="$ESBUILD" \
	> "$LOG" 2>&1
if [ $? -ne 0 ]; then
	note "configure failed:"
	tail -30 "$LOG" | sed 's/^/   | /'
	note ""
	note "The usual causes: a package above did not install, or cmake is older than 3.21"
	note "(cmake --version). Ubuntu 22.04 and later are fine."
	exit 1
fi
grep -E "^-- (Vulkan|GLFW|shaderc|glm|esbuild|Binding|Layouts|State|Tests)" "$LOG" | sed 's/^-- /   /'

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
	note "If the first error names a header that is missing, the package that provides"
	note "it is in the list at the top of this script; 'apt-file search <header>' names it."
	exit 1
fi
note "built in ${ELAPSED}s"

OURS=$(grep "warning:" "$LOG" | grep -v "/vendor/" | wc -l)
note "warnings in our own code: $OURS"
[ "$OURS" -gt 0 ] && grep "warning:" "$LOG" | grep -v "/vendor/" | head -10 | sed 's/^/   | /'

# ---- 5. tests ---------------------------------------------------------------------------

say "Tests"
if [ -x "$BUILD_DIR/bin/$CONFIG/rendeer_tests" ]; then
	( cd "$BUILD_DIR/bin/$CONFIG" && ./rendeer_tests 2>&1 | tail -3 ) | sed 's/^/   /'
else
	note "no test binary was produced"
fi

# ---- 6. stage: bin/ -------------------------------------------------------------------------

say "Staging into bin/"
cmake --build "$BUILD_DIR" --target stage >> "$LOG" 2>&1 || { note "staging failed; see $LOG"; exit 1; }
ls -1 "$STAGE_DIR" | sed 's/^/   | /'
note "$(cat "$STAGE_DIR/BUILD.txt")"

# ---- 7. the languages ----------------------------------------------------------------------

say "Python"
PYTHON=""
for candidate in python3 python; do
	if have "$candidate" && "$candidate" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' 2>/dev/null; then
		PYTHON="$candidate"; break
	fi
done
if [ -n "$PYTHON" ]; then
	note "$("$PYTHON" --version 2>&1) at $(command -v "$PYTHON")"
	"$PYTHON" "$SOURCE_DIR/bindings/python/register.py" 2>&1 | sed 's/^/   /'
	if "$PYTHON" -c 'import rda' 2>/dev/null; then
		note "import rda: ok"
		note "In a virtual environment: python $SOURCE_DIR/bindings/python/register.py"
	else
		note "import rda: FAILED -- see the lines above"
	fi
else
	note "no python3 (3.8 or newer) on PATH; skipped. Install python3 and re-run to register the package."
fi

say "Node"
if have node && have npm; then
	note "node $(node --version), npm $(npm --version)"
	# koffi is the FFI the binding needs, and it must live inside the package: npm links
	# a path dependency rather than copying it, so `require('koffi')` resolves from here.
	if ( cd "$SOURCE_DIR/bindings/node" && npm install --omit=dev --no-audit --no-fund --silent ) >> "$LOG" 2>&1; then
		note "koffi installed into bindings/node"
		note "In a project: npm install $SOURCE_DIR/bindings/node"
	else
		note "npm install in bindings/node failed; see $LOG"
	fi
else
	note "no node on PATH; skipped. A Node backend needs node 18 or newer."
fi

say "C# and everything else"
if have dotnet; then
	note "dotnet $(dotnet --version 2>/dev/null)"
else
	note "no dotnet on PATH (only needed for a C# backend)"
fi
note "bin/ holds the shared library, the tool and include/RendeerC.h; bin/README.md says how each language reaches it."

# ---- 8. what Vulkan is available ---------------------------------------------------------

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
	note "lavapipe was not found; whatever driver is installed will be used"
fi

# ---- 9. done ----------------------------------------------------------------------------

say "Done"
note "the engine is built, tested, and staged in $STAGE_DIR ($CONFIG)."
note "ctest --test-dir $BUILD_DIR runs the binding tests as well; those open a window."
note "To see an interface, write one: docs/setup/first-interface/README.md"
note ""
note "full build log: $LOG"
