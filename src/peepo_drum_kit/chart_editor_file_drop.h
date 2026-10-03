#pragma once
#include "core_types.h"
#include <string>
#include <vector>

namespace PeepoDrumKit::FileDrop
{
	enum class Error { None, ReadFailed, InvalidZip, UnsupportedZip, InvalidArchive, UnsupportedArchive, PasswordProtected, UnsafePath, SizeLimit, ExtractionFailed };
	struct Result
	{
		std::string DirectoryPath;
		std::vector<std::string> ChartPaths;
		Error Failure = Error::None;
	};

	Result ReadFolder(std::string_view directoryPath, i32 searchDepth);
	Result ExtractZip(std::string_view zipPath, std::string_view extractionRoot, i32 searchDepth);
	b8 IsArchivePath(std::string_view path);
	Result ExtractArchive(std::string_view archivePath, std::string_view extractionRoot, i32 searchDepth);
}
