#pragma once
#include <cmath>
#include <complex>
#include <algorithm>
#include <limits>

namespace PeepoDrumKit
{
	struct ScrollPolarValue { float AngleDegrees = 0.0f; float Speed = 0.0f; };

	inline float ScrollAngleDifference(float start, float end)
	{
		const double difference = static_cast<double>(end) - start;
		double wrapped = std::fmod(difference, 360.0);
		if (wrapped > 180.0) wrapped -= 360.0;
		if (wrapped < -180.0) wrapped += 360.0;
		return static_cast<float>(wrapped);
	}

	inline ScrollPolarValue ScrollToPolar(std::complex<float> scroll, bool invertImaginary)
	{
		const float speed = std::abs(scroll);
		if (speed == 0.0f) return {};
		const double imaginary = invertImaginary ? -scroll.imag() : scroll.imag();
		float angle = static_cast<float>(-std::atan2(imaginary, scroll.real()) * (180.0 / 3.14159265358979323846));
		if (angle <= -180.0f) angle = 180.0f;
		if (angle == 0.0f) angle = 0.0f;
		return { angle, speed };
	}

	inline float ScrollInterpolationEndAngle(float start, float end, bool shortestPath)
	{
		return shortestPath ? start + ScrollAngleDifference(start, end) : end;
	}

	inline std::complex<float> PolarToScroll(ScrollPolarValue value, bool invertImaginary)
	{
		if (value.Speed == 0.0f) return {};
		const double degrees = std::remainder(static_cast<double>(value.AngleDegrees), 360.0);
		const double radians = degrees * (3.14159265358979323846 / 180.0);
		double real = std::cos(radians);
		double imaginary = -std::sin(radians);
		if (degrees == 0.0) { real = 1.0; imaginary = 0.0; }
		else if (std::abs(degrees) == 180.0) { real = -1.0; imaginary = 0.0; }
		else if (degrees == 90.0) { real = 0.0; imaginary = -1.0; }
		else if (degrees == -90.0) { real = 0.0; imaginary = 1.0; }
		if (invertImaginary) imaginary = -imaginary;
		return { static_cast<float>(real * value.Speed), static_cast<float>(imaginary * value.Speed) };
	}

	inline float ScrollMagnitudeToBPM(float speed, float bpm)
	{
		return static_cast<float>(std::min(static_cast<double>(speed) * std::abs(static_cast<double>(bpm)), static_cast<double>(std::numeric_limits<float>::max())));
	}

	inline float ScrollMagnitudeFromBPM(float speedBPM, float bpm)
	{
		if (bpm == 0.0f) return 0.0f;
		return static_cast<float>(std::min(static_cast<double>(speedBPM) / std::abs(static_cast<double>(bpm)), static_cast<double>(std::numeric_limits<float>::max())));
	}
}
