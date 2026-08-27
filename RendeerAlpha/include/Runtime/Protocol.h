#pragma once
#include <cstdint>

// The wire format between an application process and the shared runtime.
//
// The runtime owns the Vulkan device, the pipelines and the texture cache; each app is
// its own process that builds its interface locally and ships the result here. That is
// what lets ten running apps share one GPU stack instead of carrying ten of them — the
// saving this engine exists to make, and the reason an app is a client rather than a
// library user.
//
// What crosses the boundary is a *draw list*, not a scene graph and not a shared
// surface. The GUI already produces exactly this — vertices, indices and clipped draw
// commands — and its retained cache means an unchanged frame produces nothing at all,
// so an idle app costs an empty message. A scene graph would put app semantics inside
// the runtime; a shared surface would give every app its own device and give up the
// saving entirely.
//
// ---- compatibility ----
// A client compiled against one version of the engine may meet a runtime built from
// another; with the hybrid distribution model (C++ apps build from source, script apps
// load a prebuilt runtime) that is the normal case, not the exception. So every
// connection opens with a handshake and the runtime refuses what it cannot speak,
// rather than reading a struct that has quietly changed shape underneath it.
//
// Rules for changing this file:
//   * Append to the end of a struct, never reorder or resize an existing field.
//   * Bump kProtocolMinor for an addition a older peer can ignore.
//   * Bump kProtocolMajor for anything else, and reset minor to 0.
//   * Every struct here is fixed-layout POD: no pointers, no virtuals, no std types,
//     explicit padding. What is written is exactly what is read.
namespace RDA::Runtime {

	inline constexpr uint32_t kMagic = 0x41445221; // "RDA!"
	// 2: FontMetrics moved from after Welcome to after SurfaceReady. Not additive — a
	// 1.x client waits for it at the handshake and would block until its timeout — so by
	// this file's own rules it is a major bump, not a minor one.
	inline constexpr uint16_t kProtocolMajor = 2;
	inline constexpr uint16_t kProtocolMinor = 0;

	// A peer accepts a message stream only when the major versions match exactly; a
	// higher minor on the other side means unknown trailing fields, which are skipped.
	constexpr bool compatible(uint16_t major, uint16_t) { return major == kProtocolMajor; }

	enum class MessageType : uint16_t {
		Invalid = 0,

		// ---- client -> runtime ----
		Hello = 1,        // opens the connection; must be the first message
		SurfaceCreate = 2,// ask for a window/surface to draw into
		SurfaceResize = 3,
		DrawList = 4,     // this frame's geometry; absent when nothing changed
		Present = 5,      // end of frame, draw it
		Goodbye = 6,
		TextureCreate = 7, // upload pixels; the runtime answers with a handle
		TextureDestroy = 8,

		// ---- runtime -> client ----
		Welcome = 128,    // handshake accepted
		Refused = 129,    // handshake rejected; see RefusedBody::reason
		SurfaceReady = 130,
		Input = 131,      // pointer/keyboard for one of this client's surfaces
		CloseRequest = 132,
		// The baked font's CPU metrics. Sent immediately before SurfaceReady, not at the
		// handshake: the metrics come from an atlas, the atlas from a renderer, and the
		// renderer only exists once some window does — which is the surface being created
		// right now. Welcoming a client with metrics would mean promising something the
		// runtime cannot have yet when it is the first client to arrive.
		FontMetrics = 133,
		TextureReady = 134,
	};

	// Every message begins with this. `size` counts the payload only, so a reader can
	// skip a message whose type it does not know without understanding its contents —
	// which is what makes an additive change safe for an older peer.
	struct MessageHeader {
		uint32_t magic;     // kMagic; a stream that does not start with it is not ours
		uint16_t type;      // MessageType
		uint16_t flags;     // reserved, must be 0
		uint32_t size;      // payload bytes following this header
		uint32_t sequence;  // per-connection, increasing; for matching replies
	};
	static_assert(sizeof(MessageHeader) == 16, "MessageHeader must stay 16 bytes");

	struct HelloBody {
		uint16_t major;         // the client's kProtocolMajor
		uint16_t minor;
		uint32_t clientPid;
		uint64_t featureFlags;  // reserved for optional capabilities; 0 today
	};

	struct WelcomeBody {
		uint16_t major;         // what the runtime will actually speak
		uint16_t minor;
		uint32_t clientId;      // identifies this client in later messages
	};

	enum class RefuseReason : uint32_t {
		Unknown = 0,
		VersionMismatch = 1,  // major differs — the usual skew case
		TooManyClients = 2,
		Unauthorised = 3,
	};

	struct RefusedBody {
		uint32_t reason;        // RefuseReason
		uint16_t runtimeMajor;  // so the client can say what it needed vs what it found
		uint16_t runtimeMinor;
	};

	struct SurfaceCreateBody {
		uint32_t surfaceId;     // chosen by the client, unique within its connection
		uint32_t width, height;
		uint32_t flags;         // reserved
	};

	struct SurfaceResizeBody {
		uint32_t surfaceId;
		uint32_t width, height;
		uint32_t padding_;
	};

	// Mirrors the GUI's vertex, deliberately rather than sharing the type: the wire
	// layout must not change because a rendering detail changed. Conversion happens at
	// the boundary, and this static layout is the thing both sides agree on.
	struct WireVertex {
		float    x, y;
		float    u, v;
		uint32_t color;   // R8G8B8A8_UNORM, as the GUI already packs it
		uint32_t padding_;
	};
	static_assert(sizeof(WireVertex) == 24, "WireVertex must stay 24 bytes");

	// One clipped batch. `textureId` is a handle the runtime issued, not a pointer —
	// a client cannot name memory in another process.
	struct WireDrawCommand {
		uint32_t indexOffset;
		uint32_t indexCount;
		float    clipX, clipY, clipW, clipH;
		uint64_t textureId;
	};

	// Followed in the payload by, in order:
	//   WireVertex      vertices[vertexCount]
	//   uint16_t        indices[indexCount]
	//   WireDrawCommand commands[commandCount]
	// A frame whose geometry is unchanged sends no DrawList at all, so an idle app
	// costs one Present and nothing else.
	struct DrawListBody {
		uint32_t surfaceId;
		uint32_t vertexCount;
		uint32_t indexCount;
		uint32_t commandCount;
		uint64_t drawVersion;   // the GUI's draw version, for dropping stale frames
	};

	struct PresentBody {
		uint32_t surfaceId;
		uint32_t padding_;
		uint64_t frameId;
	};

	// One input event. The runtime owns the real window, so it is the runtime that sees
	// the OS event and forwards it; the client never talks to the windowing system.
	enum class InputKind : uint16_t {
		PointerMove = 0, PointerDown = 1, PointerUp = 2, Scroll = 3,
		KeyDown = 4, KeyUp = 5, Text = 6,
	};

	struct InputBody {
		uint32_t surfaceId;
		uint16_t kind;       // InputKind
		uint16_t modifiers;  // shift/ctrl/alt bitfield
		float    x, y;       // pointer position, or scroll delta
		uint32_t codepoint;  // Text: the character; Key*: the key code
		uint32_t padding_;
	};

	// Sent once the runtime has a real window backing a surface. Until it arrives the
	// client knows its request was received but not that there is anything to draw into;
	// the size may differ from what it asked for, because the window manager has the
	// final say, so the client must lay out against what comes back rather than what it
	// requested.
	struct SurfaceReadyBody {
		uint32_t surfaceId;
		uint32_t width, height;
		uint32_t flags;         // reserved
	};

	struct CloseRequestBody {
		uint32_t surfaceId;
		uint32_t padding_;
	};

	// Pixels a client wants drawn. Followed in the payload by width*height*4 bytes of
	// RGBA8 — the only format for now, so there is nothing to negotiate.
	//
	// A client has no device, so it cannot make a texture; it sends the pixels once and
	// refers to the result by the id it chose. Ids are scoped to the connection, exactly
	// like surface ids: two clients may both use texture 1 and they are different
	// textures. The runtime never lets one client name another's — a draw command that
	// tries is refused, because sampling a neighbour's pixels is how one application
	// would read another's screen.
	struct TextureCreateBody {
		uint32_t textureId;   // chosen by the client, unique within its connection
		uint32_t width, height;
		uint32_t format;      // reserved; 0 = RGBA8
	};

	struct TextureDestroyBody {
		uint32_t textureId;
		uint32_t padding_;
	};

	// The runtime confirming it holds pixels for that id. `ok` is 0 when the upload
	// failed, so a client is never left drawing with a texture that does not exist.
	struct TextureReadyBody {
		uint32_t textureId;
		uint32_t ok;
	};

	// The most pixels one message may carry. 2048x2048 RGBA is 16MB, and the payload cap
	// below is derived from it so the image and the body describing it always fit in one
	// message together — see the static_assert there.
	//
	// It was 4096 until this was measured: 64MB, four times a cap that was written down
	// independently and never checked against it. Nothing above 2048 has ever reached the
	// wire, because Channel::send refused the payload before it went out — the client saw
	// createTexture() return false with no explanation and no message sent.
	inline constexpr uint32_t kMaxTextureDim = 2048;

	// One baked glyph, mirroring the engine's BakedGlyph. Copied rather than shared for
	// the same reason as WireVertex: the atlas is free to change how it bakes, the wire
	// is not.
	struct WireGlyph {
		uint16_t x0, y0, x1, y1;    // glyph box in the atlas, in texels
		float    xoff, yoff;        // placement relative to the pen
		float    xadvance;
	};
	static_assert(sizeof(WireGlyph) == 20, "WireGlyph must stay 20 bytes");

	// Sent to every client straight after Welcome, and followed in the payload by
	// WireGlyph glyphs[glyphCount].
	//
	// A client process has no device, so it has no atlas — but it still has to measure
	// and lay out text before it can produce a draw list at all. The runtime is the only
	// party that baked the font, so it is the only party that can say how wide a string
	// is. Sending the metrics at handshake also keeps them from drifting: they come from
	// the very atlas the runtime will rasterise those glyphs with.
	//
	// whiteU/whiteV matter as much as the glyphs: they are the texel a solid quad
	// samples, so without them a client cannot draw a filled rectangle either.
	struct FontMetricsBody {
		float    atlasWidth, atlasHeight;
		float    whiteU, whiteV;
		float    ascent;        // top -> baseline, pixels
		float    lineAdvance;   // baseline -> baseline
		float    pixelHeight;   // the size it was baked at
		uint32_t firstCodepoint;// what glyphs[0] describes
		uint32_t glyphCount;
		uint32_t padding_;
	};

	// The largest payload a peer will accept, so a corrupt or hostile length cannot make
	// the other side allocate without bound. A draw list far above this means a bug.
	//
	// Derived from kMaxTextureDim rather than chosen next to it, because the two were
	// chosen separately once and silently disagreed: the biggest texture the protocol
	// allowed could not be put on the wire. Written this way, changing the texture limit
	// moves the cap with it and the two cannot drift apart again. The 64KB is headroom for
	// the body that describes the image — a bare 16MB cap is 16 bytes short of a 2048
	// square texture, which is exactly how the old pair failed.
	inline constexpr uint32_t kMaxPayloadBytes =
		kMaxTextureDim * kMaxTextureDim * 4u + 64u * 1024u;

	// The invariant the comment on kMaxTextureDim claims, now enforced where it cannot be
	// missed. A test caught this once; a compile error catches it before anyone runs one.
	static_assert(static_cast<uint64_t>(kMaxTextureDim) * kMaxTextureDim * 4ull +
	              sizeof(TextureCreateBody) <= kMaxPayloadBytes,
	              "the largest allowed texture must fit in one message, body included");
}
