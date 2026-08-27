#include <GraphicalObjects/GuiComponents.h>
#include <Logger/Logger.h>
#include <vendor/tinyxml2/tinyxml2.h>
#include <cstring>
#include <filesystem>

namespace RDA {

	using tinyxml2::XMLDocument;
	using tinyxml2::XMLElement;
	using tinyxml2::XMLPrinter;

	namespace {

		// "120" | "fill" | "fill:2" | "content". Anything unrecognised is left alone, so
		// a widget with no size attribute keeps whatever its constructor set.
		bool parseSize(const char* text, SizeSpec& out) {
			if (!text || !*text) return false;
			if (std::strncmp(text, "fill", 4) == 0) {
				float weight = 1.0f;
				if (text[4] == ':') weight = static_cast<float>(std::atof(text + 5));
				out = SizeSpec::fill(weight > 0.0f ? weight : 1.0f);
				return true;
			}
			if (std::strcmp(text, "content") == 0) { out = SizeSpec::content(); return true; }
			out = SizeSpec::fixed(static_cast<float>(std::atof(text)));
			return true;
		}

		Anchor parseAnchor(const char* text, Anchor fallback) {
			if (!text || !*text) return fallback;
			if (std::strcmp(text, "topleft") == 0)    return Anchor::TopLeft;
			if (std::strcmp(text, "stretchx") == 0)   return Anchor::StretchX;
			if (std::strcmp(text, "stretchy") == 0)   return Anchor::StretchY;
			if (std::strcmp(text, "bottomleft") == 0) return Anchor::BottomLeft;
			if (std::strcmp(text, "bottomright") == 0)return Anchor::BottomRight;
			if (std::strcmp(text, "fill") == 0)       return Anchor::Fill;
			RDA_LOG_WARNING("Unknown anchor '" << text << "' in a component");
			return fallback;
		}

		TextFieldMode parseFieldMode(const char* text) {
			if (text && std::strcmp(text, "code") == 0)     return TextFieldMode::Code;
			if (text && std::strcmp(text, "document") == 0) return TextFieldMode::Document;
			return TextFieldMode::Line;
		}

		// Attributes every widget understands.
		void applyCommon(Widget& widget, const XMLElement* e) {
			parseSize(e->Attribute("width"), widget.width);
			parseSize(e->Attribute("height"), widget.height);
			e->QueryFloatAttribute("minWidth", &widget.width.min);
			e->QueryFloatAttribute("maxWidth", &widget.width.max);
			e->QueryFloatAttribute("minHeight", &widget.height.min);
			e->QueryFloatAttribute("maxHeight", &widget.height.max);

			e->QueryFloatAttribute("x", &widget.rect.x);
			e->QueryFloatAttribute("y", &widget.rect.y);
			e->QueryFloatAttribute("w", &widget.rect.w);
			e->QueryFloatAttribute("h", &widget.rect.h);
			e->QueryFloatAttribute("marginRight", &widget.marginRight);
			e->QueryFloatAttribute("marginBottom", &widget.marginBottom);
			widget.anchor = parseAnchor(e->Attribute("anchor"), widget.anchor);
			e->QueryBoolAttribute("visible", &widget.visible);
		}

		const char* attr(const XMLElement* e, const char* name, const char* fallback = "") {
			const char* value = e->Attribute(name);
			return value ? value : fallback;
		}

		// Builds one element (and its children) under `parent`. Ids are prefixed so two
		// instances of the same component never share interaction state.
		Widget* build(const XMLElement* e, Widget& parent, const std::string& prefix) {
			const char* tag = e->Name();
			const std::string id = prefix + attr(e, "id", tag);
			Widget* made = nullptr;

			if (std::strcmp(tag, "stack") == 0) {
				Stack* stack = parent.add<Stack>(id);
				stack->vertical = std::strcmp(attr(e, "direction", "vertical"), "horizontal") != 0;
				e->QueryFloatAttribute("spacing", &stack->spacing);
				e->QueryFloatAttribute("padding", &stack->padding);
				made = stack;
			} else if (std::strcmp(tag, "scroll") == 0) {
				ScrollView* scroll = parent.add<ScrollView>(id);
				scroll->variant = attr(e, "variant", "default");
				e->QueryFloatAttribute("spacing", &scroll->spacing);
				e->QueryFloatAttribute("padding", &scroll->padding);
				e->QueryFloatAttribute("barWidth", &scroll->barWidth);
				made = scroll;
			} else if (std::strcmp(tag, "container") == 0) {
				made = parent.add<Container>(id);
			} else if (std::strcmp(tag, "panel") == 0) {
				Panel* panel = parent.add<Panel>(id);
				panel->variant = attr(e, "variant", "default");
				made = panel;
			} else if (std::strcmp(tag, "label") == 0) {
				Label* label = parent.add<Label>(id, attr(e, "text"));
				label->variant = attr(e, "variant", "default");
				made = label;
			} else if (std::strcmp(tag, "button") == 0) {
				Button* button = parent.add<Button>(id, attr(e, "text"));
				button->variant = attr(e, "variant", "default");
				made = button;
			} else if (std::strcmp(tag, "checkbox") == 0) {
				Checkbox* box = parent.add<Checkbox>(id, attr(e, "text"));
				box->variant = attr(e, "variant", "default");
				e->QueryBoolAttribute("checked", &box->value);
				made = box;
			} else if (std::strcmp(tag, "slider") == 0) {
				Slider* slider = parent.add<Slider>(id);
				slider->variant = attr(e, "variant", "default");
				e->QueryFloatAttribute("min", &slider->minValue);
				e->QueryFloatAttribute("max", &slider->maxValue);
				e->QueryFloatAttribute("value", &slider->value);
				made = slider;
			} else if (std::strcmp(tag, "splitter") == 0) {
				Splitter* splitter = parent.add<Splitter>(id);
				e->QueryFloatAttribute("min", &splitter->minValue);
				e->QueryFloatAttribute("max", &splitter->maxValue);
				e->QueryFloatAttribute("value", &splitter->value);
				e->QueryBoolAttribute("vertical", &splitter->vertical);
				made = splitter;
			} else if (std::strcmp(tag, "textfield") == 0) {
				TextField* field = parent.add<TextField>(id, parseFieldMode(e->Attribute("mode")));
				field->variant = attr(e, "variant", "default");
				if (const char* text = e->Attribute("text")) field->text = text;
				made = field;
			} else if (std::strcmp(tag, "viewport") == 0) {
				made = parent.add<Viewport>(id);
			} else {
				RDA_LOG_WARNING("Unknown component element <" << tag << ">, ignored");
				return nullptr;
			}

			applyCommon(*made, e);
			for (const XMLElement* child = e->FirstChildElement(); child;
			     child = child->NextSiblingElement()) {
				build(child, *made, prefix);
			}
			return made;
		}

		// Current write time as a plain integer, for change detection.
		int64_t stampOf(const std::string& path) {
			std::error_code ec;
			auto time = std::filesystem::last_write_time(path, ec);
			if (ec) return 0;
			return static_cast<int64_t>(time.time_since_epoch().count());
		}
	}

	Widget* ComponentInstance::find(const char* name) const {
		if (!root || !name) return nullptr;
		return root->find(instanceId + name);
	}

	bool GuiComponents::loadFromString(const char* xml, const std::string& sourceName) {
		XMLDocument doc;
		if (doc.Parse(xml) != tinyxml2::XML_SUCCESS) {
			RDA_LOG_ERROR("Failed to parse components (" << sourceName << "): " << doc.ErrorStr());
			return false;
		}
		const XMLElement* root = doc.RootElement();
		if (!root) {
			RDA_LOG_ERROR("Component XML has no root element");
			return false;
		}

		for (const XMLElement* e = root->FirstChildElement("component"); e;
		     e = e->NextSiblingElement("component")) {
			const char* name = e->Attribute("name");
			if (!name || !*name) {
				RDA_LOG_WARNING("Skipping <component> with no name");
				continue;
			}
			// The element's markup is kept rather than a parsed tree: tinyxml2 owns its
			// nodes per document, and re-parsing on instantiate is cheap next to building
			// the widgets.
			XMLPrinter printer;
			e->Accept(&printer);

			Definition def;
			def.name = name;
			def.source = sourceName;
			def.xml = printer.CStr();
			mComponents[name] = std::move(def);
		}
		return true;
	}

	bool GuiComponents::loadFromFile(const std::string& path) {
		XMLDocument doc;
		if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
			RDA_LOG_WARNING("Component file not loaded (" << path << "): " << doc.ErrorStr());
			return false;
		}
		XMLPrinter printer;
		doc.Accept(&printer);
		const bool ok = loadFromString(printer.CStr(), path);
		if (ok) {
			mFileStamps[path] = stampOf(path);
			RDA_LOG_INFO("Loaded GUI components: " << path);
		}
		return ok;
	}

	ComponentInstance GuiComponents::instantiate(const std::string& name, Widget& parent,
	                                             const std::string& instanceId) {
		ComponentInstance instance;
		auto it = mComponents.find(name);
		if (it == mComponents.end()) {
			RDA_LOG_ERROR("No such component: " << name);
			return instance;
		}

		instance.instanceId = instanceId.empty()
			? (name + "#" + std::to_string(++mInstanceSerial) + ".")
			: (instanceId + ".");

		XMLDocument doc;
		if (doc.Parse(it->second.xml.c_str()) != tinyxml2::XML_SUCCESS) {
			RDA_LOG_ERROR("Component '" << name << "' failed to re-parse");
			return instance;
		}
		const XMLElement* root = doc.RootElement();
		const XMLElement* body = root ? root->FirstChildElement() : nullptr;
		if (!body) {
			RDA_LOG_ERROR("Component '" << name << "' has no root element");
			return instance;
		}

		instance.root = build(body, parent, instance.instanceId);
		if (instance.root) {
			LiveInstance live;
			live.component = name;
			live.instanceId = instance.instanceId;
			live.parent = &parent;
			live.root = instance.root;
			live.index = parent.children().size() - 1;
			mInstances.push_back(live);
		}
		return instance;
	}

	bool GuiComponents::reloadIfChanged() {
#if defined(RDA_ENABLE_HOT_RELOAD)
		// Which files actually changed on disk.
		std::vector<std::string> changed;
		for (const auto& entry : mFileStamps) {
			const int64_t stamp = stampOf(entry.first);
			if (stamp != 0 && stamp != entry.second) changed.push_back(entry.first);
		}
		if (changed.empty()) return false;

		size_t applied = 0;
		for (const std::string& path : changed) {
			XMLDocument doc;
			if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
				// Catching an editor mid-save is normal. The timestamp is deliberately
				// left alone so the next call retries: recording it here would mark the
				// edit as seen and lose it until the file changed again.
				RDA_LOG_WARNING("Component reload deferred (" << path << "): " << doc.ErrorStr());
				continue;
			}
			XMLPrinter printer;
			doc.Accept(&printer);
			if (loadFromString(printer.CStr(), path)) {
				mFileStamps[path] = stampOf(path);
				++applied;
			}
		}
		if (applied == 0) return false; // nothing usable read yet; try again next frame

		// Only the definitions are refreshed here. Rebuilding the live instances is left
		// to the application, deliberately: it is the only party that knows which state
		// is worth carrying across (text someone has typed) and which of its own pointers
		// need re-acquiring. Doing it here would destroy widgets the caller still holds
		// references to, and hand it back a set of dangling pointers to fix up — the very
		// situation the handler is supposed to resolve.
		RDA_LOG_INFO("Components reloaded: " << applied
			<< " file(s); rebuild instances to see the change");
		if (mOnReload) mOnReload();
		return true;
#else
		return false; // components are read once; define RDA_ENABLE_HOT_RELOAD to re-read
#endif
	}
}
