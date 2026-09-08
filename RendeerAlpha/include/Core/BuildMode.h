#pragma once

// Optional pieces of the engine, decided when it is compiled.
//
// There used to be three tiers here — Renderer, App and Studio — that each excluded the
// layer above it, so a shipped application could not accidentally link the interface
// layer or a JavaScript engine. They are gone, for two reasons.
//
// The interface layer stopped being optional: it is the product. A build of this engine
// without widgets is not a smaller version of it, it is a different thing that nobody
// asked for.
//
// And the footprint argument turned out to be answered already. The engine is a static
// library, so the linker pulls in an object only if something references it. An
// application that loads compiled layouts and never calls the layout compiler does not
// get the compiler, or QuickJS behind it, whatever the build was told. The tiers were
// guaranteeing by exclusion what linking guarantees by construction — at the cost of
// three named configurations, three source lists, and every change verified six ways.
//
// What is left is a feature that genuinely is optional, because it changes how an
// application is *deployed* rather than what it can do.

// Hot reload: watching layout files and rebuilding what changed.
//
// A development switch, not a deployment one. It costs a stat() per file per frame, and
// it is what makes an application reference the layout compiler -- so a build with it
// off contains no compiler and no JavaScript engine, and one with it on does.
#if defined(RDA_ENABLE_HOT_RELOAD)
	#define RDA_ENABLE_HOT_RELOAD_BUILD 1
#else
	#define RDA_ENABLE_HOT_RELOAD_BUILD 0
#endif

