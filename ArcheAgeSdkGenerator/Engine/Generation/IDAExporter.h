#pragma once

#include "stdafx.h"

namespace IDAExporter
{
	bool WriteBundle(
		const std::filesystem::path& workingDirectory,
		std::string_view unrealHeader,
		std::string& error);
}
