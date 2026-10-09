#pragma once
#include "file_format_tja.h"

namespace MC
{
	inline constexpr std::string_view Extension = ".mc";
	inline constexpr std::string_view FilterName = "Malody Taiko Chart";
	inline constexpr std::string_view FilterSpec = "*.mc";

	enum class Error { None, InvalidJSON, NotTaiko, InvalidChart, SizeLimit };
	b8 ConvertToTJA(std::string_view text, TJA::ParsedTJA& out, Error& outError);
}
