// Tests for the runtime's host half.
//
// Host.h opens by saying this is "the half that has no GPU in it… testable without a
// device". This is that test. It links the two runtime sources and nothing else — no
// engine library, no Vulkan, no GLFW — so it runs anywhere, including a machine with no
// graphics driver at all.
//
// What is under test is the part that reads bytes from another process. Everything a
// client sends is untrusted by design, and almost every case here is an attempt to make
// the host believe something that is not true: counts that disagree with the payload,
// indices past the end of the buffer they name, sizes that overflow when multiplied, and
// one client reaching for another client's texture. The happy paths are here too, but
// they are the minority on purpose — a client that behaves is not the one that breaks a
// runtime serving nine other applications.
//
// The host is driven over a real named pipe rather than through a test seam. That keeps
// the framing in Transport.cpp inside the tested path (it is where a malformed length is
// first refused) and means nothing in the production code exists only for tests.
#include "TestHarness.h"

#include <Runtime/Host.h>
#include <Runtime/Transport.h>
#include <Runtime/Protocol.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace RDA::Runtime;

namespace {

	// ---- limits mirrored from Host.cpp ------------------------------------------------
	// They live in an anonymous namespace there, so they are restated rather than shared:
	// a test that imported them would agree with the implementation by construction and
	// never notice one of them changing.
	constexpr uint32_t kHostMaxSurfaceDim = 16384;

	// ---- plumbing ---------------------------------------------------------------------

	// Every fixture gets its own endpoint. Tests would otherwise land on each other's
	// hosts, and on a developer machine they would land on the real runtime.
	std::string uniqueEndpoint() {
		static std::atomic<uint32_t> counter{ 0 };
		return "rendeer-test-" + std::to_string(::GetCurrentProcessId()) + "-" +
		       std::to_string(counter.fetch_add(1));
	}

	// A running host with handlers that need no device.
	struct Fixture {
		RuntimeHost host;
		std::string endpoint;
		// Handed out in place of real images. The host only ever stores these and hands
		// them back in a decoded draw command; it never dereferences one, which is exactly
		// why it can be tested without a device.
		std::atomic<uintptr_t> nextFake{ 0x1000 };
		int destroyed = 0;

		Fixture() {
			endpoint = uniqueEndpoint();
			host.setTextureHandlers(
				[this](uint32_t, uint32_t, const uint8_t*) -> const RDA::Texture* {
					return reinterpret_cast<const RDA::Texture*>(nextFake.fetch_add(0x10));
				},
				[this](const RDA::Texture*) { ++destroyed; });
			CHECK(host.start(endpoint));
		}
		~Fixture() { host.stop(); }
	};

	// Runs the host until `done` holds. The host is polled from this thread, so a reply
	// is on the wire by the time poll() returns and nothing here races it.
	template <typename F>
	bool pumpUntil(RuntimeHost& host, F done, int timeoutMs = 3000) {
		const auto deadline = std::chrono::steady_clock::now() +
		                      std::chrono::milliseconds(timeoutMs);
		for (;;) {
			host.poll();
			if (done()) return true;
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// Connects a raw channel and waits until the host has taken it off the accept thread.
	bool attach(Fixture& fx, Channel& out) {
		const size_t before = fx.host.clientCount();
		out = Channel::connect(fx.endpoint);
		if (!out.valid()) return false;
		return pumpUntil(fx.host, [&] { return fx.host.clientCount() > before; });
	}

	bool sendHello(Channel& channel, uint16_t major = kProtocolMajor) {
		HelloBody hello{};
		hello.major = major;
		hello.minor = kProtocolMinor;
		hello.clientPid = 0;
		hello.featureFlags = 0;
		return channel.send(MessageType::Hello, &hello, sizeof(hello));
	}

	// Pumps until a message is waiting, then reads it. False on timeout, or if the host
	// dropped the connection instead of answering.
	bool nextMessage(Fixture& fx, Channel& channel, Message& out) {
		if (!pumpUntil(fx.host, [&] { return !channel.valid() || channel.pending(); })) return false;
		if (!channel.valid()) return false;
		return channel.receive(out);
	}

	// Connects and completes the handshake, leaving a client the host will serve.
	bool handshake(Fixture& fx, Channel& out) {
		if (!attach(fx, out)) return false;
		if (!sendHello(out)) return false;
		Message reply;
		if (!nextMessage(fx, out, reply)) return false;
		return reply.type() == MessageType::Welcome;
	}

	bool clientWasDropped(Fixture& fx, size_t expectedRemaining = 0) {
		return pumpUntil(fx.host, [&] { return fx.host.clientCount() == expectedRemaining; });
	}

	// ---- message builders ---------------------------------------------------------------

	bool createSurface(Channel& channel, uint32_t id, uint32_t w, uint32_t h) {
		SurfaceCreateBody body{};
		body.surfaceId = id;
		body.width = w;
		body.height = h;
		body.flags = 0;
		return channel.send(MessageType::SurfaceCreate, &body, sizeof(body));
	}

	// A client with one surface, ready to be sent geometry.
	bool handshakeWithSurface(Fixture& fx, Channel& out, uint32_t surfaceId = 1) {
		if (!handshake(fx, out)) return false;
		if (!createSurface(out, surfaceId, 640, 480)) return false;
		// FontMetrics comes first, then SurfaceReady.
		Message msg;
		if (!nextMessage(fx, out, msg) || msg.type() != MessageType::FontMetrics) return false;
		if (!nextMessage(fx, out, msg) || msg.type() != MessageType::SurfaceReady) return false;
		return true;
	}

	std::vector<uint8_t> packTail(const std::vector<WireVertex>& vertices,
	                              const std::vector<uint16_t>& indices,
	                              const std::vector<WireDrawCommand>& commands) {
		std::vector<uint8_t> tail;
		tail.resize(vertices.size() * sizeof(WireVertex) +
		            indices.size() * sizeof(uint16_t) +
		            commands.size() * sizeof(WireDrawCommand));
		uint8_t* at = tail.data();
		if (!vertices.empty()) {
			std::memcpy(at, vertices.data(), vertices.size() * sizeof(WireVertex));
			at += vertices.size() * sizeof(WireVertex);
		}
		if (!indices.empty()) {
			std::memcpy(at, indices.data(), indices.size() * sizeof(uint16_t));
			at += indices.size() * sizeof(uint16_t);
		}
		if (!commands.empty()) {
			std::memcpy(at, commands.data(), commands.size() * sizeof(WireDrawCommand));
		}
		return tail;
	}

	// Sends a draw list whose declared counts match what is actually attached.
	bool sendDrawList(Channel& channel, uint32_t surfaceId,
	                  const std::vector<WireVertex>& vertices,
	                  const std::vector<uint16_t>& indices,
	                  const std::vector<WireDrawCommand>& commands,
	                  uint64_t drawVersion = 1) {
		DrawListBody body{};
		body.surfaceId = surfaceId;
		body.vertexCount = static_cast<uint32_t>(vertices.size());
		body.indexCount = static_cast<uint32_t>(indices.size());
		body.commandCount = static_cast<uint32_t>(commands.size());
		body.drawVersion = drawVersion;
		const std::vector<uint8_t> tail = packTail(vertices, indices, commands);
		return channel.send(MessageType::DrawList, &body, sizeof(body),
		                    tail.data(), static_cast<uint32_t>(tail.size()));
	}

	WireVertex vertexAt(float x, float y) {
		WireVertex v{};
		v.x = x; v.y = y;
		v.u = 0.0f; v.v = 0.0f;
		v.color = 0xFFFFFFFFu;
		v.padding_ = 0;
		return v;
	}

	WireDrawCommand commandOver(uint32_t indexOffset, uint32_t indexCount, uint64_t textureId = 0) {
		WireDrawCommand c{};
		c.indexOffset = indexOffset;
		c.indexCount = indexCount;
		c.clipX = 0.0f; c.clipY = 0.0f;
		c.clipW = 640.0f; c.clipH = 480.0f;
		c.textureId = textureId;
		return c;
	}

	// A triangle: the smallest thing the host will accept.
	std::vector<WireVertex> triangleVertices() {
		return { vertexAt(0.0f, 0.0f), vertexAt(10.0f, 0.0f), vertexAt(0.0f, 10.0f) };
	}
	std::vector<uint16_t> triangleIndices() { return { 0, 1, 2 }; }

	// Uploads pixels and waits for the runtime to confirm them.
	bool uploadTexture(Fixture& fx, Channel& channel, uint32_t textureId,
	                   uint32_t w = 4, uint32_t h = 4) {
		TextureCreateBody body{};
		body.textureId = textureId;
		body.width = w;
		body.height = h;
		body.format = 0;
		const std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4, 0xFF);
		if (!channel.send(MessageType::TextureCreate, &body, sizeof(body),
		                  pixels.data(), static_cast<uint32_t>(pixels.size()))) {
			return false;
		}
		Message reply;
		if (!nextMessage(fx, channel, reply) || reply.type() != MessageType::TextureReady) return false;
		const TextureReadyBody* ready = reply.body<TextureReadyBody>();
		return ready && ready->ok == 1;
	}
}

// ======================= handshake =======================

TEST(handshake_accepts_a_matching_version) {
	Fixture fx;
	Channel client;
	CHECK(attach(fx, client));
	CHECK(sendHello(client));

	Message reply;
	CHECK(nextMessage(fx, client, reply));
	CHECK(reply.type() == MessageType::Welcome);
	const WelcomeBody* welcome = reply.body<WelcomeBody>();
	CHECK(welcome != nullptr);
	if (welcome) {
		CHECK_EQ(welcome->major, kProtocolMajor);
		CHECK(welcome->clientId != 0);
	}
	CHECK_EQ(fx.host.stats().accepted, 1ull);
	CHECK_EQ(fx.host.clientCount(), size_t(1));
}

TEST(handshake_refuses_a_different_major_and_says_why) {
	Fixture fx;
	Channel client;
	CHECK(attach(fx, client));
	CHECK(sendHello(client, kProtocolMajor + 1));

	Message reply;
	CHECK(nextMessage(fx, client, reply));
	CHECK(reply.type() == MessageType::Refused);
	const RefusedBody* refused = reply.body<RefusedBody>();
	CHECK(refused != nullptr);
	if (refused) {
		CHECK_EQ(refused->reason, static_cast<uint32_t>(RefuseReason::VersionMismatch));
		// The client is told what the runtime actually speaks, so it can report the skew
		// rather than just failing.
		CHECK_EQ(refused->runtimeMajor, kProtocolMajor);
	}
	CHECK_EQ(fx.host.stats().refused, 1ull);
	CHECK(clientWasDropped(fx));
}

TEST(anything_before_hello_drops_the_client) {
	Fixture fx;
	Channel client;
	CHECK(attach(fx, client));
	// A surface request from a peer whose protocol version is still unknown.
	CHECK(createSurface(client, 1, 640, 480));

	CHECK(clientWasDropped(fx));
	CHECK(fx.host.stats().rejected >= 1ull);
	CHECK_EQ(fx.host.surfaces().size(), size_t(0));
}

TEST(a_second_hello_drops_the_client) {
	Fixture fx;
	Channel client;
	CHECK(handshake(fx, client));
	CHECK(sendHello(client));

	CHECK(clientWasDropped(fx));
}

// ======================= framing =======================

// These write to the pipe directly. Channel::send always produces a well-formed frame,
// so a malformed one cannot be built with it — and malformed framing is precisely what
// the transport exists to refuse.
namespace {
	HANDLE openRaw(const std::string& endpoint) {
		const std::string path = "\\\\.\\pipe\\" + endpoint;
		return ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
		                     OPEN_EXISTING, 0, nullptr);
	}
	bool writeRaw(HANDLE h, const void* data, uint32_t size) {
		DWORD wrote = 0;
		return ::WriteFile(h, data, size, &wrote, nullptr) && wrote == size;
	}
}

TEST(a_stream_that_does_not_start_with_the_magic_is_dropped) {
	Fixture fx;
	HANDLE raw = openRaw(fx.endpoint);
	CHECK(raw != INVALID_HANDLE_VALUE);
	if (raw == INVALID_HANDLE_VALUE) return;
	CHECK(pumpUntil(fx.host, [&] { return fx.host.clientCount() == 1; }));

	MessageHeader header{};
	header.magic = 0xDEADBEEF; // not kMagic
	header.type = static_cast<uint16_t>(MessageType::Hello);
	header.size = 0;
	CHECK(writeRaw(raw, &header, sizeof(header)));

	CHECK(clientWasDropped(fx));
	::CloseHandle(raw);
}

TEST(a_payload_larger_than_the_cap_is_dropped_without_allocating_it) {
	Fixture fx;
	HANDLE raw = openRaw(fx.endpoint);
	CHECK(raw != INVALID_HANDLE_VALUE);
	if (raw == INVALID_HANDLE_VALUE) return;
	CHECK(pumpUntil(fx.host, [&] { return fx.host.clientCount() == 1; }));

	// A declared length far past kMaxPayloadBytes, with nothing behind it. The host must
	// refuse on the header alone: believing it would mean resizing a buffer to 4GB on a
	// number a stranger chose.
	MessageHeader header{};
	header.magic = kMagic;
	header.type = static_cast<uint16_t>(MessageType::DrawList);
	header.size = 0xFFFFFFF0u;
	CHECK(writeRaw(raw, &header, sizeof(header)));

	CHECK(clientWasDropped(fx));
	::CloseHandle(raw);
}

// ======================= surfaces =======================

TEST(a_surface_is_granted_and_confirmed) {
	Fixture fx;
	Channel client;
	CHECK(handshake(fx, client));
	CHECK(createSurface(client, 7, 800, 600));

	Message msg;
	// Metrics arrive first, deliberately: a client that acts the moment its surface is
	// ready must already be able to measure text.
	CHECK(nextMessage(fx, client, msg));
	CHECK(msg.type() == MessageType::FontMetrics);
	CHECK(nextMessage(fx, client, msg));
	CHECK(msg.type() == MessageType::SurfaceReady);

	const SurfaceReadyBody* ready = msg.body<SurfaceReadyBody>();
	CHECK(ready != nullptr);
	if (ready) {
		CHECK_EQ(ready->surfaceId, 7u);
		CHECK_EQ(ready->width, 800u);
		CHECK_EQ(ready->height, 600u);
	}
	CHECK_EQ(fx.host.surfaces().size(), size_t(1));
}

TEST(the_size_reported_back_is_what_was_granted_not_what_was_asked) {
	Fixture fx;
	// A window manager that has its own opinion. The client must lay out against the
	// answer, so the answer has to be what travels.
	fx.host.setSurfaceHandlers(
		[](const RuntimeHost::Surface&, uint32_t& w, uint32_t& h) {
			w = 320; h = 240;
			return true;
		},
		[](uint32_t, uint32_t) {});

	Channel client;
	CHECK(handshake(fx, client));
	CHECK(createSurface(client, 1, 800, 600));

	Message msg;
	CHECK(nextMessage(fx, client, msg)); // FontMetrics
	CHECK(nextMessage(fx, client, msg));
	CHECK(msg.type() == MessageType::SurfaceReady);
	const SurfaceReadyBody* ready = msg.body<SurfaceReadyBody>();
	CHECK(ready != nullptr);
	if (ready) {
		CHECK_EQ(ready->width, 320u);
		CHECK_EQ(ready->height, 240u);
	}
}

TEST(a_refused_surface_drops_the_client_and_leaves_nothing_behind) {
	Fixture fx;
	fx.host.setSurfaceHandlers(
		[](const RuntimeHost::Surface&, uint32_t&, uint32_t&) { return false; },
		[](uint32_t, uint32_t) {});

	Channel client;
	CHECK(handshake(fx, client));
	CHECK(createSurface(client, 1, 640, 480));

	CHECK(clientWasDropped(fx));
	// The half-built surface must not survive the refusal.
	CHECK_EQ(fx.host.surfaces().size(), size_t(0));
}

TEST(a_surface_of_zero_or_absurd_size_is_refused) {
	{
		Fixture fx;
		Channel client;
		CHECK(handshake(fx, client));
		CHECK(createSurface(client, 1, 0, 480));
		CHECK(clientWasDropped(fx));
		CHECK(fx.host.stats().rejected >= 1ull);
	}
	{
		Fixture fx;
		Channel client;
		CHECK(handshake(fx, client));
		CHECK(createSurface(client, 1, kHostMaxSurfaceDim + 1, 480));
		CHECK(clientWasDropped(fx));
	}
}

TEST(the_same_surface_id_cannot_be_created_twice) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));
	CHECK(createSurface(client, 1, 320, 200));

	CHECK(clientWasDropped(fx));
}

TEST(resizing_a_surface_that_does_not_exist_drops_the_client) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	SurfaceResizeBody body{};
	body.surfaceId = 99; // never created
	body.width = 100;
	body.height = 100;
	body.padding_ = 0;
	CHECK(client.send(MessageType::SurfaceResize, &body, sizeof(body)));

	CHECK(clientWasDropped(fx));
}

TEST(surface_ids_are_scoped_to_a_connection) {
	Fixture fx;
	Channel a, b;
	CHECK(handshakeWithSurface(fx, a, 1));
	CHECK(handshakeWithSurface(fx, b, 1)); // the same id, a different client

	// Two surfaces, not one overwritten: an id means nothing outside the connection that
	// chose it.
	CHECK_EQ(fx.host.surfaces().size(), size_t(2));
	CHECK_EQ(fx.host.clientCount(), size_t(2));
}

// ======================= draw lists =======================

TEST(a_valid_draw_list_is_decoded_and_marks_the_surface_dirty) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0, 3) }, 42));
	CHECK(pumpUntil(fx.host, [&] { return fx.host.stats().drawLists == 1; }));

	const RuntimeHost::Surface* surface = fx.host.findSurface(1, 1);
	CHECK(surface != nullptr);
	if (surface) {
		CHECK_EQ(surface->draw.vertices.size(), size_t(3));
		CHECK_EQ(surface->draw.indices.size(), size_t(3));
		CHECK_EQ(surface->draw.commands.size(), size_t(1));
		CHECK_EQ(surface->drawVersion, 42ull);
		CHECK(surface->dirty);
		// The wire carries origin+extent; the renderer wants corners.
		CHECK_EQ(surface->draw.commands[0].clip.z, 640.0f);
		CHECK_EQ(surface->draw.commands[0].clip.w, 480.0f);
		// An untextured batch stays untextured rather than picking something up.
		CHECK(surface->draw.commands[0].texture == nullptr);
	}
	CHECK_EQ(fx.host.clientCount(), size_t(1));
}

TEST(counts_that_disagree_with_the_payload_are_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	// Claims three vertices, attaches one.
	DrawListBody body{};
	body.surfaceId = 1;
	body.vertexCount = 3;
	body.indexCount = 0;
	body.commandCount = 0;
	body.drawVersion = 1;
	const WireVertex one = vertexAt(0.0f, 0.0f);
	CHECK(client.send(MessageType::DrawList, &body, sizeof(body), &one, sizeof(one)));

	CHECK(clientWasDropped(fx));
	CHECK_EQ(fx.host.stats().drawLists, 0ull);
}

TEST(a_vertex_count_that_overflows_32_bits_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	// 178956971 * sizeof(WireVertex) is 0x1'00000008 — eight bytes once it has wrapped
	// through 32 bits. A host that multiplied in 32-bit arithmetic would look at the eight
	// bytes attached here, agree, and then resize a vector to 178 million vertices.
	static_assert(sizeof(WireVertex) == 24, "the overflow constant below assumes this");
	constexpr uint32_t kWrapping = 178956971u;
	CHECK_EQ(static_cast<uint32_t>(kWrapping * sizeof(WireVertex)), 8u);

	DrawListBody body{};
	body.surfaceId = 1;
	body.vertexCount = kWrapping;
	body.indexCount = 0;
	body.commandCount = 0;
	body.drawVersion = 1;
	const uint8_t eightBytes[8] = {};
	CHECK(client.send(MessageType::DrawList, &body, sizeof(body), eightBytes, sizeof(eightBytes)));

	CHECK(clientWasDropped(fx));
	CHECK_EQ(fx.host.stats().drawLists, 0ull);
}

TEST(an_index_past_the_last_vertex_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	// Three vertices, an index naming the fourth. On the GPU this reads past the buffer.
	CHECK(sendDrawList(client, 1, triangleVertices(), { 0, 1, 3 }, { commandOver(0, 3) }));

	CHECK(clientWasDropped(fx));
	CHECK_EQ(fx.host.stats().drawLists, 0ull);
}

TEST(a_command_reaching_past_the_index_buffer_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	// Three indices exist; the command asks to draw six.
	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(), { commandOver(0, 6) }));

	CHECK(clientWasDropped(fx));
}

TEST(a_command_whose_range_wraps_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	// offset + count overflows 32 bits back into range. Summed in 64-bit it does not.
	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0xFFFFFFF0u, 0x20u) }));

	CHECK(clientWasDropped(fx));
}

TEST(a_non_finite_vertex_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	std::vector<WireVertex> vertices = triangleVertices();
	vertices[1].x = std::numeric_limits<float>::quiet_NaN();
	CHECK(sendDrawList(client, 1, vertices, triangleIndices(), { commandOver(0, 3) }));

	CHECK(clientWasDropped(fx));
	CHECK_EQ(fx.host.stats().drawLists, 0ull);
}

TEST(an_infinite_clip_rectangle_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	WireDrawCommand command = commandOver(0, 3);
	command.clipW = std::numeric_limits<float>::infinity();
	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(), { command }));

	CHECK(clientWasDropped(fx));
}

TEST(a_negative_clip_rectangle_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	WireDrawCommand command = commandOver(0, 3);
	command.clipH = -10.0f;
	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(), { command }));

	CHECK(clientWasDropped(fx));
}

TEST(a_rejected_draw_list_does_not_half_replace_the_last_good_one) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0, 3) }, 7));
	CHECK(pumpUntil(fx.host, [&] { return fx.host.stats().drawLists == 1; }));

	// Six vertices this time, but an index past the end of them.
	std::vector<WireVertex> more = triangleVertices();
	more.push_back(vertexAt(20.0f, 20.0f));
	CHECK(sendDrawList(client, 1, more, { 0, 1, 9 }, { commandOver(0, 3) }, 8));
	CHECK(clientWasDropped(fx));

	// The client is gone, and its surface with it — but the count is what matters: the
	// bad list was never counted as decoded.
	CHECK_EQ(fx.host.stats().drawLists, 1ull);
}

TEST(a_draw_list_for_a_surface_that_does_not_exist_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	CHECK(sendDrawList(client, 55, triangleVertices(), triangleIndices(), { commandOver(0, 3) }));

	CHECK(clientWasDropped(fx));
}

// ======================= textures and isolation =======================

TEST(a_texture_is_accepted_and_can_be_drawn_with) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));
	CHECK(uploadTexture(fx, client, 1));

	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0, 3, /*textureId*/ 1) }));
	CHECK(pumpUntil(fx.host, [&] { return fx.host.stats().drawLists == 1; }));

	const RuntimeHost::Surface* surface = fx.host.findSurface(1, 1);
	CHECK(surface != nullptr);
	if (surface) {
		// Resolved to whatever the runtime handed back for those pixels — never to the
		// number the client wrote on the wire.
		CHECK(surface->draw.commands[0].texture != nullptr);
	}
	CHECK_EQ(fx.host.clientCount(), size_t(1));
}

TEST(pixels_that_do_not_match_the_declared_size_are_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	TextureCreateBody body{};
	body.textureId = 1;
	body.width = 64;
	body.height = 64; // 16384 bytes of pixels declared
	body.format = 0;
	const std::vector<uint8_t> tooFew(16, 0xFF);
	CHECK(client.send(MessageType::TextureCreate, &body, sizeof(body),
	                  tooFew.data(), static_cast<uint32_t>(tooFew.size())));

	CHECK(clientWasDropped(fx));
}

TEST(reusing_a_texture_id_is_refused_rather_than_stranding_the_old_one) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));
	CHECK(uploadTexture(fx, client, 1));

	TextureCreateBody body{};
	body.textureId = 1; // already taken
	body.width = 4;
	body.height = 4;
	body.format = 0;
	const std::vector<uint8_t> pixels(4 * 4 * 4, 0x80);
	CHECK(client.send(MessageType::TextureCreate, &body, sizeof(body),
	                  pixels.data(), static_cast<uint32_t>(pixels.size())));

	CHECK(clientWasDropped(fx));
}

TEST(drawing_with_a_texture_that_was_never_uploaded_is_refused) {
	Fixture fx;
	Channel client;
	CHECK(handshakeWithSurface(fx, client, 1));

	// Never uploaded anything, but names texture 1 anyway.
	CHECK(sendDrawList(client, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0, 3, /*textureId*/ 1) }));

	CHECK(clientWasDropped(fx));
	CHECK_EQ(fx.host.stats().drawLists, 0ull);
}

// The isolation guarantee, stated as a test.
//
// Two applications, each with a texture it uploaded itself. The second names the first's
// id. Sampling a neighbour's pixels is how one application would read another's screen,
// so the only acceptable answer is to refuse — and to leave the first client untouched
// while doing it.
TEST(one_client_cannot_name_another_clients_texture) {
	Fixture fx;
	Channel a, b;
	CHECK(handshakeWithSurface(fx, a, 1));
	CHECK(uploadTexture(fx, a, 1));

	CHECK(handshakeWithSurface(fx, b, 1));
	// B never uploaded anything. Id 1 exists — it belongs to A.
	CHECK(sendDrawList(b, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0, 3, /*textureId*/ 1) }));

	// B is dropped; A carries on.
	CHECK(pumpUntil(fx.host, [&] { return fx.host.clientCount() == 1; }));
	CHECK(a.valid());
	CHECK(fx.host.findSurface(1, 1) != nullptr);
	CHECK_EQ(fx.host.stats().drawLists, 0ull);
}

TEST(the_same_texture_id_in_two_clients_is_two_textures) {
	Fixture fx;
	Channel a, b;
	CHECK(handshakeWithSurface(fx, a, 1));
	CHECK(handshakeWithSurface(fx, b, 1));
	CHECK(uploadTexture(fx, a, 1));
	CHECK(uploadTexture(fx, b, 1));

	CHECK(sendDrawList(a, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0, 3, 1) }, 1));
	CHECK(sendDrawList(b, 1, triangleVertices(), triangleIndices(),
	                   { commandOver(0, 3, 1) }, 1));
	CHECK(pumpUntil(fx.host, [&] { return fx.host.stats().drawLists == 2; }));

	const RuntimeHost::Surface* surfaceA = fx.host.findSurface(1, 1);
	const RuntimeHost::Surface* surfaceB = fx.host.findSurface(2, 1);
	CHECK(surfaceA != nullptr);
	CHECK(surfaceB != nullptr);
	if (surfaceA && surfaceB) {
		// Same id on the wire, different images behind it.
		CHECK(surfaceA->draw.commands[0].texture != nullptr);
		CHECK(surfaceB->draw.commands[0].texture != nullptr);
		CHECK(surfaceA->draw.commands[0].texture != surfaceB->draw.commands[0].texture);
	}
}

TEST(a_departing_client_takes_its_textures_with_it) {
	Fixture fx;
	{
		Channel client;
		CHECK(handshakeWithSurface(fx, client, 1));
		CHECK(uploadTexture(fx, client, 1));
		CHECK(uploadTexture(fx, client, 2));
		CHECK_EQ(fx.destroyed, 0);
		client.close(); // the process went away without a Goodbye
	}
	CHECK(clientWasDropped(fx));
	// Nothing else can name them, so nothing else could ever free them.
	CHECK_EQ(fx.destroyed, 2);
	CHECK_EQ(fx.host.surfaces().size(), size_t(0));
}

// ======================= protocol invariants =======================

TEST(the_wire_structs_have_not_changed_shape) {
	// Protocol.h's own rule: append, never reorder or resize. A client built against one
	// version of this file meets a runtime built against another, so a field that quietly
	// moved is read as a different field entirely.
	CHECK_EQ(sizeof(MessageHeader), size_t(16));
	CHECK_EQ(sizeof(WireVertex), size_t(24));
	CHECK_EQ(sizeof(WireGlyph), size_t(20));
	CHECK_EQ(sizeof(WireDrawCommand), size_t(32));
	CHECK_EQ(sizeof(DrawListBody), size_t(24));
	CHECK_EQ(sizeof(HelloBody), size_t(16));
	CHECK_EQ(sizeof(WelcomeBody), size_t(8));
}

TEST(the_largest_allowed_texture_fits_in_one_message) {
	// Protocol.h says of kMaxTextureDim: "The most pixels one message may carry, well
	// inside kMaxPayloadBytes so the header and the image together cannot exceed it."
	//
	// Hold it to that. If this fails, a client can ask for a texture the protocol permits
	// and Channel::send will refuse it locally — the upload fails with nothing on the wire
	// and no explanation, which is the worst of both answers.
	const uint64_t largest = static_cast<uint64_t>(kMaxTextureDim) * kMaxTextureDim * 4ull
	                       + sizeof(TextureCreateBody);
	CHECK(largest <= kMaxPayloadBytes);
}

int main() {
	std::printf("runtime host tests\n\n");
	return test::runAll();
}
