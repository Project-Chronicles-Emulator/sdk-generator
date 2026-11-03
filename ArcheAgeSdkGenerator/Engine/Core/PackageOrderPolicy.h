#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace PackageOrderPolicy
{
	inline bool Before(std::string_view left, std::string_view right)
	{
		const bool leftBasic  = left == "BasicType";
		const bool rightBasic = right == "BasicType";

		if (leftBasic || rightBasic)
			return leftBasic && !rightBasic;

		std::string lowerLeft(left);
		std::string lowerRight(right);

		std::transform(lowerLeft.begin(), lowerLeft.end(), lowerLeft.begin(),
			[](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
		std::transform(lowerRight.begin(), lowerRight.end(), lowerRight.begin(),
			[](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });

		return lowerLeft < lowerRight;
	}
}
