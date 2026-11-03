#pragma once

#include <cstdint>
#include <vector>

namespace EnumArray
{

template<typename T, typename Reader>
std::vector<T> Read(const T* address, int32_t count, Reader&& reader)
{
	if (!address || count <= 0)
		return {};

	// Construct elements before reading into data(); reserve() leaves size() zero.
	std::vector<T> values(static_cast<size_t>(count));
	reader(address, values.data(), values.size() * sizeof(T));

	return values;
}

}
