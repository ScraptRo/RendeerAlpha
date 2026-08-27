#pragma once
#include <Core/BuildMode.h>
#include <Runtime/Transport.h>
#include <GraphicalObjects/GuiTypes.h>
#include <cstdint>
#include <string>
#include <vector>

// The application side of the runtime connection.
//
// An app process builds its interface locally with the ordinary Gui, then hands the
// resulting draw list here instead of to a Renderer. The runtime rasterises it. From
// the app's point of view the only change is where the frame goes.
//
//     RuntimeClient client;
//     if (!client.connect()) { /* no runtime; run standalone */ }
//     client.createSurface(1, 1280, 800);
//     ...
//     client.pumpInput(guiInput);              // events the runtime saw
//     gui.begin(guiInput); ... gui.end();
//     client.submitFrame(1, gui.drawData(), gui.drawVersion());
//
// submitFrame() sends nothing when the draw version is unchanged, so an idle app costs
// one Present per frame and no geometry at all. That is the same retained-cache saving
// the in-process GUI already gets, carried across the process boundary.
namespace RDA::Runtime {

	class RuntimeClient {
	public:
		// How long connect() waits for the handshake reply before giving up. The pipe
		// opening is not proof of a working runtime, so the reply needs its own bound.
		static constexpr uint32_t kHandshakeTimeoutMs = 3000;

		// Connects and completes the handshake. False means no runtime, or one whose
		// protocol major differs — the caller is expected to fall back to running
		// standalone rather than to retry, since a version mismatch will not resolve
		// itself. refusal() says which it was.
		bool connect(const std::string& endpoint = defaultEndpoint());

		// Connects, starting the runtime first if nothing is listening. This is how the
		// runtime comes to exist: it is not something a user launches, it is started by
		// whichever application needs it first and then shared by all the rest.
		//
		// `runtimePath` empty means "the runtime executable sitting next to this one",
		// which is the normal case for an installed application. Returns false if the
		// runtime could not be started or did not begin listening in time.
		//
		// Racing clients both starting one is expected and handled: the runtime holds a
		// single-instance lock and the loser exits, so the winner serves everyone. Without
		// that lock they would both succeed, because Windows allows several instances of
		// one named pipe and clients would be split between two runtimes.
		bool connectOrStartRuntime(const std::string& runtimePath = {},
		                           const std::string& endpoint = defaultEndpoint(),
		                           uint32_t startTimeoutMs = 10000);

		// The name of the lock a runtime holds, so it can take it and a client knows one
		// is coming up.
		static const char* runtimeInstanceLock() { return "Local\\rendeer-runtime"; }

		void disconnect();
		bool connected() const { return mChannel.valid() && mHandshaken; }

		uint32_t clientId() const { return mClientId; }
		uint16_t runtimeMajor() const { return mRuntimeMajor; }
		uint16_t runtimeMinor() const { return mRuntimeMinor; }
		// Set when connect() failed on a Refused reply rather than on the connection.
		RefuseReason refusal() const { return mRefusal; }

		// The font the runtime will rasterise with, received at handshake. A client has
		// no device and therefore no atlas of its own, so this is the only way it can
		// measure a string before drawing it. Empty until a runtime that has metrics
		// welcomes us.
		struct FontMetrics {
			FontMetricsBody        header{};
			std::vector<WireGlyph> glyphs;
			bool valid() const { return !glyphs.empty(); }
			// Advance width of one character, in pixels; a space's width for anything
			// outside the baked range, matching what the runtime's atlas does.
			float advance(char c) const;
			float textWidth(const char* text) const;
			float lineHeight() const { return header.lineAdvance; }
		};
		const FontMetrics& font() const { return mFont; }

		// Uploads RGBA8 pixels the runtime will rasterise with. `textureId` is chosen by
		// the caller and is scoped to this connection. The pixels are copied onto the wire
		// immediately, so the buffer need not outlive the call.
		bool createTexture(uint32_t textureId, uint32_t width, uint32_t height,
		                   const uint8_t* rgba);
		bool destroyTexture(uint32_t textureId);
		// True once the runtime confirmed it holds pixels for that id. Updated by
		// pumpInput(); drawing with an id before this is refused by the runtime.
		bool textureReady(uint32_t textureId) const;
		// The opaque value to put in a draw command's `texture` field for that id. The
		// runtime resolves it back against this connection's own table.
		const Texture* textureHandle(uint32_t textureId) const;

		bool createSurface(uint32_t surfaceId, uint32_t width, uint32_t height);
		bool resizeSurface(uint32_t surfaceId, uint32_t width, uint32_t height);

		// Converts and sends this frame's geometry, then Present. Skips the geometry
		// when `drawVersion` matches what was last sent for this surface.
		bool submitFrame(uint32_t surfaceId, const GuiDrawData& draw, uint64_t drawVersion);

		// Drains everything waiting and folds the input events into `out`, in the same
		// shape Gui::begin() expects. Returns false if the connection dropped.
		// `out.pointer` and the modifier state persist between calls; the one-shot fields
		// (pressed/released/typed/scroll) are cleared first, exactly as a frame expects.
		bool pumpInput(GuiInput& out);

		// True once the runtime has asked this surface to close.
		bool closeRequested() const { return mCloseRequested; }

		// A surface is only backed by a real window once the runtime says so, and the
		// size it grants may differ from the request — the window manager has the final
		// say. Lay out against these, not against what was asked for. Updated by
		// pumpInput(), so call that before reading them.
		bool surfaceReady() const { return mSurfaceReady; }
		uint32_t surfaceWidth() const { return mSurfaceWidth; }
		uint32_t surfaceHeight() const { return mSurfaceHeight; }

	private:
		bool expect(MessageType type, Message& out);

		Channel  mChannel;
		bool     mHandshaken = false;
		bool     mCloseRequested = false;
		uint32_t mClientId = 0;
		uint16_t mRuntimeMajor = 0;
		uint16_t mRuntimeMinor = 0;
		RefuseReason mRefusal = RefuseReason::Unknown;
		uint64_t mLastDrawVersion = UINT64_MAX; // no frame sent yet
		uint64_t mFrameId = 0;
		FontMetrics mFont;
		bool     mSurfaceReady = false;
		uint32_t mSurfaceWidth = 0;
		uint32_t mSurfaceHeight = 0;
		// Ids the runtime has confirmed. A client keeps no pixels, only the knowledge
		// that the far side does.
		std::vector<uint32_t> mReadyTextures;

		// Reused so a steady frame rate does not allocate; the wire buffers keep their
		// capacity the same way the GUI's own draw buffers do.
		std::vector<uint8_t> mScratch;
	};
}
