#include <Platform/WindowFrame.h>
#include <GraphicalObjects/Input.h>
#include <GraphicalObjects/WindowEdges.h>
#include <RendeerAlpha.h>

#if defined(_WIN32)

	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <windowsx.h>
	#define GLFW_EXPOSE_NATIVE_WIN32
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>

namespace RDA {

	namespace {

		const wchar_t* const kHostProperty = L"RendeerAlpha.WindowFrame";

		// What a frame is made of, as far as Windows is concerned. Snapping and the
		// layouts flyout look for a sizing border and a maximise box; WS_CAPTION is what
		// makes a drag of the window a "caption" drag rather than a popup being moved.
		constexpr LONG kFrameStyle = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
		                             WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

		// How far a maximised window's frame hangs off the monitor. Windows sizes a
		// maximised window to the work area *plus* its frame, so with the frame taken away
		// the client area would run that far past every edge of the screen.
		int frameThickness(HWND hwnd, bool horizontal) {
			const UINT dpi = GetDpiForWindow(hwnd);
			return GetSystemMetricsForDpi(horizontal ? SM_CXFRAME : SM_CYFRAME, dpi) +
			       GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
		}

		class Win32Frame final : public WindowFrameHost {
		public:
			Win32Frame(GLFWwindow* window, HWND hwnd, Input& input)
				: mWindow(window), mHwnd(hwnd), mInput(input) {
				SetPropW(hwnd, kHostProperty, this);
				mPrevious = reinterpret_cast<WNDPROC>(
					SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
				applyStyle();
			}

			~Win32Frame() override {
				if (mPrevious) SetWindowLongPtrW(mHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(mPrevious));
				RemovePropW(mHwnd, kHostProperty);
			}

			bool native() const override { return true; }

			void update(const Rect& maximizeButton, bool caption, double edgeBand) override {
				mMaximize = maximizeButton;
				mCaption = caption;
				mBand = edgeBand;
			}

			void captionPressed() override {
				// Posted rather than sent: it is handled on the next pump, outside the frame
				// this was found in, so the move loop it starts keeps the engine drawing.
				POINT cursor{};
				GetCursorPos(&cursor);
				mHandingOver = true;
				PostMessageW(mHwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
			}

		private:
			GLFWwindow* mWindow = nullptr;
			HWND        mHwnd = nullptr;
			Input&      mInput;
			WNDPROC     mPrevious = nullptr;
			Rect        mMaximize{};
			bool        mCaption = false;
			double      mBand = 6.0;
			bool        mTrackingLeave = false;
			bool        mPressedMaximize = false;
			bool        mHandingOver = false;   // a caption press posted by captionPressed

			bool fullscreen() const { return glfwGetWindowMonitor(mWindow) != nullptr; }
			// Fullscreen has no frame at all, and a window given its OS frame back has a
			// real one; either way there is nothing here to stand in for.
			bool active() const {
				return !fullscreen() && glfwGetWindowAttrib(mWindow, GLFW_DECORATED) == GLFW_FALSE;
			}

			void applyStyle() {
				if (!active()) return;
				const LONG style = GetWindowLongW(mHwnd, GWL_STYLE);
				const LONG wanted = (style & ~WS_POPUP) | kFrameStyle;
				if (wanted != style) SetWindowLongW(mHwnd, GWL_STYLE, wanted);
				// Asks for WM_NCCALCSIZE again, which is where the frame is taken away.
				SetWindowPos(mHwnd, nullptr, 0, 0, 0, 0,
				             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			}

			bool overMaximize(POINT client) const {
				return mMaximize.w > 0.0f && mMaximize.h > 0.0f &&
				       mMaximize.contains({ static_cast<float>(client.x), static_cast<float>(client.y) });
			}

			LRESULT hitTest(LPARAM lParam) {
				POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ScreenToClient(mHwnd, &point);
				RECT client{};
				GetClientRect(mHwnd, &client);
				// The edges first, the way a frame has them: the sliver of the maximise
				// button that lies inside the top band resizes, as a real one does.
				if (!IsZoomed(mHwnd)) {
					const uint8_t edge = windowEdgeAt(point.x, point.y, client.right, client.bottom, mBand);
					switch (edge) {
					case EdgeLeft:                 return HTLEFT;
					case EdgeRight:                return HTRIGHT;
					case EdgeTop:                  return HTTOP;
					case EdgeBottom:               return HTBOTTOM;
					case EdgeLeft | EdgeTop:       return HTTOPLEFT;
					case EdgeRight | EdgeTop:      return HTTOPRIGHT;
					case EdgeLeft | EdgeBottom:    return HTBOTTOMLEFT;
					case EdgeRight | EdgeBottom:   return HTBOTTOMRIGHT;
					default: break;
					}
				}
				// What makes Windows 11 offer its layouts when the pointer rests here.
				if (overMaximize(point)) return HTMAXBUTTON;
				// A bare stretch of the layout's title bar, as of the last frame: the OS's
				// own caption from here, press and all. The answer is a frame old, which
				// matters only for a press in the same instant the pointer arrives on a
				// button drawn over the bar.
				if (mCaption) return HTCAPTION;
				return HTCLIENT;
			}

			// The pointer is over the maximise button or the caption, which Windows now
			// thinks of as its own and reports in non-client messages GLFW does not read.
			// Handed on to the engine as the ordinary pointer it is -- so the layout's
			// button still lights up and clicks, and the walk keeps finding out whether the
			// pointer is still on bare bar -- in the client coordinates GLFW would have used.
			void forwardMove(LPARAM lParam) {
				POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ScreenToClient(mHwnd, &point);
				mInput.onCursorPos(point.x, point.y);
				if (!mTrackingLeave) {
					TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE | TME_NONCLIENT, mHwnd, 0 };
					mTrackingLeave = TrackMouseEvent(&track) != FALSE;
				}
				rendeerRequestRedraw();
			}

			LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam) {
				switch (message) {
				case WM_NCCALCSIZE:
					if (wParam == TRUE && active()) {
						auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
						// The whole window is client area: there is no frame to draw.
						// Maximised, less what Windows hangs off the screen.
						if (IsZoomed(mHwnd)) {
							const int x = frameThickness(mHwnd, true);
							const int y = frameThickness(mHwnd, false);
							params->rgrc[0].left += x;
							params->rgrc[0].right -= x;
							params->rgrc[0].top += y;
							params->rgrc[0].bottom -= y;
						}
						return 0;
					}
					break;

				case WM_STYLECHANGING:
					// GLFW computes a frameless window's style as a popup, and sets it again
					// whenever it changes something about the window. Kept a frame's styles,
					// so snapping does not quietly go away the first time it does.
					if (wParam == GWL_STYLE && active()) {
						auto* change = reinterpret_cast<STYLESTRUCT*>(lParam);
						change->styleNew = (change->styleNew & ~WS_POPUP) | kFrameStyle;
					}
					break;

				case WM_NCHITTEST:
					if (active()) return hitTest(lParam);
					break;

				case WM_NCMOUSEMOVE:
					if (wParam == HTMAXBUTTON) { forwardMove(lParam); return 0; }
					// Forwarded and also left to Windows, which tracks the caption itself.
					if (wParam == HTCAPTION) forwardMove(lParam);
					break;

				case WM_NCLBUTTONDOWN:
					if (wParam == HTCAPTION && mHandingOver) {
						mHandingOver = false;
						// The button may already be up, in which case the loop ends at once.
						ReleaseCapture();
						const LRESULT result = CallWindowProcW(mPrevious, mHwnd, message, wParam, lParam);
						// That loop ate the release, and GLFW saw the press go down: handed
						// one, so its idea of the button -- and the engine's -- comes back up.
						POINT cursor{};
						GetCursorPos(&cursor);
						ScreenToClient(mHwnd, &cursor);
						SendMessageW(mHwnd, WM_LBUTTONUP, 0, MAKELPARAM(cursor.x, cursor.y));
						return result;
					}
					[[fallthrough]];
				case WM_NCLBUTTONDBLCLK:
					if (wParam == HTMAXBUTTON) {
						// Not to DefWindowProc, which would run its own button and maximise
						// on release: the layout's button decides what a click does.
						forwardMove(lParam);
						mPressedMaximize = true;
						mInput.onMouseButton(GLFW_MOUSE_BUTTON_LEFT, InputAction::Press, 0);
						return 0;
					}
					break;

				case WM_NCLBUTTONUP:
					if (mPressedMaximize) {
						mPressedMaximize = false;
						forwardMove(lParam);
						mInput.onMouseButton(GLFW_MOUSE_BUTTON_LEFT, InputAction::Release, 0);
						return 0;
					}
					break;

				case WM_NCMOUSELEAVE: {
					mTrackingLeave = false;
					// Off the window entirely: somewhere no widget is, until the next move
					// says where the pointer really is. Still over it -- into the client
					// area, or the move loop taking the capture as a drag starts -- the
					// pointer is left where it was: parked off-screen, the bar under it
					// would be forgotten and the next press handed to the interface.
					POINT cursor{};
					GetCursorPos(&cursor);
					if (!mPressedMaximize && WindowFromPoint(cursor) != mHwnd) {
						mInput.onCursorPos(-10000.0, -10000.0);
						rendeerRequestRedraw();
					}
					break;
				}

				case WM_LBUTTONUP:
					// Released over the client area after a press on the button.
					mPressedMaximize = false;
					break;

				default:
					break;
				}
				return CallWindowProcW(mPrevious, mHwnd, message, wParam, lParam);
			}

			static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
				auto* host = static_cast<Win32Frame*>(GetPropW(hwnd, kHostProperty));
				if (!host) return DefWindowProcW(hwnd, message, wParam, lParam);
				return host->handle(message, wParam, lParam);
			}
		};
	}

	std::unique_ptr<WindowFrameHost> WindowFrameHost::attach(GLFWwindow* window, Input& input) {
		HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
		const bool frameless = window && glfwGetWindowAttrib(window, GLFW_DECORATED) == GLFW_FALSE;
		const bool resizable = window && glfwGetWindowAttrib(window, GLFW_RESIZABLE) == GLFW_TRUE;
		if (!hwnd || !frameless || !resizable) return std::make_unique<WindowFrameHost>();
		return std::make_unique<Win32Frame>(window, hwnd, input);
	}
}

#else

namespace RDA {
	std::unique_ptr<WindowFrameHost> WindowFrameHost::attach(GLFWwindow* window, Input& input) {
		(void)window; (void)input;
		return std::make_unique<WindowFrameHost>();
	}
}

#endif
