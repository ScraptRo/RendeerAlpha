#include <Runtime/Transport.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace RDA::Runtime {

	namespace {
		// \\.\pipe\<name>. Kept in one place so both ends cannot disagree about it.
		std::string pipePath(const std::string& name) { return "\\\\.\\pipe\\" + name; }

		constexpr uint32_t kBufferBytes = 64 * 1024;
	}

	// ---- Channel ---------------------------------------------------------------------
	Channel::~Channel() { close(); }

	Channel::Channel(Channel&& other) noexcept
		: mHandle(other.mHandle), mSequence(other.mSequence) {
		other.mHandle = nullptr;
	}

	Channel& Channel::operator=(Channel&& other) noexcept {
		if (this != &other) {
			close();
			mHandle = other.mHandle;
			mSequence = other.mSequence;
			other.mHandle = nullptr;
		}
		return *this;
	}

	void Channel::close() {
		if (mHandle) {
			::CloseHandle(static_cast<HANDLE>(mHandle));
			mHandle = nullptr;
		}
	}

	Channel Channel::connect(const std::string& name, uint32_t timeoutMs) {
		const std::string path = pipePath(name);
		// A pipe that exists but has every instance busy reports ERROR_PIPE_BUSY rather
		// than failing outright, so that case is a wait rather than an error.
		for (;;) {
			HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
			                         OPEN_EXISTING, 0, nullptr);
			if (h != INVALID_HANDLE_VALUE) return Channel(h);
			if (::GetLastError() != ERROR_PIPE_BUSY) return Channel();
			if (!::WaitNamedPipeA(path.c_str(), timeoutMs)) return Channel();
		}
	}

	bool Channel::writeAll(const void* data, uint32_t size) {
		const uint8_t* at = static_cast<const uint8_t*>(data);
		uint32_t done = 0;
		while (done < size) {
			DWORD wrote = 0;
			if (!::WriteFile(static_cast<HANDLE>(mHandle), at + done, size - done, &wrote, nullptr)
			    || wrote == 0) {
				return false;
			}
			done += wrote;
		}
		return true;
	}

	bool Channel::readAll(void* data, uint32_t size) {
		uint8_t* at = static_cast<uint8_t*>(data);
		uint32_t done = 0;
		while (done < size) {
			DWORD got = 0;
			if (!::ReadFile(static_cast<HANDLE>(mHandle), at + done, size - done, &got, nullptr)
			    || got == 0) {
				return false; // disconnected, or the peer died mid-frame
			}
			done += got;
		}
		return true;
	}

	bool Channel::send(MessageType type, const void* body, uint32_t bodySize,
	                   const void* extra, uint32_t extraSize) {
		if (!valid()) return false;
		const uint64_t total = static_cast<uint64_t>(bodySize) + extraSize;
		if (total > kMaxPayloadBytes) return false; // refuse to send what a peer must reject

		MessageHeader header{};
		header.magic = kMagic;
		header.type = static_cast<uint16_t>(type);
		header.flags = 0;
		header.size = static_cast<uint32_t>(total);
		header.sequence = nextSequence();

		// Header, body and tail go out back to back. A reader frames on size, so a torn
		// write would desynchronise the stream — hence the failure path closes it.
		if (!writeAll(&header, sizeof(header))) { close(); return false; }
		if (bodySize && !writeAll(body, bodySize)) { close(); return false; }
		if (extraSize && !writeAll(extra, extraSize)) { close(); return false; }
		return true;
	}

	bool Channel::receive(Message& out) {
		if (!valid()) return false;

		MessageHeader header{};
		if (!readAll(&header, sizeof(header))) { close(); return false; }

		// Anything wrong with the envelope means the stream is no longer trustworthy.
		// There is no resynchronising a byte stream whose framing is in doubt, so the
		// only safe response is to stop reading it.
		if (header.magic != kMagic || header.size > kMaxPayloadBytes) { close(); return false; }

		out.header = header;
		out.payload.clear();
		if (header.size) {
			out.payload.resize(header.size);
			if (!readAll(out.payload.data(), header.size)) { close(); return false; }
		}
		return true;
	}

	bool Channel::pending() {
		if (!mHandle) return false;
		DWORD available = 0;
		if (!::PeekNamedPipe(static_cast<HANDLE>(mHandle), nullptr, 0, nullptr, &available, nullptr)) {
			// The peek failed, which for a pipe means the other end is gone rather than
			// merely quiet. Closing here is what turns a vanished process into a dropped
			// client: a peer that exits without a Goodbye sends nothing, so waiting for
			// data would leave its windows and textures behind for ever.
			close();
			return false;
		}
		return available > 0;
	}

	// ---- Listener --------------------------------------------------------------------
	Listener::~Listener() { close(); }

	bool Listener::createInstance() {
		const std::string path = pipePath(mName);
		HANDLE h = ::CreateNamedPipeA(path.c_str(),
			PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			kBufferBytes, kBufferBytes, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE) return false;
		mPending = h;
		return true;
	}

	bool Listener::open(const std::string& name) {
		close();
		mName = name;
		// The instance is created here, not in accept(): once this returns the endpoint
		// is genuinely connectable, so a client racing the runtime's startup waits on a
		// busy pipe instead of failing to find one.
		return createInstance();
	}

	void Listener::close() {
		if (mPending) {
			::CloseHandle(static_cast<HANDLE>(mPending));
			mPending = nullptr;
		}
	}

	Channel Listener::accept() {
		if (!mPending) return Channel();
		HANDLE h = static_cast<HANDLE>(mPending);
		mPending = nullptr;

		// A client that connected between the instance being created and this call
		// reports ERROR_PIPE_CONNECTED, which is success with the connection already made.
		if (!::ConnectNamedPipe(h, nullptr) && ::GetLastError() != ERROR_PIPE_CONNECTED) {
			::CloseHandle(h);
			createInstance(); // keep the endpoint alive for the next client
			return Channel();
		}
		// Opened before handing this one back, so the endpoint is never momentarily
		// absent — a client connecting right now waits rather than failing.
		createInstance();
		return Channel(h);
	}
}
