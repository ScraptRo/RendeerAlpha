#pragma once
// The engine's door into the Vulkan Memory Allocator.
//
// vk_mem_alloc.h is upstream, and under -Wall -Wextra GCC has three things to say about
// it -- `{ 0 }` initialisers that name one member of several, a couple of variables kept
// for their side effects, a swap() nothing calls. Seventy-one lines per Linux build, none
// of them about this engine and none of them ours to fix. They are held here, at the one
// place the header is included, rather than silenced project-wide where they would also
// hide the same mistakes in code that is ours.
//
// MSVC never sees the pragmas and never complained: it does not have these warnings.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "vk_mem_alloc.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
