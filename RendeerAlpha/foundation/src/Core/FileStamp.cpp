#include <Core/FileStamp.h>

#include <filesystem>
#include <system_error>
#include <utility>

namespace RDA {

	int64_t fileStamp(const std::string& path) {
		if (path.empty()) return 0;
		std::error_code ec;
		const auto written = std::filesystem::last_write_time(path, ec);
		if (ec) return 0;
		return written.time_since_epoch().count();
	}

	int64_t fileStamps(const std::vector<std::string>& paths) {
		// Unsigned, so that the wrap-around is defined rather than something the compiler
		// may assume never happens; the bits are the ones the signed product would have had.
		uint64_t combined = static_cast<uint64_t>(paths.size());
		for (const std::string& path : paths) {
			combined = combined * 31 + static_cast<uint64_t>(fileStamp(path));
		}
		return static_cast<int64_t>(combined);
	}

	void FileWatch::watch(std::vector<std::string> paths) {
		mPaths = std::move(paths);
		restamp();
	}

	bool FileWatch::changed() {
		const int64_t stamp = fileStamps(mPaths);
		if (stamp == mStamp) return false;
		mStamp = stamp;
		return true;
	}

	void FileWatch::restamp() {
		mStamp = fileStamps(mPaths);
	}
}
