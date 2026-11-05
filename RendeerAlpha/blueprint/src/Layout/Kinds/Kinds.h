#pragma once
#include <Layout/WidgetRegistry.h>

// Every kind record, one per element, defined in the file for its family. Declared here so
// WidgetRegistry.cpp can list them, which is how they are registered: a list that names
// each one, rather than records that register themselves at start-up from a static
// library the linker is free to leave out.
namespace RDA::Layout::Kinds {
	// ContainerKinds.cpp
	extern const WidgetKind kContainerKind;
	extern const WidgetKind kPanelKind;
	extern const WidgetKind kStackKind;
	extern const WidgetKind kScrollKind;
	extern const WidgetKind kPopupKind;
	extern const WidgetKind kSplitterKind;
	extern const WidgetKind kTabsKind;
	extern const WidgetKind kTabKind;

	// ControlKinds.cpp
	extern const WidgetKind kLabelKind;
	extern const WidgetKind kButtonKind;
	extern const WidgetKind kCheckboxKind;
	extern const WidgetKind kShortcutKind;
	extern const WidgetKind kSliderKind;
	extern const WidgetKind kNumberKind;
	extern const WidgetKind kTextFieldKind;
	extern const WidgetKind kSelectKind;
	extern const WidgetKind kOptionKind;

	// MediaKinds.cpp
	extern const WidgetKind kImageKind;
	extern const WidgetKind kStreamKind;
	extern const WidgetKind kViewportKind;

	// DataKinds.cpp
	extern const WidgetKind kListKind;
	extern const WidgetKind kTreeKind;
	extern const WidgetKind kGridKind;
	extern const WidgetKind kColumnKind;

	// DockKinds.cpp
	extern const WidgetKind kDockSpaceKind;
	extern const WidgetKind kDockKind;
	extern const WidgetKind kHeaderKind;
}
