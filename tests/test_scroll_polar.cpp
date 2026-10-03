#include "peepo_drum_kit/chart_editor_scroll_polar.h"
#include <iostream>

using namespace PeepoDrumKit;

int main()
{
	int failures = 0;
	auto check = [&](bool condition, const char* name)
	{ if (!condition) { std::cerr << "FAILED: " << name << '\n'; ++failures; } };
	auto close = [](float a, float b) { return std::abs(a - b) < 0.0001f; };

	check(close(ScrollToPolar({ 3, 4 }, false).Speed, 5), "Both scroll components contribute to speed");
	check(close(ScrollToPolar({ 0, -2 }, false).AngleDegrees, 90), "Up is positive 90 degrees");
	check(close(ScrollToPolar({ 0, -2 }, true).AngleDegrees, -90), "Jiro display orientation reverses angle");
	for (bool invert : { false, true })
	{
		for (const auto scroll : { std::complex<float>{ 1, 0 }, { -1, 0 }, { 0, 2 }, { 0, -2 }, { 3, 4 }, { -3, -4 }, { 0, 0 } })
		{
			const auto restored = PolarToScroll(ScrollToPolar(scroll, invert), invert);
			check(close(restored.real(), scroll.real()) && close(restored.imag(), scroll.imag()), "Polar round trip in both orientations");
		}
	}
	check(close(ScrollAngleDifference(170, -170), 20), "Shortest path across boundary");
	check(close(ScrollAngleDifference(-170, 170), -20), "Reverse shortest path across boundary");
	check(ScrollAngleDifference(0, 180) == 180 && ScrollAngleDifference(0, -180) == -180, "Half-turn keeps entered direction");
	check(ScrollAngleDifference(0, 360) == 0, "Shortest full turn is stationary");
	check(ScrollInterpolationEndAngle(0, 360, true) == 0, "Newly entered full turn uses shortest path");
	check(ScrollInterpolationEndAngle(0, 360, false) == 360, "As-entered interpolation retains full turns");
	check(ScrollInterpolationEndAngle(170, -170, true) == 190, "Newly entered endpoint crosses boundary correctly");
	check(PolarToScroll({ 360, 1 }, false) == std::complex<float>(1, 0), "Full-turn endpoint is exact");
	check(PolarToScroll({ 450, 2 }, false) == std::complex<float>(0, -2), "Angles larger than a full turn");
	check(PolarToScroll({ -360, 1 }, false) == std::complex<float>(1, 0), "Reverse full turn");
	check(PolarToScroll({ 90, 0 }, false) == std::complex<float>(0, 0), "Zero speed retains an editable angle");
	check(PolarToScroll({ 90, 2 }, false) == std::complex<float>(0, -2), "Restore speed using a pending angle");
	for (float progress : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
	{
		const auto scroll = PolarToScroll({ 360 * progress, 2 }, false);
		check(close(std::abs(scroll), 2), "Full-turn interpolation preserves speed");
		const auto shortest = PolarToScroll({ 170 + ScrollAngleDifference(170, -170) * progress, 2 }, false);
		check(shortest.real() < -1.9f, "Shortest interpolation stays near left direction");
	}
	check(close(ScrollMagnitudeToBPM(2, -120), 240), "Magnitude BPM is nonnegative");
	check(close(ScrollMagnitudeFromBPM(240, -120), 2), "Negative BPM does not reverse the entered direction");
	check(close(ScrollMagnitudeFromBPM(240, 240), 1), "Different base BPM preserves requested speed in BPM");
	check(ScrollMagnitudeFromBPM(240, 0) == 0, "Zero BPM never divides by zero");
	check(std::isfinite(ScrollMagnitudeFromBPM(1, std::numeric_limits<float>::min())), "Extreme BPM conversion remains finite");
	const auto original = ScrollToPolar({ 3, 4 }, false);
	const auto rotated = PolarToScroll({ original.AngleDegrees + 90, original.Speed }, false);
	check(close(std::abs(rotated), 5), "Angle edit preserves original magnitude");
	const auto accelerated = PolarToScroll({ original.AngleDegrees, 10 }, false);
	check(close(accelerated.real(), 6) && close(accelerated.imag(), 8), "Speed edit preserves direction");
	std::cout << (failures ? "Scroll polar tests failed\n" : "Scroll polar tests passed\n");
	return failures ? 1 : 0;
}
