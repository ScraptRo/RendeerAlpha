#include <Runtime/Host.h>
#include <cmath>
#include <cstring>

namespace RDA::Runtime {

	namespace {
		// A surface no bigger than any display we would plausibly drive. A client asking
		// for something absurd is a bug or an attempt to make the host allocate.
		constexpr uint32_t kMaxSurfaceDim = 16384;
		constexpr size_t   kMaxClients = 64;

		bool finite(float v) { return std::isfinite(v); }
	}

	RuntimeHost::~RuntimeHost() { stop(); }

	void RuntimeHost::setFontMetrics(const FontMetricsBody& header,
	                                 const std::vector<WireGlyph>& glyphs) {
		FontMetricsBody body = header;
		body.glyphCount = static_cast<uint32_t>(glyphs.size());
		mFontPayload.resize(sizeof(body) + glyphs.size() * sizeof(WireGlyph));
		std::memcpy(mFontPayload.data(), &body, sizeof(body));
		if (!glyphs.empty()) {
			std::memcpy(mFontPayload.data() + sizeof(body), glyphs.data(),
			            glyphs.size() * sizeof(WireGlyph));
		}
	}

	bool RuntimeHost::start(const std::string& endpoint) {
		if (mRunning.load()) return false;
		mEndpoint = endpoint;

		// The listener is opened here, on the caller's thread, and stays open. Opening it
		// on the accept thread instead would mean start() could return before the pipe
		// existed, and a client connecting in that window fails rather than waiting —
		// the same race Listener::open() exists to close, one level up.
		mListener = std::make_unique<Listener>();
		if (!mListener->open(endpoint)) { mListener.reset(); return false; }

		mRunning.store(true);
		mAcceptThread = std::thread(&RuntimeHost::acceptLoop, this);
		return true;
	}

	void RuntimeHost::acceptLoop() {
		while (mRunning.load()) {
			Channel channel = mListener->accept(); // blocks; stop() releases it
			if (!mRunning.load()) break;
			if (!channel.valid()) continue;
			std::lock_guard<std::mutex> lock(mIncomingMutex);
			mIncoming.push_back(std::move(channel));
		}
	}

	void RuntimeHost::stop() {
		if (!mRunning.exchange(false)) return;
		// accept() is parked waiting for a connection and will not notice the flag on its
		// own, so one is made and dropped immediately to release it.
		{ Channel wake = Channel::connect(mEndpoint, 200); }
		if (mAcceptThread.joinable()) mAcceptThread.join();
		mListener.reset(); // only after the thread that uses it has stopped

		mClients.clear();
		mSurfaces.clear();
		std::lock_guard<std::mutex> lock(mIncomingMutex);
		mIncoming.clear();
	}

	void RuntimeHost::poll() {
		// Take whatever the accept thread has collected.
		{
			std::lock_guard<std::mutex> lock(mIncomingMutex);
			for (Channel& channel : mIncoming) {
				if (mClients.size() >= kMaxClients) break; // refuse quietly; the pipe closes
				auto client = std::make_unique<Client>();
				client->id = ++mNextClientId;
				client->channel = std::move(channel);
				mClients.push_back(std::move(client));
			}
			mIncoming.clear();
		}

		for (size_t i = 0; i < mClients.size();) {
			Client& client = *mClients[i];
			bool alive = client.channel.valid();

			// Drain everything waiting, so one slow frame does not leave a backlog.
			while (alive && client.channel.pending()) {
				Message msg;
				if (!client.channel.receive(msg)) { alive = false; break; }
				if (!handleMessage(client, msg)) { alive = false; break; }
			}
			// Re-checked afterwards, not just before: a peer that vanished sends nothing,
			// so it is pending() noticing the broken pipe that closes the channel — and
			// that happens inside the loop above, after `alive` was first read.
			if (alive && !client.channel.valid()) alive = false;

			if (!alive) { dropClient(i); continue; }
			++i;
		}
	}

	bool RuntimeHost::handleMessage(Client& client, const Message& msg) {
		// Nothing but Hello is legal before the handshake. Acting on anything else would
		// mean serving a peer whose protocol version is still unknown.
		if (!client.handshaken && msg.type() != MessageType::Hello) {
			++mStats.rejected;
			return false;
		}

		switch (msg.type()) {
		case MessageType::Hello: {
			if (client.handshaken) { ++mStats.rejected; return false; } // Hello twice
			const HelloBody* hello = msg.body<HelloBody>();
			if (!hello) { ++mStats.rejected; return false; }

			if (!compatible(hello->major, hello->minor)) {
				RefusedBody refused{};
				refused.reason = static_cast<uint32_t>(RefuseReason::VersionMismatch);
				refused.runtimeMajor = kProtocolMajor;
				refused.runtimeMinor = kProtocolMinor;
				client.channel.send(MessageType::Refused, &refused, sizeof(refused));
				++mStats.refused;
				return false; // told them why, now close
			}

			WelcomeBody welcome{};
			welcome.major = kProtocolMajor;
			welcome.minor = kProtocolMinor;
			welcome.clientId = client.id;
			if (!client.channel.send(MessageType::Welcome, &welcome, sizeof(welcome))) return false;
			client.handshaken = true;
			++mStats.accepted;

			return true;
		}

		case MessageType::SurfaceCreate: {
			const SurfaceCreateBody* body = msg.body<SurfaceCreateBody>();
			if (!body || body->width == 0 || body->height == 0 ||
			    body->width > kMaxSurfaceDim || body->height > kMaxSurfaceDim) {
				++mStats.rejected;
				return false;
			}
			if (findSurface(client.id, body->surfaceId)) { ++mStats.rejected; return false; }
			Surface surface;
			surface.clientId = client.id;
			surface.surfaceId = body->surfaceId;
			surface.width = body->width;
			surface.height = body->height;
			mSurfaces.push_back(std::move(surface));
			Surface& added = mSurfaces.back();

			// The runtime decides what it can actually give: a window manager may hand
			// back a different size, and the client has to lay out against that rather
			// than against what it asked for.
			uint32_t grantedW = added.width, grantedH = added.height;
			if (mOnSurfaceCreated && !mOnSurfaceCreated(added, grantedW, grantedH)) {
				mSurfaces.pop_back();
				++mStats.rejected;
				return false;
			}
			if (grantedW == 0 || grantedH == 0) { mSurfaces.pop_back(); ++mStats.rejected; return false; }
			added.width = grantedW;
			added.height = grantedH;

			// The surface handler above created the window, so the renderer — and with it
			// the atlas — now exists. Metrics go first, so a client that acts the moment
			// its surface is ready already has them.
			if (mFontPayload.empty()) {
				FontMetricsBody empty{};
				empty.firstCodepoint = 32;
				client.channel.send(MessageType::FontMetrics, &empty, sizeof(empty));
			} else {
				client.channel.send(MessageType::FontMetrics, mFontPayload.data(),
				                    static_cast<uint32_t>(mFontPayload.size()));
			}

			SurfaceReadyBody ready{};
			ready.surfaceId = added.surfaceId;
			ready.width = added.width;
			ready.height = added.height;
			ready.flags = 0;
			return client.channel.send(MessageType::SurfaceReady, &ready, sizeof(ready));
		}

		case MessageType::SurfaceResize: {
			const SurfaceResizeBody* body = msg.body<SurfaceResizeBody>();
			if (!body || body->width == 0 || body->height == 0 ||
			    body->width > kMaxSurfaceDim || body->height > kMaxSurfaceDim) {
				++mStats.rejected;
				return false;
			}
			Surface* surface = findSurface(client.id, body->surfaceId);
			if (!surface) { ++mStats.rejected; return false; }
			surface->width = body->width;
			surface->height = body->height;
			return true;
		}

		case MessageType::TextureCreate:
			return createTexture(client, msg);

		case MessageType::TextureDestroy: {
			const TextureDestroyBody* body = msg.body<TextureDestroyBody>();
			if (!body) { ++mStats.rejected; return false; }
			auto found = client.textures.find(body->textureId);
			if (found == client.textures.end()) { ++mStats.rejected; return false; }
			if (mOnTextureDestroyed) mOnTextureDestroyed(found->second);
			client.textures.erase(found);
			return true;
		}

		case MessageType::DrawList:
			return decodeDrawList(client, msg);

		case MessageType::Present: {
			const PresentBody* body = msg.body<PresentBody>();
			if (!body) { ++mStats.rejected; return false; }
			Surface* surface = findSurface(client.id, body->surfaceId);
			if (!surface) { ++mStats.rejected; return false; }
			surface->presented = true;
			++mStats.presents;
			return true;
		}

		case MessageType::Goodbye:
			return false; // a clean close is still a close

		default:
			// An unknown type from a compatible peer is a forward-compatible addition, so
			// it is skipped rather than fatal — that is what the size field is for.
			return true;
		}
	}

	bool RuntimeHost::createTexture(Client& client, const Message& msg) {
		const TextureCreateBody* body = msg.body<TextureCreateBody>();
		if (!body) { ++mStats.rejected; return false; }
		if (body->width == 0 || body->height == 0 ||
		    body->width > kMaxTextureDim || body->height > kMaxTextureDim) {
			++mStats.rejected;
			return false;
		}
		// The declared size must account for exactly the bytes that arrived, in 64-bit so
		// width*height*4 cannot wrap into something small and plausible.
		const uint64_t expected = static_cast<uint64_t>(body->width) * body->height * 4u;
		if (expected != msg.tailSize(sizeof(TextureCreateBody))) { ++mStats.rejected; return false; }
		// Re-using an id would strand the old texture with nothing able to release it.
		if (client.textures.count(body->textureId)) { ++mStats.rejected; return false; }

		const Texture* texture = mOnTextureCreated
			? mOnTextureCreated(body->width, body->height, msg.tail(sizeof(TextureCreateBody)))
			: nullptr;

		TextureReadyBody ready{};
		ready.textureId = body->textureId;
		ready.ok = texture ? 1u : 0u;
		if (!client.channel.send(MessageType::TextureReady, &ready, sizeof(ready))) return false;
		// A failed upload is reported, not fatal: the client is told so it can stop
		// drawing with it rather than being disconnected over a texture.
		if (texture) client.textures.emplace(body->textureId, texture);
		return true;
	}

	void RuntimeHost::releaseTextures(Client& client) {
		if (mOnTextureDestroyed) {
			for (auto& entry : client.textures) mOnTextureDestroyed(entry.second);
		}
		client.textures.clear();
	}

	bool RuntimeHost::decodeDrawList(Client& client, const Message& msg) {
		const DrawListBody* body = msg.body<DrawListBody>();
		if (!body) { ++mStats.rejected; return false; }

		Surface* surface = findSurface(client.id, body->surfaceId);
		if (!surface) { ++mStats.rejected; return false; }

		// The declared counts must account for exactly what arrived. Checked in 64-bit so
		// the multiplications cannot wrap into a small, plausible-looking total.
		const uint64_t vertexBytes = static_cast<uint64_t>(body->vertexCount) * sizeof(WireVertex);
		const uint64_t indexBytes = static_cast<uint64_t>(body->indexCount) * sizeof(uint16_t);
		const uint64_t commandBytes = static_cast<uint64_t>(body->commandCount) * sizeof(WireDrawCommand);
		const uint64_t expected = vertexBytes + indexBytes + commandBytes;
		if (expected != msg.tailSize(sizeof(DrawListBody))) { ++mStats.rejected; return false; }

		const uint8_t* at = msg.tail(sizeof(DrawListBody));
		if (!at && expected != 0) { ++mStats.rejected; return false; }

		GuiDrawData decoded;
		decoded.vertices.resize(body->vertexCount);
		for (uint32_t i = 0; i < body->vertexCount; ++i) {
			WireVertex wire{};
			std::memcpy(&wire, at + i * sizeof(WireVertex), sizeof(wire));
			// A non-finite coordinate would poison the vertex buffer and is never
			// something a working client sends.
			if (!finite(wire.x) || !finite(wire.y) || !finite(wire.u) || !finite(wire.v)) {
				++mStats.rejected;
				return false;
			}
			decoded.vertices[i] = GuiVertex{ { wire.x, wire.y }, { wire.u, wire.v }, wire.color };
		}
		at += vertexBytes;

		decoded.indices.resize(body->indexCount);
		if (indexBytes) std::memcpy(decoded.indices.data(), at, static_cast<size_t>(indexBytes));
		for (uint16_t index : decoded.indices) {
			// Every index must name a vertex that was actually sent, or the draw would
			// read past the buffer on the GPU.
			if (index >= body->vertexCount) { ++mStats.rejected; return false; }
		}
		at += indexBytes;

		decoded.commands.resize(body->commandCount);
		for (uint32_t i = 0; i < body->commandCount; ++i) {
			WireDrawCommand wire{};
			std::memcpy(&wire, at + i * sizeof(WireDrawCommand), sizeof(wire));
			// The run has to lie inside the index buffer; summed in 64-bit so a huge
			// offset plus a huge count cannot wrap to something that looks in range.
			const uint64_t end = static_cast<uint64_t>(wire.indexOffset) + wire.indexCount;
			if (end > body->indexCount) { ++mStats.rejected; return false; }
			if (!finite(wire.clipX) || !finite(wire.clipY) ||
			    !finite(wire.clipW) || !finite(wire.clipH) ||
			    wire.clipW < 0.0f || wire.clipH < 0.0f) {
				++mStats.rejected;
				return false;
			}
			GuiDrawCmd cmd{};
			// Back to (x0, y0, x1, y1), which is what the GUI and the renderer use.
			cmd.clip = { wire.clipX, wire.clipY, wire.clipX + wire.clipW, wire.clipY + wire.clipH };
			cmd.indexOffset = wire.indexOffset;
			cmd.indexCount = wire.indexCount;
			// A texture id is resolved against *this client's* table and nothing else. A
			// client naming an id it never uploaded is refused rather than quietly drawn
			// untextured: the id might be one a neighbour owns, and sampling another
			// application's pixels is how one would read another's screen.
			cmd.texture = nullptr;
			if (wire.textureId != 0) {
				if (wire.textureId > 0xFFFFFFFFull) { ++mStats.rejected; return false; }
				auto found = client.textures.find(static_cast<uint32_t>(wire.textureId));
				if (found == client.textures.end()) { ++mStats.rejected; return false; }
				cmd.texture = found->second;
			}
			decoded.commands[i] = cmd;
		}

		// Swapped in only once everything validated, so a rejected list cannot leave the
		// surface holding half of it.
		surface->draw = std::move(decoded);
		surface->drawVersion = body->drawVersion;
		surface->dirty = true;
		++mStats.drawLists;
		return true;
	}

	RuntimeHost::Surface* RuntimeHost::findSurface(uint32_t clientId, uint32_t surfaceId) {
		for (Surface& surface : mSurfaces) {
			if (surface.clientId == clientId && surface.surfaceId == surfaceId) return &surface;
		}
		return nullptr;
	}

	void RuntimeHost::clearDirty() {
		for (Surface& surface : mSurfaces) { surface.dirty = false; surface.presented = false; }
	}

	void RuntimeHost::dropClient(size_t index) {
		const uint32_t id = mClients[index]->id;
		// Its pixels go with it; nothing else can name them.
		releaseTextures(*mClients[index]);
		// A client's surfaces go with it; nothing else can address them.
		for (size_t i = mSurfaces.size(); i-- > 0;) {
			if (mSurfaces[i].clientId != id) continue;
			// The window goes with the surface; the runtime is told before the record is
			// dropped, while the ids are still meaningful.
			if (mOnSurfaceDestroyed) mOnSurfaceDestroyed(id, mSurfaces[i].surfaceId);
			mSurfaces.erase(mSurfaces.begin() + i);
		}
		mClients.erase(mClients.begin() + index);
		++mStats.dropped;
	}

	bool RuntimeHost::sendInput(uint32_t clientId, uint32_t surfaceId, InputKind kind,
	                            float x, float y, uint16_t modifiers, uint32_t codepoint) {
		for (auto& client : mClients) {
			if (client->id != clientId || !client->handshaken) continue;
			InputBody body{};
			body.surfaceId = surfaceId;
			body.kind = static_cast<uint16_t>(kind);
			body.modifiers = modifiers;
			body.x = x;
			body.y = y;
			body.codepoint = codepoint;
			return client->channel.send(MessageType::Input, &body, sizeof(body));
		}
		return false;
	}

	bool RuntimeHost::requestClose(uint32_t clientId, uint32_t surfaceId) {
		for (auto& client : mClients) {
			if (client->id != clientId || !client->handshaken) continue;
			CloseRequestBody body{};
			body.surfaceId = surfaceId;
			body.padding_ = 0;
			return client->channel.send(MessageType::CloseRequest, &body, sizeof(body));
		}
		return false;
	}
}
