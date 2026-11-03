#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace AccessPolicy
{
	inline const char* Mode(const char* requested)
	{
		const std::string_view mode = requested ? requested : "";

		if (mode.empty() || mode == "usermode")
			return "usermode";

		if (mode == "kernel" || mode == "playsharp")
			return "kernel";

		throw std::invalid_argument("ARCHEAGE_SDK_MEMORY_MODE must be usermode, kernel or playsharp");
	}

	inline bool AllowsUsermodeFallback(const char* mode)
	{
		return std::string_view(mode) == "usermode";
	}

	// Keep one bridge reply comfortably small: 8 KB of raw memory is ~11 KB
	// of base64, well within a single NDJSON line.
	inline constexpr std::size_t READ_CHUNK = 0x2000;

	inline bool CaptureMatches(std::uint64_t captureSize, std::uint32_t captureTimestamp,
		std::uint32_t imageSize, std::uint32_t imageTimestamp)
	{
		return imageSize > 0 && captureSize == imageSize && captureTimestamp == imageTimestamp;
	}

	inline bool Contains(std::uint64_t size, std::uint64_t offset, std::uint64_t length)
	{
		return length > 0 && offset < size && length <= size - offset;
	}
}
