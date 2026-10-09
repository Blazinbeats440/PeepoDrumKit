#pragma once
#include "file_format_tja.h"

namespace OSU
{
	inline constexpr std::string_view Extension = ".osu";
	inline constexpr std::string_view ArchiveExtension = ".osz";
	inline constexpr std::string_view FilterName = "osu! Beatmap";
	inline constexpr std::string_view FilterSpec = "*.osu;*.osz";

	enum class Error { None, InvalidChart, UnsupportedMode, SizeLimit };
	b8 ConvertToTJA(std::string_view text, TJA::ParsedTJA& out, Error& outError);
}
