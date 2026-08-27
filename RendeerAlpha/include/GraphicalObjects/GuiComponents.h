#pragma once
#include <GraphicalObjects/Widget.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Reusable pieces of interface, described in XML instead of assembled in C++.
//
// A component is a named widget subtree. It is parsed once and can be instantiated any
// number of times, each instance getting its own widget state:
//
//   <components>
//     <component name="ScriptCell">
//       <stack direction="vertical" spacing="2" height="content">
//         <textfield id="input"  variant="ide-js"      mode="code"     height="96"/>
//         <splitter  id="split"                                        height="6"/>
//         <textfield id="output" variant="console-out" mode="document" height="content" maxHeight="180"/>
//         <button    id="insert" text="+" variant="ghost"              height="14"/>
//       </stack>
//     </component>
//   </components>
//
//   auto cell = gui.components().instantiate("ScriptCell", *parent, "cell3");
//   cell.find<TextField>("input")->text = "...";
//
// Sizes accept a number, "fill", "fill:2" (a weight), or "content"; every widget also
// takes min/max on either axis, matching SizeSpec. The point is that a layout stops
// being code: it becomes a file that can be edited without a rebuild.
namespace RDA {

	class Gui;

	// What instantiate() hands back: the subtree's root, plus lookup of the widgets the
	// component named. Names are the `id` attributes, without the instance prefix.
	struct ComponentInstance {
		Widget*     root = nullptr;
		std::string instanceId;

		bool valid() const { return root != nullptr; }
		Widget* find(const char* name) const;
		template <typename T>
		T* find(const char* name) const { return dynamic_cast<T*>(find(name)); }
	};

	class GuiComponents {
	public:
		// Merges component definitions from a file. Returns false on a parse or file
		// error, leaving anything already registered intact.
		bool loadFromFile(const std::string& path);
		bool loadFromString(const char* xml, const std::string& sourceName = "<memory>");

		bool has(const std::string& name) const { return mComponents.count(name) != 0; }

		// Builds the component under `parent`. `instanceId` prefixes every child id, so
		// several instances of one component do not share interaction state — leave it
		// empty and one is generated.
		ComponentInstance instantiate(const std::string& name, Widget& parent,
		                              const std::string& instanceId = {});

		// Re-reads any component file whose contents changed on disk, and returns true
		// when one did. Existing instances are left alone: rebuilding them is the
		// application's job, because only it knows which state to carry across and which
		// of its own widget pointers have to be re-acquired.
		//
		// Only does anything when RDA_ENABLE_HOT_RELOAD is defined; otherwise components
		// are read once at load and this is a no-op that returns false. Call it from the
		// application's update, not from a widget callback — the rebuild that follows
		// restructures the tree.
		bool reloadIfChanged();

		// Called when a reload has refreshed the definitions, as an alternative to
		// checking reloadIfChanged()'s return value.
		void setReloadHandler(std::function<void()> handler) { mOnReload = std::move(handler); }

	private:
		struct Definition {
			std::string name;
			std::string source;  // the file it came from, for reloading
			std::string xml;     // the element's own markup, re-parsed on instantiate
		};
		// An instance the registry can rebuild: where it lives and what made it.
		struct LiveInstance {
			std::string component;
			std::string instanceId;
			Widget*     parent = nullptr;
			Widget*     root = nullptr;
			size_t      index = 0;
		};

		std::unordered_map<std::string, Definition> mComponents;
		std::vector<LiveInstance> mInstances;
		// path -> last write time, as reported by the filesystem
		std::unordered_map<std::string, int64_t> mFileStamps;
		std::function<void()> mOnReload;
		uint64_t mInstanceSerial = 0;
	};
}
