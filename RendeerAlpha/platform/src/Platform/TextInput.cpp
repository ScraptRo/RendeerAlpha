#include <Platform/TextInput.h>
#include <GraphicalObjects/Input.h>
#include <Core/Wake.h>

#if defined(_WIN32)

	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <imm.h>
	#define GLFW_EXPOSE_NATIVE_WIN32
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>

	#include <cmath>
	#include <string>

namespace RDA {

	namespace {

		std::string narrow(const std::wstring& text) {
			if (text.empty()) return {};
			const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
			                                     nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
			                    out.data(), size, nullptr, nullptr);
			return out;
		}

		// A composition string as the method holds it. GCS_COMPSTR is what is being
		// composed, GCS_RESULTSTR what has just been chosen.
		std::wstring compositionString(HIMC context, DWORD which) {
			const LONG bytes = ImmGetCompositionStringW(context, which, nullptr, 0);
			if (bytes <= 0) return {};
			std::wstring out(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
			ImmGetCompositionStringW(context, which, out.data(), static_cast<DWORD>(bytes));
			return out;
		}

		// Characters, not UTF-16 units: a cursor after a character outside the basic plane
		// is one character along and two units.
		int charactersIn(const std::wstring& text, LONG units) {
			int count = 0;
			for (LONG i = 0; i < units && i < static_cast<LONG>(text.size()); ++i) {
				const wchar_t c = text[static_cast<size_t>(i)];
				if (c >= 0xDC00 && c <= 0xDFFF) continue;   // the second half of a pair
				++count;
			}
			return count;
		}

		const wchar_t* const kHostProperty = L"RendeerAlpha.TextInput";

		class ImmHost final : public TextInputHost {
		public:
			ImmHost(HWND hwnd, Input& input) : mHwnd(hwnd), mInput(input) {
				// Off until a field has the keyboard. Kept, so it can be put back.
				mContext = ImmAssociateContext(hwnd, nullptr);
				SetPropW(hwnd, kHostProperty, this);
				mPrevious = reinterpret_cast<WNDPROC>(
					SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
			}

			~ImmHost() override {
				if (mPrevious) SetWindowLongPtrW(mHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(mPrevious));
				RemovePropW(mHwnd, kHostProperty);
				// The window's own context, back where it was found.
				ImmAssociateContext(mHwnd, mContext);
			}

			void update(bool wantsText, const Rect& caret) override {
				if (wantsText != mOn) {
					mOn = wantsText;
					if (wantsText) {
						ImmAssociateContext(mHwnd, mContext);
					} else {
						// Leaving a field in the middle of a word: the word is dropped, the
						// way it is when focus leaves a native field, rather than left on the
						// screen with nowhere to go.
						if (HIMC context = ImmGetContext(mHwnd)) {
							ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
							ImmReleaseContext(mHwnd, context);
						}
						mInput.setPreedit({}, 0);
						ImmAssociateContext(mHwnd, nullptr);
					}
				}
				if (!wantsText) return;
				const LONG x = static_cast<LONG>(std::lround(caret.x));
				const LONG y = static_cast<LONG>(std::lround(caret.y));
				const LONG h = static_cast<LONG>(std::lround(caret.h));
				if (x == mCaretX && y == mCaretY && h == mCaretH) return;
				mCaretX = x; mCaretY = y; mCaretH = h;
				HIMC context = ImmGetContext(mHwnd);
				if (!context) return;
				// The composition at the caret, for a method that draws its own; and the list
				// of candidates under it, kept clear of the line being typed on.
				COMPOSITIONFORM composition{};
				composition.dwStyle = CFS_POINT;
				composition.ptCurrentPos = { x, y };
				ImmSetCompositionWindow(context, &composition);
				CANDIDATEFORM candidates{};
				candidates.dwIndex = 0;
				candidates.dwStyle = CFS_EXCLUDE;
				candidates.ptCurrentPos = { x, y + h };
				candidates.rcArea = { x, y, x + 1, y + h };
				ImmSetCandidateWindow(context, &candidates);
				ImmReleaseContext(mHwnd, context);
			}

		private:
			HWND    mHwnd = nullptr;
			Input&  mInput;
			WNDPROC mPrevious = nullptr;
			HIMC    mContext = nullptr;
			bool    mOn = false;
			LONG    mCaretX = -1, mCaretY = -1, mCaretH = -1;

			LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam) {
				switch (message) {
				case WM_IME_SETCONTEXT:
					// The method's own composition window is not wanted: the field draws the
					// composition itself, at the caret. Its candidate list still is.
					lParam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
					return DefWindowProcW(mHwnd, message, wParam, lParam);
				case WM_IME_STARTCOMPOSITION:
					return 0;
				case WM_IME_COMPOSITION: {
					HIMC context = ImmGetContext(mHwnd);
					if (!context) break;
					if (lParam & GCS_RESULTSTR) {
						// Chosen: text, as if typed. Taken here rather than left to arrive as
						// WM_CHAR, so it arrives once and whole.
						mInput.imeCommit(narrow(compositionString(context, GCS_RESULTSTR)));
						mInput.setPreedit({}, 0);
					}
					if (lParam & GCS_COMPSTR) {
						const std::wstring now = compositionString(context, GCS_COMPSTR);
						LONG cursor = static_cast<LONG>(now.size());
						if (lParam & GCS_CURSORPOS) cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
						mInput.setPreedit(narrow(now), charactersIn(now, cursor));
					}
					ImmReleaseContext(mHwnd, context);
					RDA::Wake::requestFrame();
					return 0;
				}
				case WM_IME_ENDCOMPOSITION:
					mInput.setPreedit({}, 0);
					RDA::Wake::requestFrame();
					return 0;
				case WM_IME_CHAR:
					// Already taken whole from the result string above.
					return 0;
				case WM_KEYDOWN:
				case WM_KEYUP:
					// A key the method is using to compose. Not a key the interface should
					// act on: Backspace here edits the word being composed, not the field.
					if (wParam == VK_PROCESSKEY) return DefWindowProcW(mHwnd, message, wParam, lParam);
					break;
				default:
					break;
				}
				return CallWindowProcW(mPrevious, mHwnd, message, wParam, lParam);
			}

			static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
				auto* host = static_cast<ImmHost*>(GetPropW(hwnd, kHostProperty));
				if (!host) return DefWindowProcW(hwnd, message, wParam, lParam);
				return host->handle(message, wParam, lParam);
			}
		};
	}

	std::unique_ptr<TextInputHost> TextInputHost::attach(GLFWwindow* window, Input& input) {
		HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
		if (!hwnd) return std::make_unique<TextInputHost>();
		return std::make_unique<ImmHost>(hwnd, input);
	}
}

#else

namespace RDA {
	std::unique_ptr<TextInputHost> TextInputHost::attach(GLFWwindow* window, Input& input) {
		(void)window; (void)input;
		return std::make_unique<TextInputHost>();
	}
}

#endif
