#include <Platform/Accessibility.h>
#include <RendeerAlpha.h>
#include <Logger/Logger.h>

#if defined(_WIN32)

	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <ole2.h>
	#include <UIAutomation.h>
	#define GLFW_EXPOSE_NATIVE_WIN32
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>

	#include <algorithm>
	#include <atomic>
	#include <cctype>
	#include <cmath>
	#include <string>
	#include <unordered_map>
	#include <vector>

// UI Automation, spoken by hand.
//
// A window answers WM_GETOBJECT with a root provider, and from there UI Automation walks
// a tree of providers, one per node, asking each what it is, where it is, what it can do.
// Those calls arrive on UI Automation's own threads, whenever a screen reader likes --
// which is why no provider here holds a widget, or anything else the frame loop owns. A
// provider holds an id. Every question it is asked is answered from the latest published
// snapshot of the tree, and every action it is asked for is queued for the next walk.
// Neither side waits on the other for longer than it takes to copy a pointer.
//
// A node that is no longer in the snapshot answers UIA_E_ELEMENTNOTAVAILABLE, which is
// what UI Automation expects of an element that went away; a client holding one simply
// lets go of it.
namespace RDA {

	namespace {

		std::wstring widen(const std::string& text) {
			if (text.empty()) return {};
			const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
			                                     static_cast<int>(text.size()), nullptr, 0);
			std::wstring out(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
			                    out.data(), size);
			return out;
		}

		std::string narrow(const wchar_t* text) {
			if (!text || !*text) return {};
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
			if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
			return out;
		}

		BSTR bstr(const std::string& text) {
			const std::wstring wide = widen(text);
			return SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
		}

		long controlTypeOf(AccessibleRole role) {
			switch (role) {
			case AccessibleRole::Window:   return UIA_WindowControlTypeId;
			case AccessibleRole::Group:    return UIA_GroupControlTypeId;
			case AccessibleRole::Pane:     return UIA_PaneControlTypeId;
			case AccessibleRole::Text:     return UIA_TextControlTypeId;
			case AccessibleRole::Button:   return UIA_ButtonControlTypeId;
			case AccessibleRole::CheckBox: return UIA_CheckBoxControlTypeId;
			case AccessibleRole::Slider:   return UIA_SliderControlTypeId;
			case AccessibleRole::Edit:     return UIA_EditControlTypeId;
			case AccessibleRole::Document: return UIA_DocumentControlTypeId;
			case AccessibleRole::ComboBox: return UIA_ComboBoxControlTypeId;
			case AccessibleRole::List:     return UIA_ListControlTypeId;
			case AccessibleRole::ListItem: return UIA_ListItemControlTypeId;
			case AccessibleRole::Tree:     return UIA_TreeControlTypeId;
			case AccessibleRole::TreeItem: return UIA_TreeItemControlTypeId;
			case AccessibleRole::TabList:  return UIA_TabControlTypeId;
			case AccessibleRole::TabItem:  return UIA_TabItemControlTypeId;
			case AccessibleRole::Image:    return UIA_ImageControlTypeId;
			case AccessibleRole::Menu:     return UIA_MenuControlTypeId;
			default:                       return UIA_CustomControlTypeId;
			}
		}

		bool hasValuePattern(const AccessibleInfo& info) {
			return info.role == AccessibleRole::Edit || info.role == AccessibleRole::Document ||
			       info.role == AccessibleRole::ComboBox;
		}

		// What every provider of one window shares: which window, where the snapshots are,
		// and whether the window is still there. Held by shared_ptr, so a provider a client
		// is still holding after the window closed finds `alive` false rather than a
		// dangling pointer.
		struct Shared {
			HWND hwnd = nullptr;
			std::shared_ptr<AccessibleBridge> bridge;
			std::atomic<bool> alive{ true };
			IRawElementProviderFragmentRoot* root = nullptr;  // the host's; AddRef to hand out

			float scale() const {
				const auto tree = bridge->tree();
				return tree ? tree->unitScale : 1.0f;
			}
			POINT origin() const {
				POINT at{ 0, 0 };
				ClientToScreen(hwnd, &at);
				return at;
			}
		};

		IRawElementProviderFragment* makeNode(const std::shared_ptr<Shared>& shared, uint32_t id);

		// ---- a field's text, as UI Automation walks it --------------------------------------
		//
		// A range is a node and two offsets, in characters. Everything it answers is worked
		// out from the latest snapshot when it is asked -- the text, the lines, where each
		// character is -- so a range a screen reader holds across an edit simply reads the
		// text as it now is, clamped to its length.

		RDA::TextUnit unitOf(::TextUnit unit) {
			switch (unit) {
			case TextUnit_Character: return RDA::TextUnit::Character;
			case TextUnit_Word:      return RDA::TextUnit::Word;
			case TextUnit_Line:      return RDA::TextUnit::Line;
			case TextUnit_Paragraph: return RDA::TextUnit::Paragraph;
			default:                 return RDA::TextUnit::Document;   // Format, Page, Document
			}
		}

		// The field's text as it is now: the snapshot it came from, the node, the codepoints.
		struct TextNow {
			std::shared_ptr<const AccessibleTree> tree;
			const AccessibleNode* node = nullptr;
			std::vector<uint32_t> text;
			int size() const { return static_cast<int>(text.size()); }
		};

		bool textNow(const std::shared_ptr<Shared>& shared, uint32_t id, TextNow& out) {
			if (!shared->alive.load()) return false;
			out.tree = shared->bridge->tree();
			if (!out.tree) return false;
			const int at = out.tree->find(id);
			if (at < 0) return false;
			out.node = &out.tree->nodes[static_cast<size_t>(at)];
			if (!out.node->text.present) return false;
			out.text = textCodepoints(out.node->info.value);
			return true;
		}

		class TextRange final : public ITextRangeProvider {
		public:
			TextRange(std::shared_ptr<Shared> shared, uint32_t id, int start, int end)
				: mShared(std::move(shared)), mId(id), mStart(start), mEnd(end) {}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				if (riid == __uuidof(IUnknown) || riid == __uuidof(ITextRangeProvider)) {
					*out = static_cast<ITextRangeProvider*>(this);
					AddRef();
					return S_OK;
				}
				return E_NOINTERFACE;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
			ULONG STDMETHODCALLTYPE Release() override {
				const ULONG left = --mRefs;
				if (left == 0) delete this;
				return left;
			}

			HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** out) override {
				if (!out) return E_POINTER;
				*out = new TextRange(mShared, mId, mStart, mEnd);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* other, BOOL* out) override {
				if (!out) return E_POINTER;
				auto* them = dynamic_cast<TextRange*>(other);
				*out = (them && them->mId == mId && them->mStart == mStart && them->mEnd == mEnd) ? TRUE : FALSE;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* other,
			                                           TextPatternRangeEndpoint otherEndpoint, int* out) override {
				if (!out) return E_POINTER;
				auto* them = dynamic_cast<TextRange*>(other);
				if (!them || them->mId != mId) return E_INVALIDARG;
				*out = at(endpoint) - them->at(otherEndpoint);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(::TextUnit unit) override {
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				clamp(now);
				int start = 0, end = 0;
				textEnclosing(now.text, now.node->text, unitOf(unit), mStart, start, end);
				mStart = start;
				mEnd = end;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;   // one run of one format: there is nothing to find
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE FindText(BSTR wanted, BOOL backward, BOOL ignoreCase, ITextRangeProvider** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				clamp(now);
				const std::vector<uint32_t> needle = textCodepoints(narrow(wanted));
				if (needle.empty()) return S_OK;
				auto fold = [&](uint32_t c) { return (ignoreCase && c < 0x80) ? static_cast<uint32_t>(std::tolower(static_cast<int>(c))) : c; };
				const int n = static_cast<int>(needle.size());
				auto matchAt = [&](int i) {
					for (int k = 0; k < n; ++k)
						if (fold(now.text[static_cast<size_t>(i + k)]) != fold(needle[static_cast<size_t>(k)])) return false;
					return true;
				};
				if (backward) {
					for (int i = mEnd - n; i >= mStart; --i) if (matchAt(i)) { *out = new TextRange(mShared, mId, i, i + n); break; }
				} else {
					for (int i = mStart; i + n <= mEnd; ++i) if (matchAt(i)) { *out = new TextRange(mShared, mId, i, i + n); break; }
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* out) override {
				if (!out) return E_POINTER;
				VariantInit(out);
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				if (attribute == UIA_IsReadOnlyAttributeId) {
					out->vt = VT_BOOL;
					out->boolVal = now.node->info.readOnly ? VARIANT_TRUE : VARIANT_FALSE;
					return S_OK;
				}
				// Anything else is a property this text does not have, which UI Automation
				// spells as a particular object rather than as an empty value.
				out->vt = VT_UNKNOWN;
				return UiaGetReservedNotSupportedValue(&out->punkVal);
			}

			HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				clamp(now);
				// One rectangle per line the range touches, of the lines that are on screen.
				// A range with nothing in it is where the caret is, which a screen reader
				// asks for to know where to look: it gets the caret's own thin box.
				std::vector<double> boxes;
				const POINT origin = mShared->origin();
				const double k = now.tree->unitScale;
				for (const AccessibleText::Line& line : now.node->text.lines) {
					const int count = static_cast<int>(line.x.size()) - 1;
					if (count < 0) continue;
					const int a = (std::max)(mStart, line.first);
					const int b = (std::min)(mEnd, line.first + count);
					const bool caretHere = (mStart == mEnd && mStart >= line.first && mStart <= line.first + count);
					if (a >= b && !caretHere) continue;
					const float left = line.x[static_cast<size_t>(a - line.first)];
					float right = line.x[static_cast<size_t>((caretHere ? a : b) - line.first)];
					if (right - left < 1.0f) right = left + 1.0f;
					boxes.push_back(origin.x + left * k);
					boxes.push_back(origin.y + line.y * k);
					boxes.push_back((right - left) * k);
					boxes.push_back(line.h * k);
					if (caretHere) break;
				}
				SAFEARRAY* array = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(boxes.size()));
				if (!array) return E_OUTOFMEMORY;
				for (LONG i = 0; i < static_cast<LONG>(boxes.size()); ++i) SafeArrayPutElement(array, &i, &boxes[static_cast<size_t>(i)]);
				*out = array;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** out) override {
				if (!out) return E_POINTER;
				IRawElementProviderFragment* node = makeNode(mShared, mId);
				node->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(out));
				node->Release();
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR* out) override {
				if (!out) return E_POINTER;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				clamp(now);
				int to = mEnd;
				if (maxLength >= 0) to = (std::min)(to, mStart + maxLength);
				*out = bstr(textSlice(now.text, mStart, to));
				return S_OK;
			}

			// Moves the whole range by whole units: a caret steps along by them, and a range
			// with something in it lands on the next unit, all of it.
			HRESULT STDMETHODCALLTYPE Move(::TextUnit unit, int count, int* out) override {
				if (!out) return E_POINTER;
				*out = 0;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				clamp(now);
				const RDA::TextUnit u = unitOf(unit);
				const bool caret = (mStart == mEnd);
				int pos = mStart;
				if (!caret) { int s0 = 0, e0 = 0; textEnclosing(now.text, now.node->text, u, mStart, s0, e0); pos = s0; }
				int moved = 0;
				for (; count > 0 && moved < count; ++moved) {
					const int next = textNext(now.text, now.node->text, u, pos);
					if (next == pos || next >= now.size()) {
						// The last unit is still somewhere a caret can go -- its end -- but not
						// somewhere a whole unit starts.
						if (caret && next != pos && next == now.size()) { pos = next; ++moved; }
						break;
					}
					pos = next;
				}
				for (; count < 0 && moved > count; --moved) {
					const int previous = textPrevious(now.text, now.node->text, u, pos);
					if (previous == pos) break;
					pos = previous;
				}
				mStart = pos;
				mEnd = pos;
				if (!caret) mEnd = textNext(now.text, now.node->text, u, pos);
				*out = moved;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, ::TextUnit unit,
			                                             int count, int* out) override {
				if (!out) return E_POINTER;
				*out = 0;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				clamp(now);
				const RDA::TextUnit u = unitOf(unit);
				int pos = at(endpoint);
				int moved = 0;
				for (; count > 0 && moved < count; ++moved) {
					const int next = textNext(now.text, now.node->text, u, pos);
					if (next == pos) break;
					pos = next;
				}
				for (; count < 0 && moved > count; --moved) {
					const int previous = textPrevious(now.text, now.node->text, u, pos);
					if (previous == pos) break;
					pos = previous;
				}
				set(endpoint, pos);
				*out = moved;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* other,
			                                              TextPatternRangeEndpoint otherEndpoint) override {
				auto* them = dynamic_cast<TextRange*>(other);
				if (!them || them->mId != mId) return E_INVALIDARG;
				set(endpoint, them->at(otherEndpoint));
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Select() override {
				AccessibleRequest one;
				one.id = mId;
				one.action = AccessibleAction::SetSelection;
				one.value = std::to_string(mStart) + "," + std::to_string(mEnd);
				mShared->bridge->request(std::move(one));
				rendeerRequestRedraw();
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE AddToSelection() override { return UIA_E_INVALIDOPERATION; }
			HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
			HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL) override { return S_OK; }
			HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				*out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
				return *out ? S_OK : E_OUTOFMEMORY;
			}

		private:
			std::atomic<ULONG> mRefs{ 1 };
			std::shared_ptr<Shared> mShared;
			uint32_t mId = 0;
			int mStart = 0;
			int mEnd = 0;

			int at(TextPatternRangeEndpoint endpoint) const {
				return endpoint == TextPatternRangeEndpoint_Start ? mStart : mEnd;
			}
			// Moving one end past the other drags the other along: a range is never inside out.
			void set(TextPatternRangeEndpoint endpoint, int value) {
				if (endpoint == TextPatternRangeEndpoint_Start) { mStart = value; if (mEnd < mStart) mEnd = mStart; }
				else { mEnd = value; if (mStart > mEnd) mStart = mEnd; }
			}
			void clamp(const TextNow& now) {
				mStart = std::clamp(mStart, 0, now.size());
				mEnd = std::clamp(mEnd, mStart, now.size());
			}
		};

		// A list of ranges, the way ITextProvider hands them back.
		SAFEARRAY* rangeArray(std::initializer_list<ITextRangeProvider*> ranges) {
			SAFEARRAY* array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(ranges.size()));
			if (!array) return nullptr;
			LONG i = 0;
			for (ITextRangeProvider* one : ranges) {
				SafeArrayPutElement(array, &i, static_cast<IUnknown*>(one));
				one->Release();
				++i;
			}
			return array;
		}

		// ---- what holds selectable items ---------------------------------------------------
		//
		// A separate object rather than one more interface on Node, because ISelectionProvider
		// and ITextProvider both have a GetSelection with the same signature and different
		// meanings -- one class cannot be both. GetPatternProvider hands this out for a list,
		// a tree or a tab strip, and it answers from the same snapshot a node does.
		class SelectionContainer final : public ISelectionProvider {
		public:
			SelectionContainer(std::shared_ptr<Shared> shared, uint32_t id)
				: mShared(std::move(shared)), mId(id) {}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				if (riid == __uuidof(IUnknown) || riid == __uuidof(ISelectionProvider))
					*out = static_cast<ISelectionProvider*>(this);
				else return E_NOINTERFACE;
				AddRef();
				return S_OK;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
			ULONG STDMETHODCALLTYPE Release() override {
				const ULONG left = --mRefs;
				if (left == 0) delete this;
				return left;
			}

			HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				std::vector<uint32_t> chosen;
				for (const int c : node->children) {
					const AccessibleNode& child = tree->nodes[static_cast<size_t>(c)];
					if (child.info.selected > 0) chosen.push_back(child.id);
				}
				SAFEARRAY* array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(chosen.size()));
				if (!array) return E_OUTOFMEMORY;
				LONG i = 0;
				for (const uint32_t id : chosen) {
					IRawElementProviderFragment* one = makeNode(mShared, id);
					IRawElementProviderSimple* simple = nullptr;
					one->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple));
					one->Release();
					SafeArrayPutElement(array, &i, static_cast<IUnknown*>(simple));
					simple->Release();
					++i;
				}
				*out = array;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* out) override {
				if (!out) return E_POINTER;
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = node->info.multiSelectable ? TRUE : FALSE;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* out) override {
				if (!out) return E_POINTER;
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				// A tab strip always has a page open; a list or a tree may have nothing chosen.
				*out = node->info.role == AccessibleRole::TabList ? TRUE : FALSE;
				return S_OK;
			}

		private:
			std::shared_ptr<Shared> mShared;
			uint32_t mId;
			std::atomic<ULONG> mRefs{ 1 };

			bool current(std::shared_ptr<const AccessibleTree>& tree, const AccessibleNode*& node) const {
				if (!mShared->alive.load()) return false;
				tree = mShared->bridge->tree();
				if (!tree) return false;
				const int at = tree->find(mId);
				if (at < 0) return false;
				node = &tree->nodes[static_cast<size_t>(at)];
				return true;
			}
		};

		// ---- a node -----------------------------------------------------------------------

		class Node final : public IRawElementProviderSimple,
		                   public IRawElementProviderFragment,
		                   public IInvokeProvider,
		                   public IToggleProvider,
		                   public IValueProvider,
		                   public IRangeValueProvider,
		                   public IExpandCollapseProvider,
		                   public ISelectionItemProvider,
		                   public IScrollItemProvider,
		                   public ITextProvider2 {
		public:
			Node(std::shared_ptr<Shared> shared, uint32_t id)
				: mShared(std::move(shared)), mId(id) {}

			// ---- IUnknown ----
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
					*out = static_cast<IRawElementProviderSimple*>(this);
				else if (riid == __uuidof(IRawElementProviderFragment))
					*out = static_cast<IRawElementProviderFragment*>(this);
				else if (riid == __uuidof(IInvokeProvider))         *out = static_cast<IInvokeProvider*>(this);
				else if (riid == __uuidof(IToggleProvider))         *out = static_cast<IToggleProvider*>(this);
				else if (riid == __uuidof(IValueProvider))          *out = static_cast<IValueProvider*>(this);
				else if (riid == __uuidof(IRangeValueProvider))     *out = static_cast<IRangeValueProvider*>(this);
				else if (riid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
				else if (riid == __uuidof(ISelectionItemProvider))  *out = static_cast<ISelectionItemProvider*>(this);
				else if (riid == __uuidof(IScrollItemProvider))     *out = static_cast<IScrollItemProvider*>(this);
				else if (riid == __uuidof(ITextProvider))           *out = static_cast<ITextProvider*>(static_cast<ITextProvider2*>(this));
				else if (riid == __uuidof(ITextProvider2))          *out = static_cast<ITextProvider2*>(this);
				else return E_NOINTERFACE;
				AddRef();
				return S_OK;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
			ULONG STDMETHODCALLTYPE Release() override {
				const ULONG left = --mRefs;
				if (left == 0) delete this;
				return left;
			}

			// ---- IRawElementProviderSimple ----
			HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
				if (!out) return E_POINTER;
				// Free-threaded: called from UI Automation's threads, answered from a snapshot.
				*out = ProviderOptions_ServerSideProvider;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				const AccessibleInfo& info = node->info;
				bool has = false;
				switch (pattern) {
				case UIA_InvokePatternId:         has = (info.actions & kCanInvoke) != 0 && info.toggled < 0; break;
				case UIA_TogglePatternId:         has = info.toggled >= 0; break;
				case UIA_ValuePatternId:          has = hasValuePattern(info); break;
				case UIA_RangeValuePatternId:     has = info.hasRange; break;
				case UIA_ExpandCollapsePatternId: has = info.expanded >= 0; break;
				case UIA_SelectionItemPatternId:  has = info.selected >= 0; break;
				case UIA_SelectionPatternId:
					if (info.role == AccessibleRole::List || info.role == AccessibleRole::Tree ||
					    info.role == AccessibleRole::TabList) {
						*out = static_cast<ISelectionProvider*>(new SelectionContainer(mShared, mId));
						return S_OK;
					}
					break;
				case UIA_TextPatternId:
				case UIA_TextPattern2Id:          has = node->text.present; break;
				case UIA_ScrollItemPatternId:
					has = info.role == AccessibleRole::ListItem || info.role == AccessibleRole::TreeItem;
					break;
				default: break;
				}
				if (!has) return S_OK;
				*out = static_cast<IRawElementProviderSimple*>(this);
				AddRef();
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
				if (!out) return E_POINTER;
				VariantInit(out);
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				const AccessibleInfo& info = node->info;
				switch (property) {
				case UIA_ControlTypePropertyId:
					out->vt = VT_I4; out->lVal = controlTypeOf(info.role); break;
				case UIA_NamePropertyId:
					if (!info.name.empty()) { out->vt = VT_BSTR; out->bstrVal = bstr(info.name); }
					break;
				case UIA_AutomationIdPropertyId:
					out->vt = VT_BSTR; out->bstrVal = bstr(node->automationId); break;
				case UIA_IsKeyboardFocusablePropertyId:
					out->vt = VT_BOOL; out->boolVal = info.focusable ? VARIANT_TRUE : VARIANT_FALSE; break;
				case UIA_HasKeyboardFocusPropertyId:
					out->vt = VT_BOOL; out->boolVal = node->focused ? VARIANT_TRUE : VARIANT_FALSE; break;
				case UIA_IsEnabledPropertyId:
				case UIA_IsControlElementPropertyId:
				case UIA_IsContentElementPropertyId:
					out->vt = VT_BOOL; out->boolVal = VARIANT_TRUE; break;
				case UIA_LiveSettingPropertyId:
					out->vt = VT_I4; out->lVal = info.live == 2 ? Assertive : info.live == 1 ? Polite : Off; break;
				case UIA_LevelPropertyId:
					if (info.level > 0) { out->vt = VT_I4; out->lVal = info.level; }
					break;
				case UIA_IsOffscreenPropertyId:
					out->vt = VT_BOOL; out->boolVal = info.offscreen ? VARIANT_TRUE : VARIANT_FALSE; break;
				case UIA_FrameworkIdPropertyId:
				case UIA_ProviderDescriptionPropertyId:
					out->vt = VT_BSTR; out->bstrVal = SysAllocString(L"RendeerAlpha"); break;
				default:
					break;
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;   // only the root is hosted by the window
				return S_OK;
			}

			// ---- IRawElementProviderFragment ----
			HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
			                                   IRawElementProviderFragment** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;

				if (direction == NavigateDirection_Parent) {
					if (node->parent >= 0) {
						*out = makeNode(mShared, tree->nodes[static_cast<size_t>(node->parent)].id);
					} else if (mShared->root) {
						mShared->root->QueryInterface(__uuidof(IRawElementProviderFragment),
						                              reinterpret_cast<void**>(out));
					}
					return S_OK;
				}
				if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
					if (node->children.empty()) return S_OK;
					const int child = (direction == NavigateDirection_FirstChild)
						? node->children.front() : node->children.back();
					*out = makeNode(mShared, tree->nodes[static_cast<size_t>(child)].id);
					return S_OK;
				}
				// A sibling: the one beside this in its parent's list.
				const std::vector<int>& row = (node->parent >= 0)
					? tree->nodes[static_cast<size_t>(node->parent)].children : tree->roots;
				const int self = tree->find(mId);
				for (size_t i = 0; i < row.size(); ++i) {
					if (row[i] != self) continue;
					if (direction == NavigateDirection_NextSibling && i + 1 < row.size())
						*out = makeNode(mShared, tree->nodes[static_cast<size_t>(row[i + 1])].id);
					if (direction == NavigateDirection_PreviousSibling && i > 0)
						*out = makeNode(mShared, tree->nodes[static_cast<size_t>(row[i - 1])].id);
					break;
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				int ids[] = { UiaAppendRuntimeId, static_cast<int>(mId) };
				SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 2);
				if (!array) return E_OUTOFMEMORY;
				for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(array, &i, &ids[i]);
				*out = array;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
				if (!out) return E_POINTER;
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				// Units to pixels here and nowhere else, the same way the renderer does it.
				const POINT at = mShared->origin();
				const double k = tree->unitScale;
				out->left = at.x + node->bounds.x * k;
				out->top = at.y + node->bounds.y * k;
				out->width = node->bounds.w * k;
				out->height = node->bounds.h * k;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE SetFocus() override {
				return ask(AccessibleAction::Focus);
			}

			HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
				if (!out) return E_POINTER;
				*out = mShared->root;
				if (*out) (*out)->AddRef();
				return S_OK;
			}

			// ---- the patterns ----
			HRESULT STDMETHODCALLTYPE Invoke() override { return ask(AccessibleAction::Invoke); }

			HRESULT STDMETHODCALLTYPE Toggle() override { return ask(AccessibleAction::Toggle); }
			HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override {
				if (!out) return E_POINTER;
				const AccessibleNode* node = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = node->info.toggled > 0 ? ToggleState_On : ToggleState_Off;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
				return ask(AccessibleAction::SetValue, narrow(value));
			}
			HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
				if (!out) return E_POINTER;
				const AccessibleNode* node = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = bstr(node->info.value);
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
				if (!out) return E_POINTER;
				const AccessibleNode* node = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = (node->info.readOnly || !(node->info.actions & kCanSetValue)) ? TRUE : FALSE;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE SetValue(double value) override {
				char text[64];
				std::snprintf(text, sizeof(text), "%.17g", value);
				return ask(AccessibleAction::SetValue, text);
			}
			HRESULT STDMETHODCALLTYPE get_Value(double* out) override { return range(out, 0); }
			HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override { return range(out, 1); }
			HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override { return range(out, 2); }
			HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override { return range(out, 3); }
			HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override { return range(out, 3); }

			HRESULT STDMETHODCALLTYPE Expand() override { return ask(AccessibleAction::Expand); }
			HRESULT STDMETHODCALLTYPE Collapse() override { return ask(AccessibleAction::Collapse); }
			HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* out) override {
				if (!out) return E_POINTER;
				const AccessibleNode* node = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = node->info.expanded > 0 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE ScrollIntoView() override { return ask(AccessibleAction::ScrollIntoView); }

			HRESULT STDMETHODCALLTYPE Select() override { return ask(AccessibleAction::Select); }
			HRESULT STDMETHODCALLTYPE AddToSelection() override {
				return ask(inMultiSelect() ? AccessibleAction::AddToSelection : AccessibleAction::Select);
			}
			HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
				// Only where several can be chosen: elsewhere one thing is always chosen, and
				// choosing nothing is not something any of those widgets offers.
				if (!inMultiSelect()) return UIA_E_INVALIDOPERATION;
				return ask(AccessibleAction::RemoveFromSelection);
			}
			HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override {
				if (!out) return E_POINTER;
				const AccessibleNode* node = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = node->info.selected > 0 ? TRUE : FALSE;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				const AccessibleNode* node = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				if (node->parent < 0) return S_OK;
				IRawElementProviderFragment* parent =
					makeNode(mShared, tree->nodes[static_cast<size_t>(node->parent)].id);
				parent->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(out));
				parent->Release();
				return S_OK;
			}

			// ---- the text ----
			HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				const AccessibleText& t = now.node->text;
				const int caret = std::clamp(t.caret < 0 ? 0 : t.caret, 0, now.size());
				const int anchor = std::clamp(t.anchor < 0 ? caret : t.anchor, 0, now.size());
				*out = rangeArray({ new TextRange(mShared, mId, (std::min)(caret, anchor), (std::max)(caret, anchor)) });
				return *out ? S_OK : E_OUTOFMEMORY;
			}
			HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = rangeArray({ new TextRange(mShared, mId, 0, now.size()) });
				return *out ? S_OK : E_OUTOFMEMORY;
			}
			HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				return E_INVALIDARG;   // a field's text has no elements inside it
			}
			HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider** out) override {
				if (!out) return E_POINTER;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				// The character nearest the point on the line under it.
				const POINT origin = mShared->origin();
				const float k = now.tree->unitScale > 0.0f ? now.tree->unitScale : 1.0f;
				const float px = static_cast<float>((point.x - origin.x) / k);
				const float py = static_cast<float>((point.y - origin.y) / k);
				int found = now.size();
				for (const AccessibleText::Line& line : now.node->text.lines) {
					if (py < line.y || py >= line.y + line.h || line.x.empty()) continue;
					found = line.first;
					float best = std::fabs(line.x.front() - px);
					for (size_t i = 1; i < line.x.size(); ++i) {
						const float d = std::fabs(line.x[i] - px);
						if (d < best) { best = d; found = line.first + static_cast<int>(i); }
					}
					break;
				}
				*out = new TextRange(mShared, mId, found, found);
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** out) override {
				if (!out) return E_POINTER;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				*out = new TextRange(mShared, mId, 0, now.size());
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* out) override {
				if (!out) return E_POINTER;
				*out = SupportedTextSelection_Single;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE RangeFromAnnotation(IRawElementProviderSimple*, ITextRangeProvider** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				return E_INVALIDARG;
			}
			HRESULT STDMETHODCALLTYPE GetCaretRange(BOOL* isActive, ITextRangeProvider** out) override {
				if (!isActive || !out) return E_POINTER;
				TextNow now;
				if (!textNow(mShared, mId, now)) return UIA_E_ELEMENTNOTAVAILABLE;
				*isActive = now.node->focused ? TRUE : FALSE;
				const int caret = std::clamp(now.node->text.caret < 0 ? 0 : now.node->text.caret, 0, now.size());
				*out = new TextRange(mShared, mId, caret, caret);
				return S_OK;
			}

		private:
			std::atomic<ULONG> mRefs{ 1 };
			std::shared_ptr<Shared> mShared;
			uint32_t mId = 0;

			bool current(std::shared_ptr<const AccessibleTree>& tree, const AccessibleNode*& node) const {
				if (!mShared->alive.load()) return false;
				tree = mShared->bridge->tree();
				if (!tree) return false;
				const int at = tree->find(mId);
				if (at < 0) return false;
				node = &tree->nodes[static_cast<size_t>(at)];
				return true;
			}

			// Whether what holds this item lets several be chosen.
			bool inMultiSelect() const {
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node) || node->parent < 0) return false;
				return tree->nodes[static_cast<size_t>(node->parent)].info.multiSelectable;
			}

			HRESULT ask(AccessibleAction action, std::string value = {}) {
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				AccessibleRequest one;
				one.id = mId;
				one.action = action;
				one.value = std::move(value);
				mShared->bridge->request(std::move(one));
				// The loop may be asleep: an on-demand window draws when something happens,
				// and this is something happening that the window cannot see.
				rendeerRequestRedraw();
				return S_OK;
			}

			HRESULT range(double* out, int which) {
				if (!out) return E_POINTER;
				const AccessibleNode* node = nullptr;
				std::shared_ptr<const AccessibleTree> tree;
				if (!current(tree, node)) return UIA_E_ELEMENTNOTAVAILABLE;
				const AccessibleInfo& info = node->info;
				switch (which) {
				case 0: *out = info.rangeValue; break;
				case 1: *out = info.rangeMax; break;
				case 2: *out = info.rangeMin; break;
				default: *out = info.rangeStep; break;
				}
				return S_OK;
			}
		};

		IRawElementProviderFragment* makeNode(const std::shared_ptr<Shared>& shared, uint32_t id) {
			return static_cast<IRawElementProviderFragment*>(new Node(shared, id));
		}

		// ---- the root: the window's own element, which the window itself describes ----

		class Root final : public IRawElementProviderSimple,
		                   public IRawElementProviderFragment,
		                   public IRawElementProviderFragmentRoot {
		public:
			explicit Root(std::shared_ptr<Shared> shared) : mShared(std::move(shared)) {}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
					*out = static_cast<IRawElementProviderSimple*>(this);
				else if (riid == __uuidof(IRawElementProviderFragment))
					*out = static_cast<IRawElementProviderFragment*>(this);
				else if (riid == __uuidof(IRawElementProviderFragmentRoot))
					*out = static_cast<IRawElementProviderFragmentRoot*>(this);
				else return E_NOINTERFACE;
				AddRef();
				return S_OK;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
			ULONG STDMETHODCALLTYPE Release() override {
				const ULONG left = --mRefs;
				if (left == 0) delete this;
				return left;
			}

			HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
				if (!out) return E_POINTER;
				*out = ProviderOptions_ServerSideProvider;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
				if (!out) return E_POINTER;
				VariantInit(out);
				// The rest -- the name, that it is a window -- comes from the window itself,
				// through the host provider below.
				if (property == UIA_FrameworkIdPropertyId || property == UIA_ProviderDescriptionPropertyId) {
					out->vt = VT_BSTR;
					out->bstrVal = SysAllocString(L"RendeerAlpha");
				}
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
				if (!out) return E_POINTER;
				return UiaHostProviderFromHwnd(mShared->hwnd, out);
			}

			HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
			                                   IRawElementProviderFragment** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				if (!mShared->alive.load()) return S_OK;
				const auto tree = mShared->bridge->tree();
				if (!tree || tree->roots.empty()) return S_OK;
				if (direction == NavigateDirection_FirstChild)
					*out = makeNode(mShared, tree->nodes[static_cast<size_t>(tree->roots.front())].id);
				else if (direction == NavigateDirection_LastChild)
					*out = makeNode(mShared, tree->nodes[static_cast<size_t>(tree->roots.back())].id);
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;   // a root hosted by a window takes the window's
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
				if (!out) return E_POINTER;
				*out = UiaRect{ 0, 0, 0, 0 };   // the window's, from its host provider
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
			HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
				if (!out) return E_POINTER;
				*out = this;
				AddRef();
				return S_OK;
			}

			// ---- IRawElementProviderFragmentRoot ----
			HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
			                                                   IRawElementProviderFragment** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				if (!mShared->alive.load()) return S_OK;
				const auto tree = mShared->bridge->tree();
				if (!tree) return S_OK;
				const POINT at = mShared->origin();
				const float k = tree->unitScale > 0.0f ? tree->unitScale : 1.0f;
				const glm::vec2 p{ static_cast<float>((x - at.x) / k), static_cast<float>((y - at.y) / k) };
				// The last one that contains it: the walk records a parent before what is
				// in it, and what is drawn later is in front.
				for (size_t i = tree->nodes.size(); i-- > 0;) {
					if (tree->nodes[i].bounds.contains(p)) {
						*out = makeNode(mShared, tree->nodes[i].id);
						return S_OK;
					}
				}
				QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(out));
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
				if (!out) return E_POINTER;
				*out = nullptr;
				if (!mShared->alive.load()) return S_OK;
				const auto tree = mShared->bridge->tree();
				if (tree && tree->focused >= 0)
					*out = makeNode(mShared, tree->nodes[static_cast<size_t>(tree->focused)].id);
				return S_OK;
			}

		private:
			std::atomic<ULONG> mRefs{ 1 };
			std::shared_ptr<Shared> mShared;
		};

		const wchar_t* const kHostProperty = L"RendeerAlpha.Accessibility";

		// ---- the host: one per window ------------------------------------------------------

		class UiaHost final : public AccessibilityHost {
		public:
			UiaHost(HWND hwnd, std::shared_ptr<AccessibleBridge> bridge) {
				mShared = std::make_shared<Shared>();
				mShared->hwnd = hwnd;
				mShared->bridge = std::move(bridge);
				mRoot = new Root(mShared);
				mShared->root = mRoot;
				SetPropW(hwnd, kHostProperty, this);
				mPrevious = reinterpret_cast<WNDPROC>(
					SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
			}

			~UiaHost() override {
				mShared->alive.store(false);
				HWND hwnd = mShared->hwnd;
				if (mPrevious) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(mPrevious));
				RemovePropW(hwnd, kHostProperty);
				// Tells UI Automation this window no longer has a provider, and lets go of
				// any it was holding -- in that order, or a client could be handed the
				// root again between the two.
				UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
				UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(mRoot));
				mShared->root = nullptr;
				mRoot->Release();
			}

			void frameDone() override {
				if (!mShared->bridge->wanted()) return;
				const auto tree = mShared->bridge->tree();
				if (!tree || tree == mLast) return;
				const auto before = mLast;
				mLast = tree;
				if (!UiaClientsAreListening()) return;

				// What came and went. Not worked out precisely: a client told the children
				// changed reads them again, which is cheaper than being right about which.
				bool reshaped = !before || before->nodes.size() != tree->nodes.size();
				if (!reshaped) {
					for (size_t i = 0; i < tree->nodes.size(); ++i) {
						if (tree->nodes[i].id != before->nodes[i].id) { reshaped = true; break; }
					}
				}
				if (reshaped) {
					UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(mRoot),
					                              StructureChangeType_ChildrenInvalidated, nullptr, 0);
				}

				// Where the keyboard went, which is the event a screen reader speaks most.
				const uint32_t focused = tree->focused >= 0 ? tree->nodes[static_cast<size_t>(tree->focused)].id : 0;
				if (focused != mFocused) {
					mFocused = focused;
					if (focused) raise(focused, [](IRawElementProviderSimple* node) {
						UiaRaiseAutomationEvent(node, UIA_AutomationFocusChangedEventId);
					});
				}

				if (!before) return;
				std::unordered_map<uint32_t, const AccessibleNode*> was;
				was.reserve(before->nodes.size());
				for (const AccessibleNode& one : before->nodes) was.emplace(one.id, &one);
				for (const AccessibleNode& now : tree->nodes) {
					auto found = was.find(now.id);
					// A live region that has just appeared saying something is read out,
					// the way one that has just changed is: a toast arriving is the case.
					if (found == was.end()) {
						if (now.info.live && !now.info.name.empty()) liveChanged(now.id);
						continue;
					}
					const AccessibleInfo& a = found->second->info;
					const AccessibleInfo& b = now.info;
					if (a.toggled != b.toggled && b.toggled >= 0) {
						changed(now.id, UIA_ToggleToggleStatePropertyId,
						        a.toggled > 0 ? ToggleState_On : ToggleState_Off,
						        b.toggled > 0 ? ToggleState_On : ToggleState_Off);
					}
					if (a.expanded != b.expanded && b.expanded >= 0) {
						changed(now.id, UIA_ExpandCollapseExpandCollapseStatePropertyId,
						        a.expanded > 0 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed,
						        b.expanded > 0 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed);
					}
					if (a.value != b.value) changedText(now.id, UIA_ValueValuePropertyId, a.value, b.value);
					// The text, and where the caret is in it -- the two events a screen reader
					// reads a field by as the reader types and moves.
					const AccessibleText& ta = found->second->text;
					const AccessibleText& tb = now.text;
					if (tb.present && a.value != b.value) {
						raise(now.id, [](IRawElementProviderSimple* node) {
							UiaRaiseAutomationEvent(node, UIA_Text_TextChangedEventId);
						});
					}
					if (tb.present && (ta.caret != tb.caret || ta.anchor != tb.anchor)) {
						raise(now.id, [](IRawElementProviderSimple* node) {
							UiaRaiseAutomationEvent(node, UIA_Text_TextSelectionChangedEventId);
						});
					}
					if (a.name != b.name)   changedText(now.id, UIA_NamePropertyId, a.name, b.name);
					if (b.live && (a.name != b.name || a.value != b.value) && !(b.name.empty() && b.value.empty()))
						liveChanged(now.id);
					if (b.hasRange && a.rangeValue != b.rangeValue) {
						VARIANT from, to;
						VariantInit(&from); VariantInit(&to);
						from.vt = to.vt = VT_R8;
						from.dblVal = a.rangeValue;
						to.dblVal = b.rangeValue;
						raise(now.id, [&](IRawElementProviderSimple* node) {
							UiaRaiseAutomationPropertyChangedEvent(node, UIA_RangeValueValuePropertyId, from, to);
						});
					}
					// One chosen where only one can be is "selected", which also says the last one
					// was not. Where several can be, a row coming or going is said as that.
					const bool multi = now.parent >= 0 &&
						tree->nodes[static_cast<size_t>(now.parent)].info.multiSelectable;
					if (b.selected > 0 && a.selected <= 0) {
						const EVENTID event = multi ? UIA_SelectionItem_ElementAddedToSelectionEventId
						                            : UIA_SelectionItem_ElementSelectedEventId;
						raise(now.id, [event](IRawElementProviderSimple* node) {
							UiaRaiseAutomationEvent(node, event);
						});
					} else if (multi && b.selected == 0 && a.selected > 0) {
						raise(now.id, [](IRawElementProviderSimple* node) {
							UiaRaiseAutomationEvent(node, UIA_SelectionItem_ElementRemovedFromSelectionEventId);
						});
					}
				}
			}

		private:
			std::shared_ptr<Shared> mShared;
			Root*    mRoot = nullptr;
			WNDPROC  mPrevious = nullptr;
			std::shared_ptr<const AccessibleTree> mLast;
			uint32_t mFocused = 0;

			template <typename Fn>
			void raise(uint32_t id, Fn&& fn) {
				IRawElementProviderFragment* fragment = makeNode(mShared, id);
				IRawElementProviderSimple* simple = nullptr;
				fragment->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple));
				if (simple) { fn(simple); simple->Release(); }
				fragment->Release();
			}
			// What a screen reader reads out without being asked: the node's name, which it
			// reads itself once told the region changed. How urgently is the node's
			// LiveSetting, which it asks for.
			void liveChanged(uint32_t id) {
				raise(id, [](IRawElementProviderSimple* node) {
					UiaRaiseAutomationEvent(node, UIA_LiveRegionChangedEventId);
				});
			}

			void changed(uint32_t id, PROPERTYID property, int before, int after) {
				VARIANT from, to;
				VariantInit(&from); VariantInit(&to);
				from.vt = to.vt = VT_I4;
				from.lVal = before;
				to.lVal = after;
				raise(id, [&](IRawElementProviderSimple* node) {
					UiaRaiseAutomationPropertyChangedEvent(node, property, from, to);
				});
			}
			void changedText(uint32_t id, PROPERTYID property, const std::string& before, const std::string& after) {
				VARIANT from, to;
				VariantInit(&from); VariantInit(&to);
				from.vt = to.vt = VT_BSTR;
				from.bstrVal = bstr(before);
				to.bstrVal = bstr(after);
				raise(id, [&](IRawElementProviderSimple* node) {
					UiaRaiseAutomationPropertyChangedEvent(node, property, from, to);
				});
				VariantClear(&from);
				VariantClear(&to);
			}

			static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
				auto* host = static_cast<UiaHost*>(GetPropW(hwnd, kHostProperty));
				if (!host) return DefWindowProcW(hwnd, message, wParam, lParam);
				if (message == WM_GETOBJECT && static_cast<long>(lParam) == static_cast<long>(UiaRootObjectId)) {
					// Somebody is listening. From now on the GUI describes itself; until
					// the next frame has, the tree under the root is empty, and the
					// structure event that frame raises is what tells the client to look.
					if (!host->mShared->bridge->wanted()) {
						host->mShared->bridge->want();
						RDA_LOG_INFO("accessibility: a client asked; describing the interface from now on");
						rendeerRequestRedraw();
					}
					return UiaReturnRawElementProvider(hwnd, wParam, lParam,
					                                   static_cast<IRawElementProviderSimple*>(host->mRoot));
				}
				return CallWindowProcW(host->mPrevious, hwnd, message, wParam, lParam);
			}
		};
	}

	std::unique_ptr<AccessibilityHost> AccessibilityHost::attach(GLFWwindow* window,
	                                                             std::shared_ptr<AccessibleBridge> bridge) {
		HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
		if (!hwnd || !bridge) return std::make_unique<AccessibilityHost>();
		return std::make_unique<UiaHost>(hwnd, std::move(bridge));
	}
}

#elif !defined(__linux__)

namespace RDA {
	// Not yet spoken on this system (Linux has Platform/AccessibilityLinux.cpp). The bridge is never told anybody is listening,
	// so the GUI never builds a tree, and this costs nothing.
	std::unique_ptr<AccessibilityHost> AccessibilityHost::attach(GLFWwindow* window,
	                                                             std::shared_ptr<AccessibleBridge> bridge) {
		(void)window; (void)bridge;
		return std::make_unique<AccessibilityHost>();
	}
}

#endif
