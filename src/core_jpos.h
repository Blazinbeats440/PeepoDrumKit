#pragma once
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

enum class JPOSNumberFormat : uint8_t { Standard, Integer, Decimal6 };

inline double RoundJPOSValue(double value, JPOSNumberFormat format)
{
	if (format == JPOSNumberFormat::Standard) return value;
	const double scale = format == JPOSNumberFormat::Integer ? 1.0 : 1000000.0;
	return std::round(value * scale) / scale;
}

inline std::string FormatJPOSDecimal(double value, int places)
{
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::fixed << std::setprecision(places) << value;
	std::string text = stream.str();
	if (text.find('.') != std::string::npos)
	{
		while (text.back() == '0') text.pop_back();
		if (text.back() == '.') text.pop_back();
	}
	return text == "-0" ? "0" : text;
}

inline std::string FormatJPOSMove(std::complex<float> move, JPOSNumberFormat format)
{
	const int places = format == JPOSNumberFormat::Integer ? 0 : 6;
	std::string text = FormatJPOSDecimal(RoundJPOSValue(move.real(), format), places);
	const double imaginary = RoundJPOSValue(move.imag(), format);
	if (imaginary != 0.0)
		text += (imaginary > 0.0 ? "+" : "") + FormatJPOSDecimal(imaginary, places) + 'i';
	return text;
}

inline JPOSNumberFormat InferJPOSNumberFormat(std::string_view text)
{
	if (text.find_first_of("eE") != std::string_view::npos) return JPOSNumberFormat::Standard;
	bool hasDecimal = false;
	for (size_t index = 0; index < text.size(); ++index)
	{
		if (text[index] != '.') continue;
		hasDecimal = true;
		int places = 0;
		while (index + 1 < text.size() && text[index + 1] >= '0' && text[index + 1] <= '9') { ++index; ++places; }
		if (places > 6) return JPOSNumberFormat::Standard;
	}
	return hasDecimal ? JPOSNumberFormat::Decimal6 : JPOSNumberFormat::Integer;
}

inline JPOSNumberFormat MergeJPOSNumberFormats(JPOSNumberFormat first, JPOSNumberFormat second)
{
	if (first == JPOSNumberFormat::Standard || second == JPOSNumberFormat::Standard) return JPOSNumberFormat::Standard;
	return first == JPOSNumberFormat::Decimal6 || second == JPOSNumberFormat::Decimal6 ? JPOSNumberFormat::Decimal6 : JPOSNumberFormat::Integer;
}
