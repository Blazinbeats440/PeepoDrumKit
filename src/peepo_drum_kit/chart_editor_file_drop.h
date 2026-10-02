#pragma once
#include "core_types.h"
#include <string>
#include <vector>

namespace PeepoDrumKit::FileDrop
{
	enum class Error { None, ReadFailed, InvalidZip, UnsupportedZip, UnsafePath, SizeLimit, ExtractionFailed };
	struct Result
	{
		std::string DirectoryPath;
		std::vector<std::string> ChartPaths;
		Error Failure = Error::None;
	};

	Result ReadFolder(std::string_view directoryPath, i32 searchDepth);
	Result ExtractZip(std::string_view zipPath, std::string_view extractionRoot, i32 searchDepth);
}
