#pragma once
// The widget toolkit, as one include.
//
// This is the umbrella: each widget lives in its own header under Widgets/ -- the base
// class and the layout vocabulary in WidgetBase.h, then Containers/, Controls/ and
// Media/ -- and this file only includes them, so existing code that includes it still
// gets all of it.
//
// A Widget is a node in the retained tree. It persists: you build it once (typically in
// onStart), keep it, and mutate it at runtime, and each frame the tree is walked and
// every node emits the equivalent immediate-mode call into the Gui.
#include <GraphicalObjects/Widgets/WidgetBase.h>

#include <GraphicalObjects/Widgets/Containers/Stack.h>
#include <GraphicalObjects/Widgets/Containers/Scroll.h>
#include <GraphicalObjects/Widgets/Containers/Container.h>
#include <GraphicalObjects/Widgets/Containers/Panel.h>
#include <GraphicalObjects/Widgets/Containers/Popup.h>
#include <GraphicalObjects/Widgets/Containers/Tabs.h>

#include <GraphicalObjects/Widgets/Controls/Label.h>
#include <GraphicalObjects/Widgets/Controls/Button.h>
#include <GraphicalObjects/Widgets/Controls/Checkbox.h>
#include <GraphicalObjects/Widgets/Controls/Slider.h>
#include <GraphicalObjects/Widgets/Controls/NumberField.h>
#include <GraphicalObjects/Widgets/Controls/TextField.h>
#include <GraphicalObjects/Widgets/Controls/Select.h>
#include <GraphicalObjects/Widgets/Controls/ShortcutKey.h>
#include <GraphicalObjects/Widgets/Controls/Splitter.h>

#include <GraphicalObjects/Widgets/Media/Image.h>
#include <GraphicalObjects/Widgets/Media/Stream.h>
#include <GraphicalObjects/Widgets/Media/Viewport.h>
