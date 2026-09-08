# After cloning, on Linux

## The short way

```bash
git clone https://github.com/ScraptRo/RendeerAlpha.git
cd RendeerAlpha
./scripts/linux-bringup.sh
```

The bring-up script is meant for a machine that has none of this yet. It installs what is
missing (asking for `sudo` once, only for `apt`), fetches esbuild, builds the engine with
`-j2` by default, runs the tests, lays the result out in `bin/`, registers the Python and
Node packages with whatever interpreters it found, and reports what Vulkan device it
found. Everything it prints is meant to be readable on its own, so the output can be
pasted somewhere without the machine being present.

`--jobs N` raises the parallelism. Two is the default because a translation unit that
pulls in `vulkan.hpp` is large, and an older machine with four gigabytes will thrash at
`-j8`. `--release` stages a Release engine instead of Debug; a Debug engine is the one to
develop against, because it recompiles a layout you edit while the application runs.

**Run it as yourself, not with `sudo`.** It asks for `sudo` when it needs it, and refuses
to start as root. The reason is not tidiness: a build directory that belongs to root
makes every later build need `sudo` too, and a window opened by a root process under
your desktop session is refused by the X server —
`BadAccess (attempt to access private resource denied)`, and the application aborts
after its first frame. If an earlier run already left root-owned files behind,
`sudo chown -R $USER:$USER` the checkout and any project beside it, then run everything
without `sudo` from then on.

## The manual way

Debian and Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build curl \
                 libvulkan-dev libglfw3-dev libglm-dev libshaderc-dev \
                 vulkan-validationlayers mesa-vulkan-drivers vulkan-tools
./scripts/build.sh
./scripts/test.sh
cmake --build build/unix --config Debug --target stage
```

GCC 11 or Clang 14 and newer; the engine is C++20. Other distributions want the same
libraries under whatever names they use. The first four are what the engine links; the
last three are what a Debug build wants at *run* time — the validation layer it turns on
(it runs without, with a warning), a software Vulkan driver for a machine without a
usable GPU, and `vulkaninfo` to see what the loader found.

Configuring prints where each dependency came from:

```
-- Vulkan: /usr/lib/x86_64-linux-gnu/libvulkan.so
-- shaderc: /usr/lib/x86_64-linux-gnu/libshaderc.so
-- glm: system
-- GLFW: system
-- esbuild: /home/you/RendeerAlpha/bin/esbuild (0.28.2)
```

The `stage` target is what fills `bin/`; the bring-up runs it for you. esbuild by hand
is the platform tarball from the npm registry — `@esbuild/linux-x64` — unpacked so that
`package/bin/esbuild` lands at `bin/esbuild`; or `npm install` in the checkout, if you
have node, and the build finds that copy. Registering the packages by hand is
`python3 bindings/python/register.py` and, for Node, `npm install` inside
`bindings/node`.

## A device to draw on

The engine's own tests need no GPU. The binding tests and any application do, and they
also need a screen: run them from a desktop session rather than over SSH, or they stop
with a message saying there is no `DISPLAY`.

On a machine without a Vulkan-capable GPU — most integrated graphics from before about
2014 — install Mesa's software renderer, `mesa-vulkan-drivers`, which the bring-up script
does for you. It is slow, and entirely adequate for an interface made of rectangles and
text. `vulkaninfo --summary` (from `vulkan-tools`) shows what the loader can see.

## Python on a managed system

Ubuntu 23.04 and later mark the system Python as *externally managed*, and `pip install`
into it refuses. `register.py` does not go through pip: it writes one `.pth` file into
your user site-packages, which Python itself reads. In a virtual environment either
works — `python register.py` or `pip install -e bindings/python`.

## Where things are

```
bin/                       written by `stage`; not tracked
├── librendeer_c.so        the engine as a shared library -- what every non-C++ backend opens
├── rda                    the toolchain: `rda build <project>` compiles an interface
├── esbuild                what the toolchain transforms TypeScript with
├── include/RendeerC.h     the C ABI, for anything with an FFI
├── res/                   the engine's font and syntax definitions, found from here
└── BUILD.txt              which configuration this is

build/linux/               the build tree (build/unix/ if you used the presets)
```

`librendeer_c.so` is linked with `--no-undefined`, so a dependency that did not resolve
is a link error here rather than a `dlopen` failure in whichever backend tries to load
it later. It is also why shaderc comes from `libshaderc.so` on Linux and not from the
`libshaderc_combined.a` Debian ships under that name — that one is not combined, and
links to a page of undefined `glslang::` symbols.

A C++ application never looks in `bin/`: it adds the checkout as a subdirectory and
links the static engine. Every other language does, and `bin/README.md` says how.

---

Next: [adding the engine to a project](../adding-to-a-project/README.md).

Back to [setup](../README.md) · [all documentation](../../README.md)
