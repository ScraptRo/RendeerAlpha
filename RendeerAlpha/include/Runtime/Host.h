#pragma once
#include <Core/BuildMode.h>
#include <Runtime/Transport.h>
#include <GraphicalObjects/GuiTypes.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>

// The shared runtime: one process holding the device, serving many application
// processes. Each client builds its interface in its own process and sends a draw list;
// the host validates it, decodes it, and hands the result to whatever draws.
//
// This is the half that has no GPU in it. Sessions, surfaces, framing and validation
// live here and are testable without a device; compositing reads surfaces() and records
// them. Keeping the seam there is what lets the untrusted part — everything arriving
// over a pipe from another process — be tested properly.
//
// A client is not trusted. It may be an old build, a buggy one, or not ours at all, so
// every count, offset and index is checked against what actually arrived before any of
// it is stored. A client that sends something impossible is dropped, never accommodated:
// the alternative is carrying a half-valid draw list into the renderer.
namespace RDA::Runtime {

	class RuntimeHost {
	public:
		~RuntimeHost();

		// One decoded surface, ready to be drawn. `draw` is in the same shape the
		// in-process GUI produces, so recording it is the same call either way.
		struct Surface {
			uint32_t    clientId = 0;
			uint32_t    surfaceId = 0;
			uint32_t    width = 0;
			uint32_t    height = 0;
			GuiDrawData draw;
			uint64_t    drawVersion = 0;
			bool        dirty = false;  // new geometry since the last render
			bool        presented = false;
		};

		struct Stats {
			uint64_t accepted = 0;   // handshakes completed
			uint64_t refused = 0;    // handshakes rejected on version
			uint64_t dropped = 0;    // clients disconnected, cleanly or otherwise
			uint64_t drawLists = 0;  // draw lists decoded and accepted
			uint64_t presents = 0;
			uint64_t rejected = 0;   // messages refused as malformed
		};

		// The font metrics every client is given at handshake. The host has no renderer
		// of its own, so whoever owns the atlas hands them over once at startup; the
		// payload is built here and then simply replayed to each client that connects.
		void setFontMetrics(const FontMetricsBody& header, const std::vector<WireGlyph>& glyphs);
		bool hasFontMetrics() const { return !mFontPayload.empty(); }

		// Called when a client asks for a surface and when one goes away. The runtime
		// creates and destroys a real window in these; the host itself owns no windowing.
		// The create handler returns the size actually granted, which may differ from the
		// request — that is what is reported back to the client in SurfaceReady. Return
		// {0,0} to refuse, which drops the client.
		using SurfaceCreated = std::function<bool(const Surface&, uint32_t& width, uint32_t& height)>;
		using SurfaceDestroyed = std::function<void(uint32_t clientId, uint32_t surfaceId)>;
		void setSurfaceHandlers(SurfaceCreated created, SurfaceDestroyed destroyed) {
			mOnSurfaceCreated = std::move(created);
			mOnSurfaceDestroyed = std::move(destroyed);
		}

		// Uploading a client's pixels. The host holds no device, so the runtime does the
		// work and hands back an opaque texture the renderer understands; null means the
		// upload failed. The destroy handler releases it.
		using TextureCreated = std::function<const Texture*(uint32_t width, uint32_t height,
		                                                    const uint8_t* rgba)>;
		using TextureDestroyed = std::function<void(const Texture*)>;
		void setTextureHandlers(TextureCreated created, TextureDestroyed destroyed) {
			mOnTextureCreated = std::move(created);
			mOnTextureDestroyed = std::move(destroyed);
		}

		bool start(const std::string& endpoint = defaultEndpoint());
		void stop();
		bool running() const { return mRunning.load(); }

		// Accepts whatever has connected and drains every client's queue. Never blocks:
		// a client that has sent nothing costs one peek.
		void poll();

		const std::vector<Surface>& surfaces() const { return mSurfaces; }
		Surface* findSurface(uint32_t clientId, uint32_t surfaceId);
		// Clears the dirty flags, once a frame has actually been drawn.
		void clearDirty();

		size_t clientCount() const { return mClients.size(); }
		const Stats& stats() const { return mStats; }

		// Input travels the other way: the host owns the real window, so it is the host
		// that sees the OS event and forwards it to whoever owns the surface.
		bool sendInput(uint32_t clientId, uint32_t surfaceId, InputKind kind,
		               float x, float y, uint16_t modifiers = 0, uint32_t codepoint = 0);
		bool requestClose(uint32_t clientId, uint32_t surfaceId);

	private:
		struct Client {
			uint32_t id = 0;
			Channel  channel;
			bool     handshaken = false;
			// Textures this client uploaded, by the id *it* chose. Held per client rather
			// than in one table because that is what makes naming another client's
			// texture impossible: there is no shared namespace to reach into.
			std::unordered_map<uint32_t, const Texture*> textures;
		};

		void acceptLoop();
		// Returns false when the client should be dropped.
		bool handleMessage(Client& client, const Message& msg);
		bool decodeDrawList(Client& client, const Message& msg);
		bool createTexture(Client& client, const Message& msg);
		void releaseTextures(Client& client);
		void dropClient(size_t index);

		std::vector<std::unique_ptr<Client>> mClients;
		std::vector<Surface> mSurfaces;
		// FontMetricsBody followed by its glyphs, ready to send as one message.
		std::vector<uint8_t> mFontPayload;
		SurfaceCreated   mOnSurfaceCreated;
		SurfaceDestroyed mOnSurfaceDestroyed;
		TextureCreated   mOnTextureCreated;
		TextureDestroyed mOnTextureDestroyed;
		Stats mStats;

		// Opened by start() on the calling thread and owned until stop(), so the endpoint
		// exists continuously from the moment start() returns. Handing the accept thread
		// a name to open for itself would leave a window with no pipe, and a client that
		// connects in it fails outright rather than waiting.
		std::unique_ptr<Listener> mListener;

		// New connections arrive on the accept thread and are collected here, because
		// accept() blocks and poll() must not.
		std::thread mAcceptThread;
		std::mutex  mIncomingMutex;
		std::vector<Channel> mIncoming;
		std::atomic<bool> mRunning{ false };
		std::string mEndpoint;
		uint32_t mNextClientId = 0;
	};
}
