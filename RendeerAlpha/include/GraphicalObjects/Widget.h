#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/GuiTheme.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace RDA {
	class Gui;

	// A node in the retained widget tree. Unlike an immediate call, a Widget persists:
	// you build it once (typically in onStart), keep it, and mutate it at runtime. Each
	// frame the tree is walked and every node emits the equivalent immediate-mode call
	// into the Gui — so the retained layer reuses the immediate core's drawing, clipping
	// and hot/active interaction wholesale, and the two coexist in one draw list.
	//
	// `rect` is relative to the parent's content origin; the walk accumulates it.
	class Widget {
	public:
		explicit Widget(std::string id) : mId(std::move(id)) {}
		virtual ~Widget() = default;

		Widget(const Widget&) = delete;
		Widget& operator=(const Widget&) = delete;

		Rect rect{};
		bool visible = true;

		const std::string& id() const { return mId; }

		// ---- tree building / runtime mutation ----
		// Construct a child in place and return a stable pointer to it (ownership stays
		// in the tree). This is also the "modify the tree at runtime" entry point.
		template<typename T, typename... Args>
		T* add(Args&&... args) {
			auto node = std::make_unique<T>(std::forward<Args>(args)...);
			T* raw = node.get();
			mChildren.push_back(std::move(node));
			return raw;
		}
		Widget* addChild(std::unique_ptr<Widget> child);
		Widget* find(const std::string& id);  // depth-first, by id
		bool    remove(const std::string& id); // removes the first match anywhere below
		void    clearChildren() { mChildren.clear(); }
		const std::vector<std::unique_ptr<Widget>>& children() const { return mChildren; }

		// Emits this node (and its subtree) into `gui`, offset by the accumulated origin.
		virtual void paint(Gui& gui, glm::vec2 origin) = 0;

	protected:
		void paintChildren(Gui& gui, glm::vec2 origin);

		std::string mId;
		std::vector<std::unique_ptr<Widget>> mChildren;
	};

	// An invisible grouping node: paints its children, draws nothing itself. The Gui's
	// retained root is one of these.
	class Container : public Widget {
	public:
		explicit Container(std::string id) : Widget(std::move(id)) {}
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A framed panel with a background; clips and positions its children.
	class Panel : public Widget {
	public:
		explicit Panel(std::string id) : Widget(std::move(id)) {}
		Variant variant = kDefaultVariant; // theme variant (see Gui::theme())
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A line of text.
	class Label : public Widget {
	public:
		Label(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string text;
		uint32_t    color = rgba(230, 230, 235); // used when variant == "default"
		Variant     variant = kDefaultVariant;   // a named variant's color wins over `color`
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A clickable button. onClick fires once on each completed click.
	class Button : public Widget {
	public:
		Button(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string           text;
		Variant               variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void()> onClick;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A labelled boolean toggle. onChange fires with the new value when toggled.
	class Checkbox : public Widget {
	public:
		Checkbox(std::string id, std::string label) : Widget(std::move(id)), label(std::move(label)) {}
		std::string               label;
		bool                      value = false;
		Variant                   variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void(bool)> onChange;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A draggable float in [minValue, maxValue]. onChange fires while dragging.
	class Slider : public Widget {
	public:
		explicit Slider(std::string id) : Widget(std::move(id)) {}
		float                      value = 0.0f;
		float                      minValue = 0.0f;
		float                      maxValue = 1.0f;
		Variant                    variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void(float)> onChange;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// An editable text field. `style` selects Line / Document / Code presentation.
	// onChange fires with the new contents on the frames it changes.
	//
	// Styling comes from `style` by default. Naming a theme variant instead replaces it
	// wholesale with the themed one — including its mode, colors and syntax language —
	// so a variant is the one place an editor's whole look is described.
	class TextField : public Widget {
	public:
		TextField(std::string id, TextFieldMode mode = TextFieldMode::Line)
			: Widget(std::move(id)), style(TextFieldStyle::forMode(mode)) {}
		std::string                            text;
		TextFieldStyle                         style;
		Variant                                variant = kDefaultVariant;
		std::function<void(const std::string&)> onChange;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// Displays the engine's offscreen scene texture (ViewportMode::Widget) filling its
	// rect. In Fullscreen mode there is no scene texture, so it draws a placeholder.
	class Viewport : public Widget {
	public:
		explicit Viewport(std::string id) : Widget(std::move(id)) {}
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}

