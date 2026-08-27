#pragma once
#include <Runtime/Protocol.h>
#include <cstddef>
#include <string>
#include <vector>

// Moving Protocol.h messages between an application process and the runtime.
//
// A local named pipe in *byte* mode, framed by MessageHeader::size rather than by the
// pipe's own message boundaries. Byte mode means a read can return a partial frame, so
// this reassembles explicitly — which is also what makes the framing the single source
// of truth about where a message ends, instead of splitting that knowledge between our
// header and the OS.
//
// Nothing here knows what a draw list is. The transport moves bytes and enforces the
// envelope: magic, a payload within kMaxPayloadBytes, and a complete frame or nothing.
// A peer that sends garbage gets disconnected, never a partial message.
namespace RDA::Runtime {

	// A received message: the header plus its payload bytes.
	struct Message {
		MessageHeader header{};
		std::vector<uint8_t> payload;

		MessageType type() const { return static_cast<MessageType>(header.type); }
		// Reads the payload's leading struct, when it is big enough to hold one.
		template <typename T>
		const T* body() const {
			return payload.size() >= sizeof(T) ? reinterpret_cast<const T*>(payload.data()) : nullptr;
		}
		// Whatever follows that struct — the vertex/index/command block of a draw list.
		const uint8_t* tail(size_t bodySize) const {
			return payload.size() > bodySize ? payload.data() + bodySize : nullptr;
		}
		size_t tailSize(size_t bodySize) const {
			return payload.size() > bodySize ? payload.size() - bodySize : 0;
		}
	};

	// One connection. Move-only: it owns an OS handle.
	class Channel {
	public:
		Channel() = default;
		~Channel();
		Channel(Channel&&) noexcept;
		Channel& operator=(Channel&&) noexcept;
		Channel(const Channel&) = delete;
		Channel& operator=(const Channel&) = delete;

		// Connects to a runtime already listening on `name`. Waits up to timeoutMs for a
		// free pipe instance, since a busy runtime may have every instance in use.
		static Channel connect(const std::string& name, uint32_t timeoutMs = 2000);

		bool valid() const { return mHandle != nullptr; }
		void close();

		// Writes one framed message. `body` is the payload struct; `extra` is appended
		// after it, which is how a draw list carries its geometry without a second send
		// (and so cannot be torn between two frames by a reader).
		bool send(MessageType type, const void* body, uint32_t bodySize,
		          const void* extra = nullptr, uint32_t extraSize = 0);

		// Reads exactly one message. Returns false on disconnect or a malformed frame,
		// after which the channel is closed — a stream that has lost framing cannot be
		// resynchronised, so continuing to read it would be guessing.
		bool receive(Message& out);

		// True when at least one byte is waiting, so a caller can drain without blocking.
		//
		// Also how a silent death is noticed: a peer that exits without saying goodbye
		// leaves nothing to read, so a caller that only ever asks "is there data" would
		// wait for ever on a process that is gone. The peek distinguishes "nothing yet"
		// from "the pipe is broken" and closes the channel in the second case, which
		// makes valid() the single answer to whether this connection is still worth
		// anything.
		bool pending();

		uint32_t nextSequence() { return ++mSequence; }

	private:
		// Listener is the only other thing that may wrap a raw handle, so an OS handle
		// can enter a Channel from exactly two places: connect() and accept().
		friend class Listener;
		explicit Channel(void* handle) : mHandle(handle) {}
		bool writeAll(const void* data, uint32_t size);
		bool readAll(void* data, uint32_t size);

		void*    mHandle = nullptr;
		uint32_t mSequence = 0;
	};

	// The runtime side: accepts client connections on a named pipe.
	class Listener {
	public:
		~Listener();
		// Creates the pipe and leaves an instance waiting. It must exist before this
		// returns: a caller that signals "the runtime is up" and only then creates the
		// pipe leaves a window in which a client's connect fails outright — and the
		// server is left blocking on a connection that already came and went.
		bool open(const std::string& name);
		// Blocks until a client connects to the waiting instance, then immediately opens
		// the next one, so a second client never queues behind the first.
		Channel accept();
		void close();
		const std::string& name() const { return mName; }
		bool listening() const { return mPending != nullptr; }

	private:
		bool createInstance();

		std::string mName;
		void* mPending = nullptr; // the instance the next client will land on
	};

	// The default endpoint. A per-user name would be the next step; one runtime per
	// machine is a placeholder, not a decision.
	inline std::string defaultEndpoint() { return "rendeer-runtime"; }
}
