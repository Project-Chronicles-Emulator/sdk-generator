#pragma once

#include <string>
#include <string_view>

namespace PackageNamePolicy
{
	inline std::string Second(std::string_view package)
	{
		if (package.empty() || package.front() != '/')
			return {};

		package.remove_prefix(1);
		const size_t firstSeparator = package.find('/');

		if (firstSeparator == std::string_view::npos)
			return std::string(package);

		package.remove_prefix(firstSeparator + 1);
		const size_t secondSeparator = package.find('/');

		return std::string(package.substr(0, secondSeparator));
	}
}
