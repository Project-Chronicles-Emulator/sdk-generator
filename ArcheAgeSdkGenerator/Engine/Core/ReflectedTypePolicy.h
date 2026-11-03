#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ReflectedTypePolicy
{
	inline bool IsAsciiPath(std::string_view name)
	{
		if (name.empty() || name.size() > 4096)
			return false;

		for (unsigned char letter : name)
		{
			if (letter < 0x21 || letter > 0x7e)
				return false;
		}

		return true;
	}

	inline bool IsObjectPath(std::string_view name)
	{
		return IsAsciiPath(name) && name.front() == '/'
			&& name.find('.') != std::string_view::npos;
	}

	inline bool IsIdentifier(std::string_view name)
	{
		if (name.empty() || name.size() > 256)
			return false;

		for (size_t index = 0; index < name.size(); ++index)
		{
			const unsigned char letter = static_cast<unsigned char>(name[index]);
			const bool alpha = (letter >= 'A' && letter <= 'Z')
				|| (letter >= 'a' && letter <= 'z') || letter == '_';
			const bool digit = letter >= '0' && letter <= '9';

			if (!alpha && (index == 0 || !digit))
				return false;
		}

		return true;
	}

	inline bool ValidLayout(int32_t size, int32_t alignment)
	{
		constexpr int32_t MAX_SIZE = 0x100000;
		return size > 0 && size <= MAX_SIZE && alignment >= 0
			&& (alignment == 0 || (alignment <= 64 && (alignment & (alignment - 1)) == 0));
	}

	inline bool ValidMember(int32_t offset, int32_t size, int32_t ownerSize)
	{
		return offset >= 0 && size > 0 && ownerSize > 0
			&& offset <= ownerSize && size <= ownerSize - offset;
	}
}
