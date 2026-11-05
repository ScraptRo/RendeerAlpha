#include <Layout/ImportScan.h>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace RDA::Layout {

	// Every file an entry pulls in, transitively, itself included.
	//
	// Read out of the source rather than asked of esbuild: --metafile refuses to run
	// without an output path, so getting the list from the bundler would mean writing the
	// bundle to a temporary file and parsing JSON to learn something the import lines
	// already say.
	//
	// Static relative imports only. A bare specifier names a package, which is not
	// supported, and a dynamic import is not something the compiler accepts either.
	std::vector<std::string> importedFiles(const std::string& entry) {
		namespace fs = std::filesystem;
		std::vector<std::string> found;
		std::vector<std::string> pending{ entry };
		std::unordered_set<std::string> seen;

		auto resolve = [](const fs::path& from, const std::string& specifier) -> std::string {
			const fs::path base = from.parent_path() / specifier;
			std::error_code ec;
			// The extension is usually left off, the way TypeScript is written.
			for (const char* suffix : { "", ".ts", ".tsx", "/index.ts", "/index.tsx" }) {
				const fs::path candidate = base.string() + suffix;
				if (fs::is_regular_file(candidate, ec)) return candidate.lexically_normal().string();
			}
			return {};
		};

		while (!pending.empty()) {
			const std::string path = pending.back();
			pending.pop_back();
			if (path.empty() || !seen.insert(path).second) continue;
			found.push_back(path);

			std::ifstream file(path, std::ios::binary);
			if (!file) continue;
			const std::string text((std::istreambuf_iterator<char>(file)),
			                       std::istreambuf_iterator<char>());

			// Walk the keywords rather than every quoted string, so a path-shaped literal
			// in ordinary code is not mistaken for a dependency.
			for (const char* keyword : { "from", "import" }) {
				const size_t length = std::strlen(keyword);
				size_t at = 0;
				while ((at = text.find(keyword, at)) != std::string::npos) {
					size_t cursor = at + length;
					at = cursor;
					// A keyword has to stand alone: `fromage` is not `from`.
					if (cursor < text.size() && (std::isalnum(static_cast<unsigned char>(text[cursor])) ||
					                             text[cursor] == '_')) continue;
					while (cursor < text.size() &&
					       std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
					if (cursor >= text.size()) break;
					const char quote = text[cursor];
					if (quote != '"' && quote != '\'') continue;
					const size_t start = ++cursor;
					while (cursor < text.size() && text[cursor] != quote) ++cursor;
					if (cursor >= text.size()) break;

					const std::string specifier = text.substr(start, cursor - start);
					if (specifier.rfind("./", 0) != 0 && specifier.rfind("../", 0) != 0) continue;
					const std::string resolved = resolve(fs::path(path), specifier);
					if (!resolved.empty()) pending.push_back(resolved);
				}
			}
		}
		return found;
	}
}
