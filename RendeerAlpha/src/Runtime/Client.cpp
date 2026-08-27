#include <Runtime/Client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace RDA::Runtime {

	namespace {
		// A pointer means nothing in another process, so what travels is the id the
		// client chose when it uploaded the pixels — carried in the draw command's
		// texture field by textureHandle() and read back out here. Null (the font atlas,
		// and so every solid quad) is 0 by definition, which is why an untextured batch
		// needs no upload at all.
		uint64_t textureIdOf(const Texture* texture) {
			return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(texture));
		}
	}

	float RuntimeClient::FontMetrics::advance(char c) const {
		const uint32_t code = static_cast<unsigned char>(c);
		if (code < header.firstCodepoint) return 0.0f;
		const uint32_t index = code - header.firstCodepoint;
		// Outside the baked range: a space, which is what the atlas falls back to. Any
		// other choice would make a client's layout disagree with the runtime's raster.
		if (index >= glyphs.size()) {
			const uint32_t spaceIndex = ' ' - header.firstCodepoint;
			return spaceIndex < glyphs.size() ? glyphs[spaceIndex].xadvance : 0.0f;
		}
		return glyphs[index].xadvance;
	}

	float RuntimeClient::FontMetrics::textWidth(const char* text) const {
		if (!text) return 0.0f;
		float width = 0.0f;
		for (const char* at = text; *at; ++at) width += advance(*at);
		return width;
	}

	namespace {
		// The directory this executable lives in, so a runtime shipped beside an
		// application is found without anyone configuring a path.
		std::string executableDirectory() {
			char buffer[MAX_PATH]{};
			const DWORD length = ::GetModuleFileNameA(nullptr, buffer, MAX_PATH);
			if (length == 0 || length >= MAX_PATH) return {};
			std::string path(buffer, length);
			const size_t slash = path.find_last_of("\\/");
			return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
		}

		// Started detached and without a console: the runtime outlives the application
		// that happened to start it, and a user opening an app should not see a terminal
		// appear beside it.
		bool startProcess(const std::string& path) {
			STARTUPINFOA startup{};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION process{};
			std::string command = path; // CreateProcess may write to this buffer

			// Started in its own directory, not the application's. The runtime loads its
			// font and shaders by relative path, and inheriting the launcher's directory
			// means it looks for them wherever that application happened to be started
			// from — so it comes up, fails to bake an atlas, and serves nothing.
			const size_t slash = path.find_last_of("\\/");
			const std::string directory = (slash == std::string::npos)
				? std::string() : path.substr(0, slash);

			if (!::CreateProcessA(path.c_str(), command.data(), nullptr, nullptr, FALSE,
			                      DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
			                      nullptr, directory.empty() ? nullptr : directory.c_str(),
			                      &startup, &process)) {
				return false;
			}
			::CloseHandle(process.hThread);
			::CloseHandle(process.hProcess); // not ours to wait on; it outlives us
			return true;
		}
	}

	bool RuntimeClient::connectOrStartRuntime(const std::string& runtimePath,
	                                          const std::string& endpoint,
	                                          uint32_t startTimeoutMs) {
		if (connect(endpoint)) return true;
		// A refusal is an answer: a runtime is there and will not have us. Starting
		// another would not help, and the lock would stop it anyway.
		if (mRefusal == RefuseReason::VersionMismatch) return false;

		const std::string path = runtimePath.empty()
			? executableDirectory() + "RuntimeHost.exe"
			: runtimePath;
		if (path.empty() || !startProcess(path)) return false;

		// It has a device and a window to bring up before it listens, so this waits
		// rather than trying once.
		const auto deadline = std::chrono::steady_clock::now()
		                    + std::chrono::milliseconds(startTimeoutMs);
		while (std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (connect(endpoint)) return true;
		}
		return false;
	}

	bool RuntimeClient::connect(const std::string& endpoint) {
		disconnect();
		mChannel = Channel::connect(endpoint);
		if (!mChannel.valid()) return false;

		HelloBody hello{};
		hello.major = kProtocolMajor;
		hello.minor = kProtocolMinor;
		hello.clientPid = 0; // filled by the runtime from the connection; not trusted here
		hello.featureFlags = 0;
		if (!mChannel.send(MessageType::Hello, &hello, sizeof(hello))) return false;

		// receive() blocks, so the reply is waited for with a deadline: a runtime that
		// accepted the pipe and then never answered — wedged, or not really a runtime —
		// must not hang the application for ever on startup.
		const auto deadline = std::chrono::steady_clock::now()
		                    + std::chrono::milliseconds(kHandshakeTimeoutMs);
		while (!mChannel.pending()) {
			if (std::chrono::steady_clock::now() >= deadline) { mChannel.close(); return false; }
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		Message reply;
		if (!mChannel.receive(reply)) return false;

		if (reply.type() == MessageType::Refused) {
			if (const RefusedBody* body = reply.body<RefusedBody>()) {
				mRefusal = static_cast<RefuseReason>(body->reason);
				mRuntimeMajor = body->runtimeMajor;
				mRuntimeMinor = body->runtimeMinor;
			}
			mChannel.close();
			return false;
		}
		const WelcomeBody* welcome = (reply.type() == MessageType::Welcome)
			? reply.body<WelcomeBody>() : nullptr;
		if (!welcome) { mChannel.close(); return false; } // not a runtime we understand

		// Belt and braces: the runtime already checked, but a client that trusts a
		// Welcome without looking is one protocol change away from misreading a stream.
		if (!compatible(welcome->major, welcome->minor)) {
			mRefusal = RefuseReason::VersionMismatch;
			mRuntimeMajor = welcome->major;
			mRuntimeMinor = welcome->minor;
			mChannel.close();
			return false;
		}

		mRuntimeMajor = welcome->major;
		mRuntimeMinor = welcome->minor;
		mClientId = welcome->clientId;
		mHandshaken = true;

		// Metrics no longer come with the handshake: they arrive just before SurfaceReady,
		// because only creating a surface gives the runtime a window and therefore an
		// atlas. pumpInput() picks them up, so font() is populated by the time
		// surfaceReady() is true.
		mFont = FontMetrics{};

		mLastDrawVersion = UINT64_MAX;
		mCloseRequested = false;
		mSurfaceReady = false;
		return true;
	}

	void RuntimeClient::disconnect() {
		if (mChannel.valid() && mHandshaken) {
			mChannel.send(MessageType::Goodbye, nullptr, 0); // best effort
		}
		mChannel.close();
		mHandshaken = false;
		mClientId = 0;
		mCloseRequested = false;
	}

	bool RuntimeClient::createTexture(uint32_t textureId, uint32_t width, uint32_t height,
	                                  const uint8_t* rgba) {
		if (!connected() || !rgba || width == 0 || height == 0) return false;
		if (width > kMaxTextureDim || height > kMaxTextureDim) return false;
		TextureCreateBody body{};
		body.textureId = textureId;
		body.width = width;
		body.height = height;
		body.format = 0;
		const uint32_t bytes = width * height * 4u;
		return mChannel.send(MessageType::TextureCreate, &body, sizeof(body), rgba, bytes);
	}

	bool RuntimeClient::destroyTexture(uint32_t textureId) {
		if (!connected()) return false;
		TextureDestroyBody body{};
		body.textureId = textureId;
		body.padding_ = 0;
		mReadyTextures.erase(std::remove(mReadyTextures.begin(), mReadyTextures.end(), textureId),
		                     mReadyTextures.end());
		return mChannel.send(MessageType::TextureDestroy, &body, sizeof(body));
	}

	bool RuntimeClient::textureReady(uint32_t textureId) const {
		return std::find(mReadyTextures.begin(), mReadyTextures.end(), textureId)
		     != mReadyTextures.end();
	}

	const Texture* RuntimeClient::textureHandle(uint32_t textureId) const {
		// Not a pointer to anything in this process: the id, carried in the field the
		// draw command has for a texture, and turned back into an id on the way out.
		// A client has no textures of its own to point at.
		return reinterpret_cast<const Texture*>(static_cast<uintptr_t>(textureId));
	}

	bool RuntimeClient::createSurface(uint32_t surfaceId, uint32_t width, uint32_t height) {
		if (!connected()) return false;
		SurfaceCreateBody body{};
		body.surfaceId = surfaceId;
		body.width = width;
		body.height = height;
		body.flags = 0;
		return mChannel.send(MessageType::SurfaceCreate, &body, sizeof(body));
	}

	bool RuntimeClient::resizeSurface(uint32_t surfaceId, uint32_t width, uint32_t height) {
		if (!connected()) return false;
		SurfaceResizeBody body{};
		body.surfaceId = surfaceId;
		body.width = width;
		body.height = height;
		body.padding_ = 0;
		// A resize invalidates what the runtime holds, so the next frame must resend.
		mLastDrawVersion = UINT64_MAX;
		return mChannel.send(MessageType::SurfaceResize, &body, sizeof(body));
	}

	bool RuntimeClient::submitFrame(uint32_t surfaceId, const GuiDrawData& draw,
	                                uint64_t drawVersion) {
		if (!connected()) return false;

		// Unchanged geometry is not resent. This is the retained cache paying off across
		// the process boundary: an idle app sends 16 bytes of Present and nothing else.
		if (drawVersion != mLastDrawVersion) {
			DrawListBody body{};
			body.surfaceId = surfaceId;
			body.vertexCount = static_cast<uint32_t>(draw.vertices.size());
			body.indexCount = static_cast<uint32_t>(draw.indices.size());
			body.commandCount = static_cast<uint32_t>(draw.commands.size());
			body.drawVersion = drawVersion;

			const size_t vertexBytes = draw.vertices.size() * sizeof(WireVertex);
			const size_t indexBytes = draw.indices.size() * sizeof(uint16_t);
			const size_t commandBytes = draw.commands.size() * sizeof(WireDrawCommand);
			mScratch.resize(vertexBytes + indexBytes + commandBytes);
			uint8_t* at = mScratch.data();

			// Converted field by field rather than memcpy'd: GuiVertex is a rendering
			// type and free to change, WireVertex is a contract and is not. Copying one
			// onto the other would make an unrelated change to the renderer silently
			// alter the wire format.
			for (const GuiVertex& v : draw.vertices) {
				WireVertex wire{};
				wire.x = v.pos.x;   wire.y = v.pos.y;
				wire.u = v.uv.x;    wire.v = v.uv.y;
				wire.color = v.color;
				wire.padding_ = 0;
				std::memcpy(at, &wire, sizeof(wire));
				at += sizeof(wire);
			}
			if (indexBytes) {
				std::memcpy(at, draw.indices.data(), indexBytes);
				at += indexBytes;
			}
			for (const GuiDrawCmd& c : draw.commands) {
				WireDrawCommand wire{};
				wire.indexOffset = c.indexOffset;
				wire.indexCount = c.indexCount;
				// GuiDrawCmd::clip is (x0, y0, x1, y1); the wire carries origin+extent.
				wire.clipX = c.clip.x;
				wire.clipY = c.clip.y;
				wire.clipW = c.clip.z - c.clip.x;
				wire.clipH = c.clip.w - c.clip.y;
				wire.textureId = textureIdOf(c.texture);
				std::memcpy(at, &wire, sizeof(wire));
				at += sizeof(wire);
			}

			if (!mChannel.send(MessageType::DrawList, &body, sizeof(body),
			                   mScratch.data(), static_cast<uint32_t>(mScratch.size()))) {
				return false;
			}
			mLastDrawVersion = drawVersion;
		}

		PresentBody present{};
		present.surfaceId = surfaceId;
		present.padding_ = 0;
		present.frameId = ++mFrameId;
		return mChannel.send(MessageType::Present, &present, sizeof(present));
	}

	bool RuntimeClient::pumpInput(GuiInput& out) {
		if (!connected()) return false;

		// One-shot state belongs to a single frame; the pointer position and modifiers
		// persist, because the runtime reports those as changes rather than as state.
		out.pressed = false;
		out.released = false;
		out.scroll = 0.0f;
		out.typed.clear();
		out.editKeys.clear();

		while (mChannel.pending()) {
			Message msg;
			if (!mChannel.receive(msg)) return false;

			switch (msg.type()) {
			case MessageType::Input: {
				const InputBody* in = msg.body<InputBody>();
				if (!in) break;
				out.ctrl = (in->modifiers & 0x1) != 0;
				out.shift = (in->modifiers & 0x2) != 0;
				switch (static_cast<InputKind>(in->kind)) {
				case InputKind::PointerMove:
					out.pointer = { in->x, in->y };
					break;
				case InputKind::PointerDown:
					out.pointer = { in->x, in->y };
					out.pressed = true;
					out.down = true;
					break;
				case InputKind::PointerUp:
					out.pointer = { in->x, in->y };
					out.released = true;
					out.down = false;
					break;
				case InputKind::Scroll:
					out.scroll += in->y;
					break;
				case InputKind::Text:
					// One codepoint per event; ASCII for now, as the GUI's text path is.
					if (in->codepoint && in->codepoint < 0x80)
						out.typed.push_back(static_cast<char>(in->codepoint));
					break;
				default:
					break; // key events map to GuiEditKey once the runtime sends them
				}
				break;
			}
			case MessageType::SurfaceReady: {
				const SurfaceReadyBody* ready = msg.body<SurfaceReadyBody>();
				if (!ready) break;
				mSurfaceReady = true;
				mSurfaceWidth = ready->width;
				mSurfaceHeight = ready->height;
				// The granted size may differ from the request, so whatever the runtime
				// holds for this surface no longer matches what was last sent.
				mLastDrawVersion = UINT64_MAX;
				break;
			}
			case MessageType::FontMetrics: {
				// The runtime can send these again once it has an atlas — the first client
				// to connect is welcomed before any window, and therefore before one exists.
				const FontMetricsBody* body = msg.body<FontMetricsBody>();
				if (!body) break;
				const size_t expected = static_cast<size_t>(body->glyphCount) * sizeof(WireGlyph);
				if (expected != msg.tailSize(sizeof(FontMetricsBody))) break;
				mFont.header = *body;
				mFont.glyphs.resize(body->glyphCount);
				if (body->glyphCount) {
					std::memcpy(mFont.glyphs.data(), msg.tail(sizeof(FontMetricsBody)), expected);
				}
				break;
			}
			case MessageType::TextureReady: {
				const TextureReadyBody* ready = msg.body<TextureReadyBody>();
				if (!ready) break;
				if (ready->ok && !textureReady(ready->textureId)) {
					mReadyTextures.push_back(ready->textureId);
				}
				break;
			}
			case MessageType::CloseRequest:
				mCloseRequested = true;
				break;
			default:
				break; // unknown or out-of-turn: skipped, which is what `size` is for
			}
		}
		return true;
	}

	bool RuntimeClient::expect(MessageType type, Message& out) {
		if (!mChannel.receive(out)) return false;
		return out.type() == type;
	}
}
