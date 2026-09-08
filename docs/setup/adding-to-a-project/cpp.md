# A C++ project

The native case. The engine is a static library; there is no ABI in the way and no round
trip to anything.

## The tree

```
MyApp/
├── CMakeLists.txt
├── main.cpp
└── res/
    ├── state.ts
    ├── themes/app.ts
    └── layouts/
        ├── rda.d.ts         generated
        ├── tsconfig.json
        └── home.tsx
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyApp VERSION 0.1.0 LANGUAGES C CXX)

set(RDA_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../RendeerAlpha"
    CACHE PATH "Path to the RendeerAlpha checkout")
if(NOT EXISTS "${RDA_ENGINE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "No RendeerAlpha checkout at ${RDA_ENGINE_DIR}. "
                        "Clone it beside this folder, or pass -DRDA_ENGINE_DIR=<path>.")
endif()

# The engine's own tests are not your business. Its toolchain is: `rda` is what
# compiles the layouts, and without it there is nothing to build them with.
set(RDA_BUILD_TESTS OFF)
set(RDA_BUILD_TOOLS ON)

# One directory per configuration, holding the executable and its assets together.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

add_subdirectory("${RDA_ENGINE_DIR}" "${CMAKE_BINARY_DIR}/rendeer")

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE rendeer)

# A development build is told where the sources are, so hot reload watches the file
# you are editing rather than the copy beside the binary. A release build is not.
target_compile_definitions(myapp PRIVATE
    $<$<CONFIG:Debug>:MYAPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">)

rda_add_state(myapp res/state.ts)
rda_add_theme(myapp res/themes/app.ts)
rda_add_types(myapp res/layouts/rda.d.ts res/themes/app.ts res/state.ts)
rda_add_layouts(myapp res/layouts/home.tsx)

# Assets resolve relative to the working directory, not the executable, so they are
# copied beside it and the program is started from there.
add_custom_command(TARGET myapp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/res" "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/res"
    VERBATIM)
```

That is the whole file. `rda_add_state` generates `RdaState.h`, which is what `main.cpp`
includes to get typed access to every signal — see [the first interface in
C++](../first-interface/cpp.md) for the program that goes with this.

## Building

```bash
cmake -B build
cmake --build build --config Debug
```

On Linux add `-G Ninja -DCMAKE_BUILD_TYPE=Debug` to the first line. The executable and its
`res/` land together in `build/bin/Debug/`, and the program is started from there.

---

Back to [adding to a project](README.md) · [setup](../README.md) · [all documentation](../../README.md)
