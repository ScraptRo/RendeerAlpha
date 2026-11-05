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
#   ./scripts/linux-bringup.sh --check      show what is missing and what would be installed, then stop
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
# --check reads the machine and the package database, prints what it found, and exits
# with 0 when nothing NEEDED is missing. Nothing is installed and nothing is written, so
# it is safe to run on a machine nobody has set up yet, and it is how this script is
# tested.
CHECK_ONLY=0
for arg in "$@"; do
	case "$arg" in
		--jobs)     ;;                       # value is read below
		[0-9]*)     JOBS="$arg" ;;
		--release)  CONFIG="Release" ;;
		--check)    CHECK_ONLY=1 ;;
		--help|-h)
			sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
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
note "system: $(. /etc/os-release 2>/dev/null; printf '%s' "${PRETTY_NAME:-unknown}")"
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

# Each dependency is described by what it is -- "the Vulkan loader and headers" -- and the
# table below gives the name each package manager knows it by. The names differ on every
# distribution and the purpose does not, so the purpose is what is printed when something
# is missing, and what a reader can look up on any system.
#
# Columns: key, class, probe, purpose, then one name per manager. NEEDED is what the build
# cannot do without; WANTED is what a Debug engine likes at run time and can run without
# (the validation layers, lavapipe, and vulkaninfo). The probe is a command that counts as
# the dependency being present when it is on PATH, so a cmake or compiler installed by hand,
# or from pip, is used rather than a second copy installed beside it. "-" means only the
# package database answers. A name of "-" means this table has no name for that manager.
#
# The package manager is found by the command it provides, not by the name of the
# distribution: Manjaro and Omarchy are pacman systems, and Linux Mint is apt. A machine
# has one, so the first one found is the one.
PM=""
for candidate in apt-get:apt pacman:pacman dnf:dnf zypper:zypper apk:apk xbps-install:xbps; do
	if have "${candidate%%:*}"; then PM="${candidate##*:}"; break; fi
done
if [ -n "$PM" ]; then
	note "package manager: $PM"
else
	note "no supported package manager found (apt-get, pacman, dnf, zypper, apk, xbps-install)"
fi

case "$PM" in
	apt)    FILE_HINT="'apt-file search <header>'" ;;
	pacman) FILE_HINT="'pacman -F <header>' (after 'sudo pacman -Fy')" ;;
	dnf)    FILE_HINT="'dnf provides \"*/<header>\"'" ;;
	zypper) FILE_HINT="'zypper wp <header>'" ;;
	apk)    FILE_HINT="the file search on pkgs.alpinelinux.org" ;;
	xbps)   FILE_HINT="'xlocate <header>' (from the xtools package)" ;;
	*)      FILE_HINT="your distribution's package search" ;;
esac

# Each manager answers "is this installed" in its own way. Only the exit status is used.
# stdin is closed because the loop below reads the table from a here-document, and any
# command that read stdin would take the rest of the table with it.
pkg_installed() {
	case "$PM" in
		apt)        dpkg -s "$1" </dev/null >/dev/null 2>&1 ;;
		pacman)     pacman -Qi "$1" </dev/null >/dev/null 2>&1 ;;
		dnf|zypper) rpm -q "$1" </dev/null >/dev/null 2>&1 ;;
		apk)        apk info -e "$1" </dev/null >/dev/null 2>&1 ;;
		xbps)       xbps-query "$1" </dev/null >/dev/null 2>&1 ;;
		*)          return 1 ;;
	esac
}

# The non-interactive install for this manager goes in INSTALL_ARGV, and the index refresh
# that must come before it in REFRESH_ARGV. pacman has no refresh here on purpose: its
# sync is always a full upgrade, and a sync on its own is a partial upgrade, which Arch
# does not support. A stale pacman database is reported further down rather than fixed.
pm_install_argv() {
	case "$PM" in
		apt)    INSTALL_ARGV=(env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "$@") ;;
		pacman) INSTALL_ARGV=(pacman -S --needed --noconfirm "$@") ;;
		dnf)    INSTALL_ARGV=(dnf install -y --setopt=install_weak_deps=False "$@") ;;
		zypper) INSTALL_ARGV=(zypper --non-interactive install --no-recommends "$@") ;;
		apk)    INSTALL_ARGV=(apk add --no-cache "$@") ;;
		xbps)   INSTALL_ARGV=(xbps-install -Sy "$@") ;;
		*)      INSTALL_ARGV=() ;;
	esac
}

pm_refresh_argv() {
	case "$PM" in
		apt)    REFRESH_ARGV=(apt-get update -qq) ;;
		zypper) REFRESH_ARGV=(zypper --non-interactive refresh) ;;
		apk)    REFRESH_ARGV=(apk update) ;;
		*)      REFRESH_ARGV=() ;;
	esac
}

# The commands as someone would type them, for the messages that ask for them to be run by
# hand. $1 is "sudo " or nothing, for a system where sudo is not installed.
show_install_commands() {
	if [ "${#REFRESH_ARGV[@]}" -gt 0 ]; then note "   $1${REFRESH_ARGV[*]}"; fi
	note "   $1${INSTALL_ARGV[*]}"
}

# True when $1 is at least $2, comparing the first two dotted fields. CMake 3.21 and GCC 11
# are the only minimums this script knows about, and both are two fields.
version_at_least() {
	awk -v got="$1" -v want="$2" 'BEGIN {
		n = split(got, g, "."); m = split(want, w, ".")
		for (i = 1; i <= 2; i++) {
			a = (i <= n) ? g[i] + 0 : 0
			b = (i <= m) ? w[i] + 0 : 0
			if (a != b) exit (a > b ? 0 : 1)
		}
		exit 0
	}'
}

show_versions() {
	for t in cmake ninja g++; do
		have "$t" && note "$(printf '%-8s %s' "$t" "$("$t" --version 2>&1 | head -1)")"
	done
	if have cmake && ! version_at_least "$(cmake --version 2>/dev/null | awk 'NR==1 {print $3}')" 3.21; then
		note "cmake is older than 3.21, which the configure step needs. Put a newer one first on PATH."
	fi
	if have g++ && ! version_at_least "$(g++ -dumpversion 2>/dev/null)" 11; then
		note "g++ is older than GCC 11, which C++20 needs. Use a newer compiler, or Clang 14 or newer."
	fi
}

NEEDED=""        # package names missing, for NEEDED dependencies
WANTED=""        # package names missing, for WANTED dependencies
CANNOT=0         # a NEEDED dependency this manager has no name for: nothing can install it
UNPACKAGED=""    # what CANNOT refers to, for the message

while IFS='|' read -r key class probe purpose c_apt c_pacman c_dnf c_zypper c_apk c_xbps; do
	case "$key" in ''|\#*) continue ;; esac
	if [ "$probe" != "-" ] && have "$probe"; then
		note "ok       $purpose"
		continue
	fi
	case "$PM" in
		apt)    names="$c_apt" ;;
		pacman) names="$c_pacman" ;;
		dnf)    names="$c_dnf" ;;
		zypper) names="$c_zypper" ;;
		apk)    names="$c_apk" ;;
		xbps)   names="$c_xbps" ;;
		*)      names="-" ;;
	esac
	if [ "$names" = "-" ]; then
		note "unknown  $purpose: no package name for ${PM:-this system}"
		if [ "$class" = NEEDED ]; then CANNOT=1; UNPACKAGED="$UNPACKAGED$purpose; "; fi
		continue
	fi
	missing=""
	for pkg in $names; do
		pkg_installed "$pkg" || missing="$missing $pkg"
	done
	if [ -z "$missing" ]; then
		note "ok       $purpose"
	else
		note "missing  $purpose:$missing"
		if [ "$class" = NEEDED ]; then NEEDED="$NEEDED$missing"; else WANTED="$WANTED$missing"; fi
	fi
done <<'DEPENDENCIES'
# key|class|probe|purpose|apt|pacman|dnf|zypper|apk|xbps
compiler|NEEDED|g++|C++ compiler (GCC 11 or newer, or Clang 14 or newer)|build-essential|gcc|gcc-c++|gcc-c++|g++|gcc
cmake|NEEDED|cmake|CMake 3.21 or newer|cmake|cmake|cmake|cmake|cmake|cmake
ninja|NEEDED|ninja|Ninja, the build tool CMake drives|ninja-build|ninja|ninja-build|ninja|ninja-build|ninja
curl|NEEDED|curl|curl, which fetches esbuild|curl|curl|curl|curl|curl|curl
pkgconfig|NEEDED|pkg-config|pkg-config, which finds the libraries below|pkg-config|pkgconf|pkgconf-pkg-config|pkgconf-pkg-config|pkgconf|pkg-config
vulkan|NEEDED|-|the Vulkan loader and headers|libvulkan-dev|vulkan-icd-loader vulkan-headers|vulkan-loader-devel vulkan-headers|vulkan-devel|vulkan-loader-dev vulkan-headers|vulkan-loader-devel Vulkan-Headers
glfw|NEEDED|-|GLFW 3 development files|libglfw3-dev|glfw|glfw-devel|glfw-devel|glfw-dev|glfw-devel
glm|NEEDED|-|glm, the header-only maths library|libglm-dev|glm|glm-devel|glm-devel|glm-dev|glm
shaderc|NEEDED|-|shaderc, the GLSL to SPIR-V compiler (library and headers)|libshaderc-dev|shaderc|libshaderc-devel|shaderc-devel|shaderc-dev|shaderc
validation|WANTED|-|the Vulkan validation layers, which a Debug engine uses|vulkan-validationlayers|vulkan-validation-layers|vulkan-validation-layers|vulkan-validationlayers|vulkan-validation-layers|Vulkan-ValidationLayers
lavapipe|WANTED|-|Mesa's software Vulkan driver (lavapipe), for a machine with no usable GPU|mesa-vulkan-drivers|vulkan-swrast|mesa-vulkan-drivers|Mesa-vulkan-drivers|mesa-vulkan-swrast|mesa-vulkan-lavapipe
vulkantools|WANTED|vulkaninfo|vulkaninfo and the other Vulkan tools, to see what the loader found|vulkan-tools|vulkan-tools|vulkan-tools|vulkan-tools|vulkan-tools|Vulkan-Tools
DEPENDENCIES

# --check stops here: what was found, and the command that would run, and nothing run.
# It exits 0 only when nothing NEEDED is missing, so it can be used as a test.
if [ "$CHECK_ONLY" -eq 1 ]; then
	say "Check only: nothing installed, nothing built"
	show_versions
	if [ -z "$PM" ]; then
		note "no supported package manager was found, so nothing can be installed here"
	elif [ -n "$NEEDED$WANTED" ]; then
		pm_install_argv $NEEDED $WANTED
		pm_refresh_argv
		note "would run, with sudo and only for this:"
		show_install_commands "sudo "
	else
		note "nothing to install"
	fi
	if [ "$CANNOT" -eq 1 ]; then
		note "The build cannot go on without: ${UNPACKAGED%; }, and this system has no package name for them."
	fi
	[ "$CANNOT" -eq 0 ] && [ -z "$NEEDED" ]
	exit $?
fi

if [ "$CANNOT" -eq 1 ]; then
	note ""
	note "The build cannot go on without these, and this system has no package name for them:"
	note "   ${UNPACKAGED%; }"
	note "Install them by hand, then run this again."
	exit 1
fi

pm_install_argv $NEEDED $WANTED
pm_refresh_argv
if [ -z "$NEEDED$WANTED" ]; then
	note "everything already present"
elif ! have sudo; then
	note "sudo is not installed. Run these as root, then run this again:"
	show_install_commands ""
	[ -n "$NEEDED" ] && exit 1
	note "The build does not need the WANTED ones; a Debug engine runs without them."
elif [ -t 0 ] || sudo -n true 2>/dev/null; then
	note "installing:$NEEDED$WANTED"
	note "(sudo is needed for this, and only for this)"
	show_install_commands "sudo "
	if [ "${#REFRESH_ARGV[@]}" -gt 0 ]; then
		sudo "${REFRESH_ARGV[@]}" || { note "package index refresh failed"; exit 1; }
	fi
	PKG_LOG="$(mktemp)"
	sudo "${INSTALL_ARGV[@]}" 2>&1 | tee "$PKG_LOG"
	if [ "${PIPESTATUS[0]}" -ne 0 ]; then
		note "install failed"
		if [ "$PM" = pacman ] && grep -qiE "target not found|failed retrieving file|returned error: 404|could not find all required" "$PKG_LOG"; then
			note ""
			note "pacman could not find these packages. That usually means the local package"
			note "database is older than the mirrors. This script does not run a full upgrade"
			note "for you. Run it yourself, then run this script again. Arch does not support"
			note "partial upgrades, so there is no shorter fix:"
			note "   sudo pacman -Syu"
		fi
		rm -f "$PKG_LOG"
		exit 1
	fi
	rm -f "$PKG_LOG"
else
	# No terminal to ask for a password on, and no password-less sudo.
	note "sudo cannot ask for a password here. Install them yourself:"
	show_install_commands "sudo "
	if [ -n "$NEEDED" ]; then
		note "then run this again."
		exit 1
	fi
	note "The build does not need the WANTED ones; a Debug engine warns about any it"
	note "misses and runs without them."
fi

show_versions

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
	note "(cmake --version)."
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
	note "in the table at the top of this script. To find one that is not there, use $FILE_HINT."
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
note "bin/ holds the shared library, the tool (with rda-preview beside it) and include/RendeerC.h; bin/README.md says how each language reaches it."

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
