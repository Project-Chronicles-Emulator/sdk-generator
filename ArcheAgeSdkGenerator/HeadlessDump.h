#pragma once

#include <string>

namespace HeadlessDump
{
	// Returns process exit code (0 = success).
	int run(int pid, const std::string& projectName, const std::string& cacheFile = {});
}
