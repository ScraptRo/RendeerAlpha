#include "FontInternal.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

namespace RDA {

	// The faces a system has for what an interface's own font does not draw, most likely
	// first. Han characters are drawn differently in Chinese, Japanese and Korean type, so
	// the order follows the language the reader's system is set to; anyone who wants a
	// particular face names it in config.font and it is asked first.
	std::vector<std::string> detail::systemFallbacks() {
		std::vector<std::string> out;
		if (const char* off = std::getenv("RDA_SYSTEM_FONTS"); off && off[0] == '0') return out;
	#if defined(_WIN32)
		std::string fonts = "C:/Windows/Fonts/";
		if (const char* windir = std::getenv("WINDIR")) fonts = std::string(windir) + "/Fonts/";
		const LANGID lang = GetUserDefaultUILanguage();
		const WORD primary = PRIMARYLANGID(lang);
		const WORD sub = SUBLANGID(lang);
		std::vector<std::string> cjk = { "msyh.ttc", "YuGothM.ttc", "malgun.ttf", "msjh.ttc" };
		if (primary == LANG_JAPANESE) cjk = { "YuGothM.ttc", "msyh.ttc", "malgun.ttf", "msjh.ttc" };
		else if (primary == LANG_KOREAN) cjk = { "malgun.ttf", "msyh.ttc", "YuGothM.ttc", "msjh.ttc" };
		else if (primary == LANG_CHINESE && sub != SUBLANG_CHINESE_SIMPLIFIED && sub != SUBLANG_CHINESE_SINGAPORE)
			cjk = { "msjh.ttc", "msyh.ttc", "YuGothM.ttc", "malgun.ttf" };
		for (const std::string& name : cjk) out.push_back(fonts + name);
		// Then the faces that cover what CJK faces do not: symbols, other scripts.
		for (const char* name : { "seguisym.ttf", "segoeui.ttf", "Nirmala.ttf", "ebrima.ttf" })
			out.push_back(fonts + name);
	#elif defined(__linux__)
		const char* lang = std::getenv("LANG");
		const std::string l = lang ? lang : "";
		// Noto CJK is what nearly every distribution ships; its collections hold every
		// regional variant, and the first face in them is the one used.
		std::vector<std::string> cjk = {
			"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
			"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
			"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
			"/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
			"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
			"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
			"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
		};
		(void)l;
		for (const std::string& path : cjk) out.push_back(path);
		for (const char* path : { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		                          "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf" })
			out.push_back(path);
	#endif
		// Only what is actually there: a stat now saves a failed open on the first frame
		// that needs one.
		std::vector<std::string> present;
		std::error_code ec;
		for (std::string& path : out) if (std::filesystem::exists(path, ec)) present.push_back(std::move(path));
		return present;
	}
}
