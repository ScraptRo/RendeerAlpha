#pragma once
#include <GraphicalObjects/GuiTheme.h>
#include <Layout/Blueprint.h>
#include <string>

// Reading a compiled theme (.rdth) into a Theme.
//
// A theme is written in TypeScript and compiled by `rda theme` into a blueprint; this is
// the half that reads it back, which belongs with the blueprint reader rather than inside
// the style registry every widget includes. Theme::loadFromFile and replaceWithFile reach
// it through Theme::setFileReader, which installThemeReader() fills.
namespace RDA::Layout {

	// Applies every top-level variant in `blueprint` on top of what `theme` holds. An
	// element this build does not know is ignored, so a theme written for a newer engine
	// still styles everything here.
	bool applyThemeBlueprint(Theme& theme, const Blueprint& blueprint);

	// Reads `path` and applies it: merged, or -- with `replace` -- after a reset, so a
	// theme that does not mention a variant puts it back to the default. Read before the
	// reset, so a path that does not load leaves the interface wearing the theme it had.
	bool loadThemeFile(Theme& theme, const std::string& path, bool replace);

	// Makes Theme::loadFromFile and Theme::replaceWithFile read through loadThemeFile.
	// The engine calls it when it starts.
	void installThemeReader();
}
