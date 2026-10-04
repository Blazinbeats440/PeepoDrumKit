#include "peepo_drum_kit/chart_editor_jpos_easing.h"
#include "peepo_drum_kit/chart_editor_undo.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace PeepoDrumKit;

// The undo header includes these application globals; the tests never use audio or settings reflection.
namespace Audio
{
	struct AudioEngine::Impl {};
	AudioEngine::AudioEngine() = default;
	AudioEngine::~AudioEngine() = default;
}
namespace PeepoDrumKit
{
	SettingsReflectionMap StaticallyInitializeAppSettingsReflectionMap() { return {}; }
}

int main()
{
	int failures = 0;
	const auto check = [&](bool condition, const char* name)
	{
		if (!condition) { std::cerr << "FAILED: " << name << '\n'; ++failures; }
	};
	const auto close = [](double first, double second) { return std::abs(first - second) < 0.001; };
	const auto sumMoves = [](const JPOSEasingResult& result)
	{
		std::complex<double> sum = {};
		for (const auto& segment : result.Segments) sum += std::complex<double>(segment.Move);
		return sum;
	};
	SortedTempoMap tempo;
	tempo.Tempo.InsertOrUpdate({ Beat::Zero(), Tempo(120.0f) });
	tempo.RebuildAccelerationStructure();
	const auto beatToSeconds = [&](int32_t ticks) { return tempo.BeatToTime(Beat::FromTicks(ticks)).ToSec(); };
	const int endTicks = Beat::FromBars(1).Ticks;
	const auto nextGrid16 = [](int32_t ticks) { const int step = GetGridBeatSnap(16).Ticks; return (ticks / step + 1) * step; };
	JPOSMotionSettings gridMotion; gridMotion.Type = JPOSMotionType::OneWay;
	const auto oneBarGrid = GenerateJPOSMotionOnGrid(0, endTicks, { 400, -200 }, JPOSEasing::EaseOut, beatToSeconds, 1.0, gridMotion, nextGrid16);
	const auto twoBarGrid = GenerateJPOSMotionOnGrid(0, endTicks * 2, { 400, -200 }, JPOSEasing::EaseOut, beatToSeconds, 1.0, gridMotion, nextGrid16);
	check(oneBarGrid.Error == JPOSEasingError::None && oneBarGrid.Segments.size() == 16 && twoBarGrid.Segments.size() == 32,
		"Timeline 1/16 grid produces sixteen divisions per standard bar");
	const auto nextGrid32 = [](int32_t ticks) { const int step = GetGridBeatSnap(32).Ticks; return (ticks / step + 1) * step; };
	const auto finerGrid = GenerateJPOSMotionOnGrid(0, endTicks * 2, { 400, -200 }, JPOSEasing::EaseOut, beatToSeconds, 1.0, gridMotion, nextGrid32);
	check(finerGrid.Segments.size() == 64 && std::abs(sumMoves(finerGrid) - std::complex<double>(400, -200)) < 0.001,
		"Changing the timeline grid updates event count without changing displacement");
	const auto gridSeconds = [](int32_t ticks) { return ticks * 0.01; };
	const auto nextGrid100 = [](int32_t ticks) { return (ticks / 100 + 1) * 100; };
	const auto partialGrid = GenerateJPOSMotionOnGrid(30, 270, { 240, 0 }, JPOSEasing::Linear, gridSeconds, 1.0, gridMotion, nextGrid100);
	check(partialGrid.Segments.size() == 3 && partialGrid.Segments[0].BeatTicks == 30 && partialGrid.Segments[1].BeatTicks == 100 &&
		partialGrid.Segments[2].BeatTicks == 200 && close(partialGrid.Segments[0].Move.real(), 70) && close(partialGrid.Segments[2].Move.real(), 70),
		"Off-grid range endpoints preserve actual timeline grid positions and partial intervals");
	const auto nextBarGrid = [](int32_t ticks)
	{
		if (ticks < 250) return std::min((ticks / 100 + 1) * 100, 250);
		return 250 + ((ticks - 250) / 100 + 1) * 100;
	};
	const auto barGrid = GenerateJPOSMotionOnGrid(30, 470, { 440, 0 }, JPOSEasing::Linear, gridSeconds, 1.0, gridMotion, nextBarGrid);
	const int barGridStarts[] = { 30, 100, 200, 250, 350, 450 };
	check(barGrid.Error == JPOSEasingError::None && barGrid.Segments.size() == 6, "Grid stepping supports resetting the grid at a short bar boundary");
	for (size_t index = 0; index < barGrid.Segments.size() && index < 6; ++index)
		check(barGrid.Segments[index].BeatTicks == barGridStarts[index], "Generation retains each supplied bar-relative timeline grid line");
	for (const auto type : { JPOSMotionType::OneWay, JPOSMotionType::RoundTrip, JPOSMotionType::Shake, JPOSMotionType::Ellipse,
		JPOSMotionType::Bounce, JPOSMotionType::Zigzag, JPOSMotionType::Polygon, JPOSMotionType::Waypoints })
	{
		gridMotion = {}; gridMotion.Type = type; gridMotion.Repetitions = 2; gridMotion.RoundTrips = 2; gridMotion.OutboundRatio = 0.35f;
		gridMotion.Waypoints = { { 0, {} }, { 30, { 100, -40 } }, { 60, { 100, -40 } }, { 100, { 240, -30 } } };
		for (const auto easing : { JPOSEasing::Linear, JPOSEasing::EaseOut, JPOSEasing::EaseInOut })
		{
			const auto generated = GenerateJPOSMotionOnGrid(0, 400, { 240, -30 }, easing, gridSeconds, 1.0, gridMotion, nextGrid100);
			check(generated.Error == JPOSEasingError::None, "Every motion generates on the timeline grid");
			for (const int ticks : { 0, 100, 200, 300 })
				check(std::any_of(generated.Segments.begin(), generated.Segments.end(), [&](const JPOSEasingSegment& segment) { return segment.BeatTicks == ticks; }),
					"Every motion preserves all interior timeline grid lines");
			const bool returnsToStart = type == JPOSMotionType::RoundTrip || type == JPOSMotionType::Shake || type == JPOSMotionType::Ellipse || type == JPOSMotionType::Polygon;
			const auto destination = returnsToStart ? std::complex<double>{} : std::complex<double>(240, -30);
			check(std::abs(sumMoves(generated) - destination) < 0.001, "Timeline grid preserves the motion destination");
			for (size_t index = 0; index < generated.Segments.size(); ++index)
			{
				const int next = index + 1 == generated.Segments.size() ? 400 : generated.Segments[index + 1].BeatTicks;
				check(next > generated.Segments[index].BeatTicks && generated.Segments[index].DurationSeconds > 0 &&
					generated.Segments[index].DurationSeconds <= gridSeconds(next) - gridSeconds(generated.Segments[index].BeatTicks),
					"Merging grid lines and landmarks creates unique beats and non-overlapping movement");
			}
		}
		const auto coarse = GenerateJPOSMotionOnGrid(0, 400, { 240, -30 }, JPOSEasing::Linear, gridSeconds, 1.0, gridMotion,
			[](int32_t ticks) { return (ticks / 1000 + 1) * 1000; });
		check(coarse.Error == JPOSEasingError::None && !coarse.Segments.empty(), "Coarse grids automatically add mandatory landmarks for every motion");
		if (type == JPOSMotionType::RoundTrip) check(coarse.Segments.size() == 4 && coarse.Segments[1].BeatTicks == 70, "Coarse grid retains round-trip turns");
		if (type == JPOSMotionType::Waypoints)
			check(coarse.Segments.size() == 3 && coarse.Segments[1].BeatTicks == 120 && coarse.Segments[2].BeatTicks == 240 && coarse.Segments[1].Move == std::complex<float>{},
				"Coarse grid retains off-grid waypoint arrivals and holds");
	}
	gridMotion = {}; gridMotion.Type = JPOSMotionType::OneWay;
	const auto maxGrid = GenerateJPOSMotionOnGrid(0, MaxJPOSEasingGrid, { 100, 0 }, JPOSEasing::Linear, gridSeconds, 1.0, gridMotion,
		[](int32_t ticks) { return ticks + 1; });
	check(maxGrid.Error == JPOSEasingError::None && maxGrid.Segments.size() == MaxJPOSEasingGrid, "The maximum supported grid count is accepted");
	const auto excessiveGrid = GenerateJPOSMotionOnGrid(0, MaxJPOSEasingGrid + 1, { 100, 0 }, JPOSEasing::Linear, gridSeconds, 1.0, gridMotion,
		[](int32_t ticks) { return ticks + 1; });
	check(excessiveGrid.Error == JPOSEasingError::InvalidGrid && excessiveGrid.Segments.empty(), "Excessive grid counts fail without partial commands");
	gridMotion.Type = JPOSMotionType::Waypoints;
	gridMotion.Waypoints = { { 0, {} }, { 30.02f, { 50, 0 } }, { 100, { 100, 0 } } };
	const auto excessiveMergedGrid = GenerateJPOSMotionOnGrid(0, MaxJPOSEasingGrid * 2, {}, JPOSEasing::Linear, gridSeconds, 1.0, gridMotion,
		[](int32_t ticks) { return (ticks / 2 + 1) * 2; });
	check(excessiveMergedGrid.Error == JPOSEasingError::InvalidGrid && excessiveMergedGrid.Segments.empty(), "Additional waypoint landmarks respect the total command limit");
	gridMotion.Type = JPOSMotionType::OneWay;
	check(GenerateJPOSMotionOnGrid(0, 400, {}, JPOSEasing::Linear, gridSeconds, 1.0, gridMotion,
		[](int32_t ticks) { return ticks; }).Error == JPOSEasingError::InvalidGrid, "Non-advancing grid providers are rejected");
	check(GenerateJPOSMotionOnGrid(0, 400, {}, JPOSEasing::Linear, [](int32_t ticks) { return ticks * 0.000001; }, 1.0, gridMotion,
		nextGrid100).Error == JPOSEasingError::IntervalTooShort, "Timeline grids retain the minimum movement duration check");

	for (const auto easing : { JPOSEasing::Linear, JPOSEasing::EaseIn, JPOSEasing::EaseOut, JPOSEasing::EaseInOut, JPOSEasing::EaseOutIn })
	{
		const auto result = GenerateJPOSEasing(0, endTicks, { 400, -200 }, 16, easing, beatToSeconds);
		check(result.Error == JPOSEasingError::None && result.Segments.size() == 16, "Each easing generates the requested count");
		check(close(sumMoves(result).real(), 400) && close(sumMoves(result).imag(), -200), "Each easing preserves both total movements");
		check(EvaluateJPOSEasing(easing, 0) == 0 && EvaluateJPOSEasing(easing, 1) == 1, "Curves retain exact endpoints");
		for (size_t index = 0; index < result.Segments.size(); ++index)
		{
			const auto& segment = result.Segments[index];
			const int next = (index + 1 < result.Segments.size()) ? result.Segments[index + 1].BeatTicks : endTicks;
			check(segment.DurationSeconds > 0 && segment.DurationSeconds <= beatToSeconds(next) - beatToSeconds(segment.BeatTicks),
				"Movement finishes before the next command");
		}
	}
	const auto easeIn = GenerateJPOSEasing(0, endTicks, { 400, 0 }, 4, JPOSEasing::EaseIn, beatToSeconds);
	const auto easeOut = GenerateJPOSEasing(0, endTicks, { 400, 0 }, 4, JPOSEasing::EaseOut, beatToSeconds);
	check(easeIn.Segments.size() == 4 && easeIn.Segments.front().Move.real() == 25 && easeIn.Segments.back().Move.real() == 175,
		"Ease in increases movement per equal time interval");
	check(easeOut.Segments.size() == 4 && easeOut.Segments.front().Move.real() == 175 && easeOut.Segments.back().Move.real() == 25,
		"Ease out decreases movement per equal time interval");
	const auto easeOutIn = GenerateJPOSEasing(0, endTicks, { 400, 0 }, 4, JPOSEasing::EaseOutIn, beatToSeconds);
	check(easeOutIn.Segments.size() == 4 && easeOutIn.Segments[0].Move.real() == 150 && easeOutIn.Segments[1].Move.real() == 50 &&
		easeOutIn.Segments[2].Move.real() == 50 && easeOutIn.Segments[3].Move.real() == 150, "Ease out in slows down at the midpoint and accelerates again");
	for (const auto easing : { JPOSEasing::Linear, JPOSEasing::EaseIn, JPOSEasing::EaseOut, JPOSEasing::EaseInOut, JPOSEasing::EaseOutIn })
	{
		for (double strength : { 0.0, 0.25, 1.0, 2.5, MaxJPOSEasingStrength })
		{
			check(EvaluateJPOSEasing(easing, 0, strength) == 0 && EvaluateJPOSEasing(easing, 1, strength) == 1,
				"Every strength preserves the curve endpoints");
			double previous = 0;
			for (int step = 0; step <= 100; ++step)
			{
				const double progress = step / 100.0;
				const double value = EvaluateJPOSEasing(easing, progress, strength);
				check(std::isfinite(value) && value >= previous && value <= 1, "Strength curves remain continuous in direction and within the range");
				if (strength == 0 || easing == JPOSEasing::Linear)
					check(value == progress, "Zero strength and linear mode are constant speed");
				previous = value;
			}
			const auto result = GenerateJPOSEasing(0, endTicks, { -400, 200 }, 16, easing, beatToSeconds, strength);
			check(result.Error == JPOSEasingError::None && result.Segments.size() == 16 &&
				close(sumMoves(result).real(), -400) && close(sumMoves(result).imag(), 200),
				"Every strength retains the command count and both total movements");
		}
	}
	check(EvaluateJPOSEasing(JPOSEasing::EaseIn, 0.25, 0.25) > EvaluateJPOSEasing(JPOSEasing::EaseIn, 0.25, 1) &&
		EvaluateJPOSEasing(JPOSEasing::EaseIn, 0.25, 1) > EvaluateJPOSEasing(JPOSEasing::EaseIn, 0.25, 4),
		"Increasing ease-in strength delays movement");
	check(EvaluateJPOSEasing(JPOSEasing::EaseOut, 0.25, 4) > EvaluateJPOSEasing(JPOSEasing::EaseOut, 0.25, 1),
		"Increasing ease-out strength moves farther initially");
	check(EvaluateJPOSEasing(JPOSEasing::EaseInOut, 0.25, 4) < EvaluateJPOSEasing(JPOSEasing::EaseInOut, 0.25, 1) &&
		EvaluateJPOSEasing(JPOSEasing::EaseInOut, 0.75, 4) > EvaluateJPOSEasing(JPOSEasing::EaseInOut, 0.75, 1),
		"Increasing ease-in-out strength concentrates movement around the midpoint");
	check(EvaluateJPOSEasing(JPOSEasing::EaseOutIn, 0.25, 4) > EvaluateJPOSEasing(JPOSEasing::EaseOutIn, 0.25, 1) &&
		EvaluateJPOSEasing(JPOSEasing::EaseOutIn, 0.75, 4) < EvaluateJPOSEasing(JPOSEasing::EaseOutIn, 0.75, 1),
		"Increasing ease-out-in strength concentrates movement at the edges");
	const auto stronger = GenerateJPOSEasing(0, endTicks, { 400, 0 }, 4, JPOSEasing::EaseIn, beatToSeconds, 4);
	check(stronger.Segments.size() == 4 && stronger.Segments.front().Move.real() < easeIn.Segments.front().Move.real() &&
		stronger.Segments.back().Move.real() > easeIn.Segments.back().Move.real(), "Strength affects generated commands as well as the displayed curve");
	for (double strength : { -1.0, MaxJPOSEasingStrength + 1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
	{
		const auto result = GenerateJPOSEasing(0, endTicks, { 400, 0 }, 4, JPOSEasing::EaseIn, beatToSeconds, strength);
		check(result.Error == JPOSEasingError::InvalidStrength && result.Segments.empty(), "Invalid strength fails without partial commands");
	}

	tempo.Tempo.InsertOrUpdate({ Beat::FromBeats(2), Tempo(240.0f) });
	tempo.RebuildAccelerationStructure();
	const auto varyingTempo = GenerateJPOSEasing(0, endTicks, { 400, 0 }, 4, JPOSEasing::EaseIn, beatToSeconds);
	check(varyingTempo.Segments.size() == 4 && close(varyingTempo.Segments[0].Move.real(), 400.0 / 9.0) &&
		close(varyingTempo.Segments[0].Move.real() + varyingTempo.Segments[1].Move.real(), 1600.0 / 9.0),
		"Tempo changes use elapsed time rather than beat fraction");
	check(close(sumMoves(varyingTempo).real(), 400), "Tempo changes preserve final movement");

	JPOSMotionSettings motion;
	motion.UseSameEasing = false;
	motion.ReturnEasing = JPOSEasing::EaseIn;
	motion.ReturnStrength = 2.5f;
	const auto roundTrip = GenerateJPOSMotion(0, endTicks, { 200, -100 }, 16, JPOSEasing::EaseOut, beatToSeconds, 1.0, motion);
	check(roundTrip.Error == JPOSEasingError::None && roundTrip.Segments.size() == 16, "Round trip keeps the total division count");
	if (roundTrip.Segments.size() == 16)
	{
		std::complex<double> turningPosition = {};
		for (int index = 0; index < 8; ++index) turningPosition += std::complex<double>(roundTrip.Segments[index].Move);
		check(roundTrip.Segments[8].BeatTicks == endTicks / 2 && close(turningPosition.real(), 200) && close(turningPosition.imag(), -100),
			"Round trip reaches the turning point at the selected beat share");
		check(close(sumMoves(roundTrip).real(), 0) && close(sumMoves(roundTrip).imag(), 0), "Round trip returns to its starting position with tempo changes");
		check(roundTrip.Segments.front().Move.real() > roundTrip.Segments[7].Move.real() &&
			std::abs(roundTrip.Segments[8].Move.real()) < std::abs(roundTrip.Segments.back().Move.real()),
			"Outbound and return legs use independent easing and strength");
	}
	const auto motionRoundTrip = roundTrip;
	for (int trips : { 1, 2, 3, 7 })
	{
		for (float ratio : { 0.1f, 0.25f, 0.5f, 0.8f, 0.99f })
		{
			for (int divisions : { trips * 2, trips * 2 + 1, trips * 2 + 9 })
			{
				motion.RoundTrips = trips;
				motion.OutboundRatio = ratio;
				const int start = 23, end = start + endTicks * 16;
				const auto seconds = [](int32_t ticks) { return ticks * 0.01; };
				const auto result = GenerateJPOSMotion(start, end, { -123.25f, 87.5f }, divisions, JPOSEasing::EaseOutIn, seconds, 4.0, motion);
				check(result.Error == JPOSEasingError::None && result.Segments.size() == divisions, "Asymmetric repeated trips preserve the requested count");
				check(close(sumMoves(result).real(), 0) && close(sumMoves(result).imag(), 0), "Repeated trips have no cumulative position drift");
				std::complex<double> position = {};
				for (size_t index = 0; index < result.Segments.size(); ++index)
				{
					const auto& segment = result.Segments[index];
					const int next = index + 1 < result.Segments.size() ? result.Segments[index + 1].BeatTicks : end;
					check(segment.BeatTicks >= start && next > segment.BeatTicks && next <= end && segment.DurationSeconds > 0 &&
						segment.DurationSeconds <= seconds(next) - seconds(segment.BeatTicks), "All motion commands are unique and finish before the next command");
					position += std::complex<double>(segment.Move);
					for (int cycle = 0; cycle < trips; ++cycle)
					{
						const int cycleStart = start + (static_cast<int64_t>(end - start) * cycle + trips / 2) / trips;
						const int cycleEnd = start + (static_cast<int64_t>(end - start) * (cycle + 1) + trips / 2) / trips;
						const int turn = cycleStart + static_cast<int>(std::llround((cycleEnd - cycleStart) * static_cast<double>(ratio)));
						if (next == turn) check(close(position.real(), -123.25) && close(position.imag(), 87.5), "Each repeated turn retains its relative target");
						if (next == cycleEnd) check(close(position.real(), 0) && close(position.imag(), 0), "Each repeated return reaches the starting position");
					}
				}
			}
		}
	}
	motion = {};
	motion.Type = JPOSMotionType::OneWay;
	const auto oneWay = GenerateJPOSMotion(0, endTicks, { 400, 0 }, 4, JPOSEasing::EaseIn, beatToSeconds, 1.0, motion);
	check(oneWay.Segments.size() == varyingTempo.Segments.size(), "One-way motion uses the existing easing generator");
	for (size_t index = 0; index < oneWay.Segments.size() && index < varyingTempo.Segments.size(); ++index)
		check(oneWay.Segments[index].BeatTicks == varyingTempo.Segments[index].BeatTicks && oneWay.Segments[index].Move == varyingTempo.Segments[index].Move &&
			oneWay.Segments[index].DurationSeconds == varyingTempo.Segments[index].DurationSeconds, "One-way motion retains existing timing and movement");
	motion = {};
	motion.RoundTrips = 2;
	const auto insufficientGrid = GenerateJPOSMotion(0, endTicks, { 100, 0 }, 3, JPOSEasing::EaseOut, beatToSeconds, 1.0, motion);
	check(insufficientGrid.Error == JPOSEasingError::InvalidMotionGrid && insufficientGrid.Segments.empty(), "Too few divisions cannot drop a turning point");
	for (float ratio : { 0.0f, 1.0f, -0.5f, std::numeric_limits<float>::quiet_NaN() })
	{
		motion.OutboundRatio = ratio;
		const auto result = GenerateJPOSMotion(0, endTicks, { 100, 0 }, 16, JPOSEasing::EaseOut, beatToSeconds, 1.0, motion);
		check(result.Error == JPOSEasingError::InvalidMotion && result.Segments.empty(), "Invalid outbound shares fail without partial commands");
	}
	motion = {};
	motion.RoundTrips = 0;
	check(GenerateJPOSMotion(0, endTicks, { 100, 0 }, 16, JPOSEasing::EaseOut, beatToSeconds, 1.0, motion).Error == JPOSEasingError::InvalidMotion, "Zero round trips is rejected");
	motion = {};
	motion.OutboundRatio = 0.99f;
	check(GenerateJPOSMotion(0, 4, { 100, 0 }, 2, JPOSEasing::EaseOut, beatToSeconds, 1.0, motion).Error == JPOSEasingError::InvalidMotionTiming,
		"Unrepresentable turning points are rejected");
	motion = {};
	motion.UseSameEasing = false;
	motion.ReturnStrength = -1.0f;
	const auto invalidReturn = GenerateJPOSMotion(0, endTicks, { 100, 0 }, 16, JPOSEasing::EaseOut, beatToSeconds, 1.0, motion);
	check(invalidReturn.Error == JPOSEasingError::InvalidStrength && invalidReturn.Segments.empty(), "Invalid return settings discard all outbound commands");

	const auto curveSeconds = [](int32_t ticks) { return ticks * 0.01; };
	JPOSMotionSettings curve;
	curve.Type = JPOSMotionType::Shake;
	const auto shake = GenerateJPOSMotion(0, 400, { 100, -50 }, 4, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(shake.Error == JPOSEasingError::None && shake.Segments.size() == 4, "Shake retains its four turning and crossing segments");
	if (shake.Segments.size() == 4)
	{
		std::complex<double> position = {};
		const std::complex<double> expected[] = { { 100, -50 }, {}, { -100, 50 }, {} };
		for (int index = 0; index < 4; ++index)
		{
			position += std::complex<double>(shake.Segments[index].Move);
			check(close(position.real(), expected[index].real()) && close(position.imag(), expected[index].imag()), "Shake visits both sides of its starting position");
		}
	}
	const auto smoothShake = GenerateJPOSMotion(0, 400, { 100, 0 }, 16, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(smoothShake.Segments.size() == 16 && close(smoothShake.Segments[7].Move.real(), smoothShake.Segments[8].Move.real()) &&
		smoothShake.Segments[7].Move.real() < -30, "Shake passes through its center without stopping");
	curve.Damped = true;
	curve.DampingStrength = 2.0f;
	const auto damped = GenerateJPOSMotion(0, 400, { 100, 0 }, 4, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(damped.Segments.size() == 4 && damped.Segments[1].BeatTicks < 100, "Damped shake includes its shifted first extremum");
	if (damped.Segments.size() == 4)
	{
		const double firstPeak = damped.Segments[0].Move.real();
		const double secondPeak = damped.Segments[0].Move.real() + damped.Segments[1].Move.real() + damped.Segments[2].Move.real();
		check(firstPeak > 0 && firstPeak < 100 && secondPeak < 0 && std::abs(secondPeak) < firstPeak && close(sumMoves(damped).real(), 0),
			"Damped shake shrinks towards its starting position");
		const double phase = damped.Segments[1].BeatTicks / 400.0;
		check(std::abs(EvaluateJPOSCurvePosition(curve, { 100, 0 }, phase - 0.01).real()) < firstPeak &&
			std::abs(EvaluateJPOSCurvePosition(curve, { 100, 0 }, phase + 0.01).real()) < firstPeak, "Damping landmarks follow the actual amplitude extrema");
	}
	curve.DampingStrength = 0.0f;
	const auto noDamping = GenerateJPOSMotion(0, 400, { 100, -50 }, 4, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(noDamping.Segments.size() == shake.Segments.size(), "Zero damping retains the regular shake count");
	for (size_t index = 0; index < noDamping.Segments.size() && index < shake.Segments.size(); ++index)
		check(noDamping.Segments[index].Move == shake.Segments[index].Move && noDamping.Segments[index].BeatTicks == shake.Segments[index].BeatTicks,
			"Zero damping matches the regular shake");
	curve = {};
	curve.Type = JPOSMotionType::Ellipse;
	curve.RadiusX = 100; curve.RadiusY = 50;
	const auto orbit = GenerateJPOSMotion(0, 400, {}, 4, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(orbit.Segments.size() == 4, "Ellipse includes each quarter orbit");
	if (orbit.Segments.size() == 4)
	{
		std::complex<double> position = {};
		const std::complex<double> expected[] = { { -100, 50 }, { -200, 0 }, { -100, -50 }, {} };
		for (int index = 0; index < 4; ++index)
		{
			position += std::complex<double>(orbit.Segments[index].Move);
			check(close(position.real(), expected[index].real()) && close(position.imag(), expected[index].imag()), "Ellipse starts at the current position and follows its radii");
		}
	}
	curve.Clockwise = false;
	const auto reverseOrbit = GenerateJPOSMotion(0, 400, {}, 4, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(reverseOrbit.Segments.size() == 4 && close(reverseOrbit.Segments[0].Move.imag(), -50), "Counterclockwise orbit reverses the vertical direction");
	curve.Clockwise = true; curve.Repetitions = 2;
	const auto wholeOrbit = GenerateJPOSMotion(0, 400, {}, 8, JPOSEasing::EaseOut, curveSeconds, 1.0, curve);
	curve.EasingPerCycle = true;
	const auto perOrbit = GenerateJPOSMotion(0, 400, {}, 8, JPOSEasing::EaseOut, curveSeconds, 1.0, curve);
	check(wholeOrbit.Segments.size() == 8 && perOrbit.Segments.size() == 8 && wholeOrbit.Segments[4].BeatTicks < 200 && perOrbit.Segments[4].BeatTicks == 200,
		"Whole-range and per-orbit easing have distinct cycle timing");
	curve = {};
	curve.Type = JPOSMotionType::Bounce;
	curve.JumpHeight = 80;
	const auto jump = GenerateJPOSMotion(0, 400, { 300, 40 }, 2, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(jump.Segments.size() == 2 && close(jump.Segments[0].Move.real(), 150) && close(jump.Segments[0].Move.imag(), -60) &&
		close(sumMoves(jump).real(), 300) && close(sumMoves(jump).imag(), 40), "Jump reaches its upper arc and then the requested destination");
	curve.Repetitions = 3;
	curve.JumpDamped = true;
	const auto dampedJump = GenerateJPOSMotion(0, 1200, { 300, 0 }, 6, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(dampedJump.Error == JPOSEasingError::None && dampedJump.Segments.size() == 6, "Damped jumps preserve the division count");
	double firstJumpPeak = 0;
	if (dampedJump.Segments.size() == 6)
	{
		std::complex<double> position = {};
		double previousHeight = curve.JumpHeight;
		for (int index = 0; index < 6; ++index)
		{
			position += std::complex<double>(dampedJump.Segments[index].Move);
			if (index % 2 == 0)
			{
				const double height = -position.imag();
				check(height > 0 && height < previousHeight, "Later damped jumps have strictly smaller heights");
				previousHeight = height;
				if (index == 0) firstJumpPeak = height;
				const int nextTicks = dampedJump.Segments[index + 1].BeatTicks;
				check(nextTicks < (index + 1) * 200, "Damped jump includes its shifted peak");
				const double progress = nextTicks / 1200.0;
				check(-EvaluateJPOSCurvePosition(curve, { 300, 0 }, progress - 0.01).imag() < height &&
					-EvaluateJPOSCurvePosition(curve, { 300, 0 }, progress + 0.01).imag() < height, "Generated peak follows the maximum damped jump height");
			}
			else check(close(position.imag(), 0), "Each damped jump returns to its movement baseline");
		}
		check(close(position.real(), 300) && close(position.imag(), 0), "Damped jumps retain the requested destination");
	}
	curve.JumpDampingStrength = 4;
	const auto strongerJumpDamping = GenerateJPOSMotion(0, 1200, { 300, 0 }, 6, JPOSEasing::Linear, curveSeconds, 1.0, curve);
	check(strongerJumpDamping.Segments.size() == 6 && -strongerJumpDamping.Segments[0].Move.imag() < firstJumpPeak,
		"Increasing damping strength lowers the jump heights");
	curve.JumpDampingStrength = 0;
	const auto zeroJumpDamping = GenerateJPOSMotion(0, 1200, { 300, -40 }, 6, JPOSEasing::EaseOut, curveSeconds, 1.0, curve);
	curve.JumpDamped = false;
	const auto disabledJumpDamping = GenerateJPOSMotion(0, 1200, { 300, -40 }, 6, JPOSEasing::EaseOut, curveSeconds, 1.0, curve);
	check(zeroJumpDamping.Segments.size() == 6 && disabledJumpDamping.Segments.size() == 6, "Zero and disabled jump damping both generate all jumps");
	for (size_t index = 0; index < zeroJumpDamping.Segments.size() && index < disabledJumpDamping.Segments.size(); ++index)
		check(zeroJumpDamping.Segments[index].BeatTicks == disabledJumpDamping.Segments[index].BeatTicks &&
			zeroJumpDamping.Segments[index].Move == disabledJumpDamping.Segments[index].Move &&
			zeroJumpDamping.Segments[index].DurationSeconds == disabledJumpDamping.Segments[index].DurationSeconds,
			"Zero jump damping matches the original undamped motion");
	curve.JumpDamped = true;
	for (float damping : { -1.0f, 5.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
	{
		curve.JumpDampingStrength = damping;
		const auto invalidJumpDamping = GenerateJPOSMotion(0, 1200, { 300, 0 }, 16, JPOSEasing::EaseOut, curveSeconds, 1.0, curve);
		check(invalidJumpDamping.Error == JPOSEasingError::InvalidCurveMotion && invalidJumpDamping.Segments.empty(), "Invalid jump damping fails without partial commands");
	}
	for (const auto type : { JPOSMotionType::Shake, JPOSMotionType::Ellipse, JPOSMotionType::Bounce })
	{
		for (const auto easing : { JPOSEasing::Linear, JPOSEasing::EaseIn, JPOSEasing::EaseOut, JPOSEasing::EaseInOut, JPOSEasing::EaseOutIn })
		{
			for (double strength : { 0.0, 1.0, 4.0 })
			{
				curve = {}; curve.Type = type; curve.Repetitions = 3; curve.StartAngle = 35; curve.RadiusY = 45;
				curve.Damped = true; curve.DampingStrength = 2.5f;
				curve.JumpDamped = true; curve.JumpDampingStrength = 2.5f;
				for (bool perCycle : { false, true })
				{
					curve.EasingPerCycle = perCycle;
					const int minimum = curve.Repetitions * (type == JPOSMotionType::Bounce ? 2 : 4);
					for (int grid : { minimum, minimum + 7 })
					{
						const auto result = GenerateJPOSMotion(0, endTicks * 8, { -160, 70 }, grid, easing, beatToSeconds, strength, curve);
						check(result.Error == JPOSEasingError::None && result.Segments.size() == grid, "Every curved motion retains the division count with tempo changes");
						const auto total = sumMoves(result);
						check(close(total.real(), type == JPOSMotionType::Bounce ? -160 : 0) && close(total.imag(), type == JPOSMotionType::Bounce ? 70 : 0),
							"Every curved motion retains its endpoint at all easing strengths");
						std::complex<double> position = {};
						for (size_t index = 0; index < result.Segments.size(); ++index)
						{
							const auto& segment = result.Segments[index];
							const int next = index + 1 < result.Segments.size() ? result.Segments[index + 1].BeatTicks : endTicks * 8;
							check(next > segment.BeatTicks && segment.DurationSeconds > 0 && segment.DurationSeconds <= beatToSeconds(next) - beatToSeconds(segment.BeatTicks),
								"Curved motion finishes every segment before the following command");
							position += std::complex<double>(segment.Move);
							if (type == JPOSMotionType::Ellipse)
							{
								const double angle = 35 * 3.14159265358979323846 / 180.0;
								const double x = position.real() / curve.RadiusX + std::cos(angle), y = position.imag() / curve.RadiusY + std::sin(angle);
								check(close(x * x + y * y, 1), "Easing changes timing without changing the ellipse geometry");
							}
							if (type == JPOSMotionType::Shake) check(std::abs(position.real()) <= 160.001 && std::abs(position.imag()) <= 70.001,
								"Shake never exceeds its entered amplitude");
						}
					}
				}
				for (int step = 0; step <= 100; ++step)
				{
					const double progress = step / 100.0;
					check(std::abs(InverseJPOSEasing(easing, EvaluateJPOSEasing(easing, progress, strength), strength) - progress) < 0.0000001,
						"Easing inversion retains landmark progress");
				}
			}
		}
		curve = {}; curve.Type = type;
		const auto tooFew = GenerateJPOSMotion(0, 400, { 100, 0 }, type == JPOSMotionType::Bounce ? 1 : 3, JPOSEasing::EaseOut, curveSeconds, 1.0, curve);
		check(tooFew.Error == JPOSEasingError::InvalidMotionGrid && tooFew.Segments.empty(), "Insufficient curved motion grids cannot omit landmarks");
		curve.Repetitions = 0;
		check(GenerateJPOSMotion(0, 400, { 100, 0 }, 16, JPOSEasing::EaseOut, curveSeconds, 1.0, curve).Error == JPOSEasingError::InvalidCurveMotion,
			"Zero curved motion repetitions are rejected");
	}
	curve = {}; curve.Type = JPOSMotionType::Ellipse; curve.RadiusX = -1;
	check(GenerateJPOSMotion(0, 400, {}, 16, JPOSEasing::EaseOut, curveSeconds, 1.0, curve).Error == JPOSEasingError::InvalidCurveMotion, "Negative ellipse radius is rejected");
	curve.RadiusX = 100; curve.StartAngle = std::numeric_limits<float>::quiet_NaN();
	check(GenerateJPOSMotion(0, 400, {}, 16, JPOSEasing::EaseOut, curveSeconds, 1.0, curve).Error == JPOSEasingError::InvalidCurveMotion, "Nonfinite starting angle is rejected");
	curve = {}; curve.Type = JPOSMotionType::Shake; curve.Damped = true; curve.DampingStrength = 5;
	check(GenerateJPOSMotion(0, 400, { 100, 0 }, 16, JPOSEasing::EaseOut, curveSeconds, 1.0, curve).Error == JPOSEasingError::InvalidCurveMotion, "Invalid damping is rejected");
	curve = {}; curve.Type = JPOSMotionType::Bounce; curve.JumpHeight = -1;
	check(GenerateJPOSMotion(0, 400, { 100, 0 }, 16, JPOSEasing::EaseOut, curveSeconds, 1.0, curve).Error == JPOSEasingError::InvalidCurveMotion, "Negative jump height is rejected");
	curve.JumpHeight = 100;
	const auto shortCurve = GenerateJPOSMotion(0, 400, { 100, 0 }, 16, JPOSEasing::EaseOut, [](int32_t ticks) { return ticks * 0.00001; }, 1.0, curve);
	check(shortCurve.Error == JPOSEasingError::IntervalTooShort && shortCurve.Segments.empty(), "Sub-millisecond curved motion fails without partial commands");
	curve = {}; curve.Type = JPOSMotionType::Ellipse;
	check(GenerateJPOSMotion(0, 4, {}, 4, JPOSEasing::EaseOut, curveSeconds, 4.0, curve).Error == JPOSEasingError::InvalidMotionTiming,
		"Unrepresentable eased geometry boundaries are rejected");

	JPOSMotionSettings zigzag;
	zigzag.Type = JPOSMotionType::Zigzag;
	const auto zigzagCorners = GenerateJPOSMotion(0, 400, { 200, 0 }, 4, JPOSEasing::Linear, curveSeconds, 1.0, zigzag);
	check(zigzagCorners.Error == JPOSEasingError::None && zigzagCorners.Segments.size() == 4, "Zigzag contains both side peaks and center crossings");
	if (zigzagCorners.Segments.size() == 4)
	{
		std::complex<double> position = {};
		const std::complex<double> expected[] = { { 50, -50 }, { 100, 0 }, { 150, 50 }, { 200, 0 } };
		for (int index = 0; index < 4; ++index)
		{
			position += std::complex<double>(zigzagCorners.Segments[index].Move);
			check(std::abs(position - expected[index]) < 0.001, "Default zigzag follows the travel direction and starts left");
		}
	}
	check(std::abs(EvaluateJPOSCurvePosition(zigzag, { 0, 200 }, 0.25) - std::complex<double>(50, 50)) < 0.001,
		"Left side rotates with a downward travel direction");
	zigzag.FirstSideLeft = false;
	check(close(EvaluateJPOSCurvePosition(zigzag, { 200, 0 }, 0.25).imag(), 50), "First side can be reversed");
	zigzag.FirstSideLeft = true; zigzag.ReferenceAngle = 90;
	check(std::abs(EvaluateJPOSCurvePosition(zigzag, {}, 0.25) - std::complex<double>(50, 0)) < 0.001,
		"A zero displacement uses the reference direction");
	const double angularHeight = std::abs(EvaluateJPOSCurvePosition(zigzag, { 200, 0 }, 0.125).imag());
	zigzag.SmoothZigzag = true;
	check(std::abs(EvaluateJPOSCurvePosition(zigzag, { 200, 0 }, 0.125).imag()) > angularHeight,
		"Smooth and angular zigzags have different paths between landmarks");
	zigzag.Repetitions = 3; zigzag.ZigzagDamped = true; zigzag.ZigzagDampingStrength = 2;
	const auto decayingZigzag = GenerateJPOSMotion(0, 1200, { 300, 0 }, 12, JPOSEasing::Linear, curveSeconds, 1.0, zigzag);
	check(decayingZigzag.Error == JPOSEasingError::None && decayingZigzag.Segments.size() == 12, "Smooth decaying zigzag retains its count");
	if (decayingZigzag.Segments.size() == 12)
	{
		std::complex<double> position = {}; double previousPeak = zigzag.LateralAmplitude;
		for (int index = 0; index < 12; ++index)
		{
			position += std::complex<double>(decayingZigzag.Segments[index].Move);
			if (index % 2 == 0)
			{
				check(std::abs(position.imag()) < previousPeak && std::abs(position.imag()) > 0,
					"Zigzag lateral peaks decay while its baseline advances");
				previousPeak = std::abs(position.imag());
			}
			else check(close(position.imag(), 0), "Every decaying zigzag center crossing remains on the baseline");
		}
		check(std::abs(position - std::complex<double>(300, 0)) < 0.001, "Decaying zigzag preserves its destination");
	}
	zigzag.Repetitions = 1; zigzag.SmoothZigzag = false; zigzag.ZigzagDampingStrength = 4;
	const auto shortAngularGrid = GenerateJPOSMotion(0, 1200, { 300, 0 }, 4, JPOSEasing::Linear, curveSeconds, 1.0, zigzag);
	check(shortAngularGrid.Error == JPOSEasingError::InvalidMotionGrid && shortAngularGrid.RequiredDivisions == 6,
		"Angular damping retains both its shifted peak and original corner");
	const auto dampedAngular = GenerateJPOSMotion(0, 1200, { 300, 0 }, 6, JPOSEasing::Linear, curveSeconds, 1.0, zigzag);
	check(dampedAngular.Error == JPOSEasingError::None && dampedAngular.Segments.size() == 6 && dampedAngular.Segments[2].BeatTicks == 300,
		"Angular damping includes the original first corner");
	zigzag.ZigzagDampingStrength = 0;
	const auto zeroZigzagDecay = GenerateJPOSMotion(0, 400, { 200, 0 }, 4, JPOSEasing::Linear, curveSeconds, 1.0, zigzag);
	for (size_t index = 0; index < zeroZigzagDecay.Segments.size() && index < zigzagCorners.Segments.size(); ++index)
		check(zeroZigzagDecay.Segments[index].Move == zigzagCorners.Segments[index].Move, "Zero zigzag decay matches disabled decay");

	JPOSMotionSettings polygon;
	polygon.Type = JPOSMotionType::Polygon; polygon.Width = 200; polygon.Height = 100;
	const auto rectangle = GenerateJPOSMotion(0, 600, {}, 4, JPOSEasing::Linear, curveSeconds, 1.0, polygon);
	check(rectangle.Error == JPOSEasingError::None && rectangle.Segments.size() == 4, "Rectangle contains every vertex");
	if (rectangle.Segments.size() == 4)
	{
		std::complex<double> position = {};
		const std::complex<double> expected[] = { { 200, 0 }, { 200, 100 }, { 0, 100 }, {} };
		for (int index = 0; index < 4; ++index)
		{
			position += std::complex<double>(rectangle.Segments[index].Move);
			check(std::abs(position - expected[index]) < 0.001, "Rectangle visits exact corner coordinates");
			check(close(std::abs(rectangle.Segments[index].Move) / rectangle.Segments[index].DurationSeconds, 100),
				"Linear polygon distributes duration by edge length");
		}
	}
	polygon.Clockwise = false;
	check(std::abs(EvaluateJPOSCurvePosition(polygon, {}, 1.0 / 6.0) - std::complex<double>(0, 100)) < 0.001,
		"Reversing polygon direction keeps the start vertex fixed");
	polygon.Clockwise = true; polygon.RotationAngle = 90;
	check(std::abs(EvaluateJPOSCurvePosition(polygon, {}, 1.0 / 3.0) - std::complex<double>(0, 200)) < 0.001,
		"Polygon rotation is about the start vertex");
	polygon.PolygonShape = JPOSPolygonShape::Triangle; polygon.RotationAngle = 0;
	const auto triangle = GenerateJPOSMotion(0, 1200, {}, 3, JPOSEasing::EaseOut, curveSeconds, 1.0, polygon);
	check(triangle.Error == JPOSEasingError::None && triangle.Segments.size() == 3, "Triangle uses a minimum of three divisions");
	if (triangle.Segments.size() == 3)
	{
		check(std::abs(std::complex<double>(triangle.Segments[0].Move) - std::complex<double>(200, 0)) < 0.001 &&
			std::abs(std::complex<double>(triangle.Segments[0].Move + triangle.Segments[1].Move) - std::complex<double>(100, 100)) < 0.001,
			"Eased triangle retains both non-start vertices");
	}
	polygon.Repetitions = 2;
	const auto wholePolygon = GenerateJPOSMotion(0, 1200, {}, 6, JPOSEasing::EaseOut, curveSeconds, 1.0, polygon);
	polygon.EasingPerCycle = true;
	const auto cyclePolygon = GenerateJPOSMotion(0, 1200, {}, 6, JPOSEasing::EaseOut, curveSeconds, 1.0, polygon);
	check(wholePolygon.Segments.size() == 6 && cyclePolygon.Segments.size() == 6 && wholePolygon.Segments[3].BeatTicks < 600 && cyclePolygon.Segments[3].BeatTicks == 600,
		"Polygon supports whole-range and per-cycle easing");
	polygon.Width = 0;
	check(GenerateJPOSMotion(0, 1200, {}, 6, JPOSEasing::Linear, curveSeconds, 1.0, polygon).Error == JPOSEasingError::InvalidCurveMotion,
		"Zero polygon width is rejected");
	zigzag.LateralAmplitude = -1;
	check(GenerateJPOSMotion(0, 400, {}, 16, JPOSEasing::Linear, curveSeconds, 1.0, zigzag).Error == JPOSEasingError::InvalidCurveMotion,
		"Negative lateral amplitude is rejected");

	JPOSMotionSettings waypoints;
	waypoints.Type = JPOSMotionType::Waypoints;
	waypoints.Waypoints = { { 0, {} }, { 25, { 200, 0 } }, { 50, { 200, 0 } }, { 100, { 0, -100 } } };
	const auto waypointPath = GenerateJPOSMotion(0, 800, {}, 8, JPOSEasing::EaseOut, curveSeconds, 1.0, waypoints);
	check(waypointPath.Error == JPOSEasingError::None && waypointPath.Segments.size() == 8, "Waypoints share the requested total subdivision count");
	if (waypointPath.Segments.size() == 8)
	{
		check(waypointPath.Segments[2].BeatTicks == 200 && waypointPath.Segments[4].BeatTicks == 400,
			"Easing does not change waypoint arrival beats");
		check(waypointPath.Segments[2].Move == std::complex<float>{} && waypointPath.Segments[3].Move == std::complex<float>{},
			"Repeated waypoint coordinates create a stationary interval");
		check(std::abs(std::complex<double>(waypointPath.Segments[0].Move + waypointPath.Segments[1].Move) - std::complex<double>(200, 0)) < 0.001 &&
			std::abs(sumMoves(waypointPath) - std::complex<double>(0, -100)) < 0.001, "Waypoint coordinates are relative to the original start");
	}
	waypoints.WaypointSameEasing = false;
	waypoints.Waypoints[1].Easing = JPOSEasing::EaseIn;
	waypoints.Waypoints[3].Easing = JPOSEasing::Linear;
	const auto individualWaypoints = GenerateJPOSMotion(0, 800, {}, 8, JPOSEasing::EaseOut, curveSeconds, 1.0, waypoints);
	check(individualWaypoints.Segments.size() == 8 && individualWaypoints.Segments[0].Move.real() < waypointPath.Segments[0].Move.real() &&
		individualWaypoints.Segments[2].BeatTicks == 200 && individualWaypoints.Segments[4].BeatTicks == 400,
		"Individual interval easing changes speed while preserving arrivals");
	const auto waypointTempo = [](int32_t ticks) { return ticks <= 100 ? ticks * 0.01 : 1.0 + (ticks - 100) * 0.005; };
	const auto changedTempoWaypoints = GenerateJPOSMotion(0, 800, {}, 8, JPOSEasing::Linear, waypointTempo, 1.0, waypoints);
	check(changedTempoWaypoints.Segments.size() == 8 && changedTempoWaypoints.Segments[2].BeatTicks == 200 &&
		close(changedTempoWaypoints.Segments[0].Move.real(), EvaluateJPOSEasing(JPOSEasing::EaseIn, 2.0 / 3.0) * 200),
		"BPM changes affect within-interval easing but not waypoint beats");
	waypoints.Waypoints[2].ArrivalPercent = 25;
	const auto repeatedArrival = GenerateJPOSMotion(0, 800, {}, 8, JPOSEasing::Linear, curveSeconds, 1.0, waypoints);
	check(repeatedArrival.Error == JPOSEasingError::InvalidWaypoints && repeatedArrival.ErrorInterval == 2 && repeatedArrival.Segments.empty(),
		"Repeated arrivals identify the failing interval and leave no partial events");
	waypoints.Waypoints[2].ArrivalPercent = 25.01f;
	const auto collapsedArrival = GenerateJPOSMotion(0, 800, {}, 8, JPOSEasing::Linear, curveSeconds, 1.0, waypoints);
	check(collapsedArrival.Error == JPOSEasingError::InvalidMotionTiming && collapsedArrival.ErrorInterval == 2,
		"Waypoints rounded to the same beat report the corresponding interval");
	waypoints.Waypoints[2].ArrivalPercent = 50;
	waypoints.Waypoints[2].Strength = 5;
	const auto badIntervalStrength = GenerateJPOSMotion(0, 800, {}, 8, JPOSEasing::Linear, curveSeconds, 1.0, waypoints);
	check(badIntervalStrength.Error == JPOSEasingError::InvalidStrength && badIntervalStrength.ErrorInterval == 2 && badIntervalStrength.Segments.empty(),
		"Invalid interval strength leaves no partial commands");
	waypoints.Waypoints[2].Strength = 1;
	const auto tooShortWaypoint = GenerateJPOSMotion(0, 800, {}, 8, JPOSEasing::Linear, [](int32_t ticks) { return ticks * 0.000001; }, 1.0, waypoints);
	check(tooShortWaypoint.Error == JPOSEasingError::IntervalTooShort && tooShortWaypoint.ErrorInterval == 1,
		"Sub-millisecond waypoint intervals identify the failing interval");
	check(GenerateJPOSMotion(0, 800, {}, 2, JPOSEasing::Linear, curveSeconds, 1.0, waypoints).Error == JPOSEasingError::InvalidMotionGrid,
		"A waypoint path requires at least one division per interval");
	for (const auto type : { JPOSMotionType::Zigzag, JPOSMotionType::Polygon, JPOSMotionType::Waypoints })
	{
		for (const auto easing : { JPOSEasing::Linear, JPOSEasing::EaseIn, JPOSEasing::EaseOut, JPOSEasing::EaseInOut, JPOSEasing::EaseOutIn })
		{
			for (double strength : { 0.0, 1.0, 4.0 })
			{
				JPOSMotionSettings settings; settings.Type = type; settings.Repetitions = 2;
				settings.SmoothZigzag = true; settings.ZigzagDamped = true; settings.ZigzagDampingStrength = 2;
				settings.Waypoints = { { 0, {} }, { 30, { 75, -20 } }, { 70, { 75, -20 } }, { 100, { 300, -40 } } };
				const auto generated = GenerateJPOSMotion(100, 4100, { 300, -40 }, 31, easing, waypointTempo, strength, settings);
				check(generated.Error == JPOSEasingError::None && generated.Segments.size() == 31, "New modes retain uneven total grids for every easing and strength");
				const auto destination = type == JPOSMotionType::Polygon ? std::complex<double>{} : std::complex<double>(300, -40);
				check(std::abs(sumMoves(generated) - destination) < 0.001, "New modes retain exact final destinations");
				for (size_t index = 0; index < generated.Segments.size(); ++index)
				{
					const int next = index + 1 == generated.Segments.size() ? 4100 : generated.Segments[index + 1].BeatTicks;
					check(next > generated.Segments[index].BeatTicks && generated.Segments[index].DurationSeconds > 0 &&
						generated.Segments[index].DurationSeconds <= waypointTempo(next) - waypointTempo(generated.Segments[index].BeatTicks),
						"New modes have unique beats and finish before the next command");
				}
			}
		}
	}

	const auto fractional = GenerateJPOSEasing(100, 117, { -48, 84 }, 3, JPOSEasing::EaseInOut,
		[](int32_t ticks) { return ticks * 0.01; });
	check(fractional.Segments.size() == 3 && fractional.Segments[0].BeatTicks == 100 &&
		fractional.Segments[1].BeatTicks == 106 && fractional.Segments[2].BeatTicks == 111,
		"Uneven divisions keep unique beat ticks and the selected start");
	check(close(sumMoves(fractional).real(), -48) && close(sumMoves(fractional).imag(), 84), "Uneven divisions preserve negative and vertical movement");

	const auto shortIntervals = GenerateJPOSEasing(0, 4, { 100, 0 }, 4, JPOSEasing::Linear,
		[](int32_t ticks) { return ticks * 0.0625; });
	double openTaikoMove = 0;
	for (size_t index = 0; index < shortIntervals.Segments.size(); ++index)
	{
		const auto& segment = shortIntervals.Segments[index];
		char durationText[64];
		std::snprintf(durationText, sizeof(durationText), "%g", segment.DurationSeconds);
		const double savedDurationMs = std::strtod(durationText, nullptr) * 1000.0;
		const int nextMs = static_cast<int>((index + 1) * 62.5);
		const int startMs = static_cast<int>(index * 62.5);
		openTaikoMove += segment.Move.real() * std::min(1.0, (nextMs - startMs) / savedDurationMs);
		check(savedDurationMs <= nextMs - startMs, "Serialized duration avoids OpenTaiko movement truncation");
	}
	check(shortIntervals.Segments.size() == 4 && close(openTaikoMove, 100), "Fractional milliseconds reach the full target in OpenTaiko");
	const auto invalid = GenerateJPOSEasing(0, 100, { 100, 0 }, 100, JPOSEasing::Linear,
		[](int32_t ticks) { return ticks * 0.0001; });
	check(invalid.Error == JPOSEasingError::IntervalTooShort && invalid.Segments.empty(), "Too short intervals fail without partial commands");
	check(GenerateJPOSEasing(0, 0, { 1, 0 }, 16, JPOSEasing::Linear, beatToSeconds).Error == JPOSEasingError::InvalidRange, "Empty selection is rejected");
	check(GenerateJPOSEasing(-1, 10, { 1, 0 }, 1, JPOSEasing::Linear, beatToSeconds).Error == JPOSEasingError::InvalidRange, "Negative start is rejected");
	for (int grid : { 0, -1, MaxJPOSEasingGrid + 1, 101 })
		check(GenerateJPOSEasing(0, 100, { 1, 0 }, grid, JPOSEasing::Linear, beatToSeconds).Error == JPOSEasingError::InvalidGrid, "Invalid grid is rejected");
	check(GenerateJPOSEasing(0, endTicks, { std::numeric_limits<float>::infinity(), 0 }, 4, JPOSEasing::Linear, beatToSeconds).Error == JPOSEasingError::InvalidMove,
		"Nonfinite movement is rejected");
	check(GenerateJPOSEasing(0, 100, { 1, 0 }, 4, JPOSEasing::Linear, [](int32_t ticks) { return -ticks * 0.01; }).Error == JPOSEasingError::InvalidTime,
		"Nonincreasing times are rejected");

	TJA::ErrorList errors;
	const std::string source = "TITLE:JPOS test\nBPM:120\nCOURSE:Oni\n#START\n0000,\n#END\n";
	TJA::ParsedTJA parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(source)), errors);
	check(errors.Errors.empty() && parsed.Courses.size() == 1, "Round trip fixture parses");
	TJA::ConvertedCourse converted = TJA::ConvertParsedToConvertedCourse(parsed, parsed.Courses[0]);
	tempo.Tempo.RemoveAtBeat(Beat::FromBeats(2));
	tempo.RebuildAccelerationStructure();
	const auto saved = GenerateJPOSEasing(0, endTicks, { 400, -200 }, 16, JPOSEasing::EaseInOut, beatToSeconds, 2.5);
	for (const auto& segment : saved.Segments)
		converted.Measures[0].JPOSScrollChanges.push_back({ Beat::FromTicks(segment.BeatTicks), Complex(segment.Move.real(), segment.Move.imag()), segment.DurationSeconds });
	parsed.Courses[0].ChartCommands.clear();
	TJA::ConvertConvertedMeasuresToParsedCommands(converted.Measures, parsed.Courses[0].ChartCommands);
	std::string output;
	TJA::ConvertParsedToText(parsed, output, TJA::SaveFormat::Current);
	if (UTF8::HasBOM(output)) output.erase(0, sizeof(UTF8::BOM_UTF8));
	TJA::ErrorList outputErrors;
	const auto reparsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(output)), outputErrors);
	check(outputErrors.Errors.empty() && reparsed.Courses.size() == 1, "Generated TJA parses after saving");
	const auto reloaded = TJA::ConvertParsedToConvertedCourse(reparsed, reparsed.Courses[0]);
	const auto& reloadedEvents = reloaded.Measures[0].JPOSScrollChanges;
	check(reloadedEvents.size() == saved.Segments.size(), "Save and reload retain all generated commands");
	std::complex<double> reloadedMove = {};
	for (size_t index = 0; index < reloadedEvents.size(); ++index)
	{
		const auto& event = reloadedEvents[index];
		check(event.TimeWithinMeasure.Ticks == saved.Segments[index].BeatTicks, "Save and reload retain beat placement");
		reloadedMove += std::complex<double>(event.Move.GetRealPart(), event.Move.GetImaginaryPart());
	}
	check(close(reloadedMove.real(), 400) && close(reloadedMove.imag(), -200), "Save and reload retain the requested movement");
	converted.Measures[0].JPOSScrollChanges.clear();
	for (const auto& segment : motionRoundTrip.Segments)
		converted.Measures[0].JPOSScrollChanges.push_back({ Beat::FromTicks(segment.BeatTicks), Complex(segment.Move.real(), segment.Move.imag()), segment.DurationSeconds });
	parsed.Courses[0].ChartCommands.clear();
	TJA::ConvertConvertedMeasuresToParsedCommands(converted.Measures, parsed.Courses[0].ChartCommands);
	output.clear();
	TJA::ConvertParsedToText(parsed, output, TJA::SaveFormat::Current);
	if (UTF8::HasBOM(output)) output.erase(0, sizeof(UTF8::BOM_UTF8));
	TJA::ErrorList motionErrors;
	const auto motionParsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(output)), motionErrors);
	check(motionErrors.Errors.empty() && motionParsed.Courses.size() == 1, "Saved motion TJA parses");
	const auto motionReloaded = TJA::ConvertParsedToConvertedCourse(motionParsed, motionParsed.Courses[0]);
	const auto& motionEvents = motionReloaded.Measures[0].JPOSScrollChanges;
	check(motionEvents.size() == motionRoundTrip.Segments.size(), "Motion save and reload retain the event count");
	std::complex<double> motionPosition = {};
	for (size_t index = 0; index < motionEvents.size(); ++index)
	{
		check(motionEvents[index].TimeWithinMeasure.Ticks == motionRoundTrip.Segments[index].BeatTicks, "Motion save and reload retain beat placement");
		motionPosition += std::complex<double>(motionEvents[index].Move.GetRealPart(), motionEvents[index].Move.GetImaginaryPart());
		if (index == 7) check(close(motionPosition.real(), 200) && close(motionPosition.imag(), -100), "Saved motion retains the turning position");
	}
	check(close(motionPosition.real(), 0) && close(motionPosition.imag(), 0), "Saved motion returns to the starting position");

	ChartCourse course;
	const JPOSScrollChange outside = { Beat::FromBars(2), Complex(5, 0), 1, false };
	course.JPOSScrollChanges.InsertOrUpdate(outside);
	std::vector<JPOSScrollChange> events;
	for (const auto& segment : motionRoundTrip.Segments)
		events.push_back({ Beat::FromTicks(segment.BeatTicks), Complex(segment.Move.real(), segment.Move.imag()), segment.DurationSeconds, false });
	Undo::UndoHistory undo;
	undo.Execute<Commands::AddMultipleChartEvents<JPOSScrollChange>>(&course, &course.JPOSScrollChanges, std::move(events));
	check(course.JPOSScrollChanges.size() == 17 && undo.UndoStack.size() == 1, "All generated commands create one undo entry");
	undo.Undo();
	check(course.JPOSScrollChanges.size() == 1 && course.JPOSScrollChanges[0].BeatTime == outside.BeatTime, "One undo restores existing commands outside the range");
	undo.Redo();
	check(course.JPOSScrollChanges.size() == 17, "One redo restores the generated commands");
	for (const auto type : { JPOSMotionType::Shake, JPOSMotionType::Ellipse, JPOSMotionType::Bounce, JPOSMotionType::Zigzag, JPOSMotionType::Polygon, JPOSMotionType::Waypoints })
	{
		curve = {}; curve.Type = type; curve.Repetitions = 2; curve.StartAngle = 35;
		curve.Waypoints = { { 0, {} }, { 25, { 80, -30 } }, { 50, { 80, -30 } }, { 100, { 160, -30 } } };
		curve.Damped = true; curve.DampingStrength = 2; curve.EasingPerCycle = true;
		curve.JumpDamped = true; curve.JumpDampingStrength = 2;
		const auto generated = GenerateJPOSMotionOnGrid(0, endTicks, { 160, -30 }, JPOSEasing::EaseOut, beatToSeconds, 2.5, curve, nextGrid16);
		check(generated.Error == JPOSEasingError::None && !generated.Segments.empty(), "Timeline-grid TJA fixture generates all commands");
		converted.Measures[0].JPOSScrollChanges.clear();
		for (const auto& segment : generated.Segments)
			converted.Measures[0].JPOSScrollChanges.push_back({ Beat::FromTicks(segment.BeatTicks), Complex(segment.Move.real(), segment.Move.imag()), segment.DurationSeconds });
		parsed.Courses[0].ChartCommands.clear();
		TJA::ConvertConvertedMeasuresToParsedCommands(converted.Measures, parsed.Courses[0].ChartCommands);
		output.clear();
		TJA::ConvertParsedToText(parsed, output, TJA::SaveFormat::Current);
		if (UTF8::HasBOM(output)) output.erase(0, sizeof(UTF8::BOM_UTF8));
		TJA::ErrorList curveErrors;
		const auto curveParsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(output)), curveErrors);
		check(curveErrors.Errors.empty() && curveParsed.Courses.size() == 1, "Curved motion TJA parses after saving");
		if (curveParsed.Courses.empty()) continue;
		const auto curveReloaded = TJA::ConvertParsedToConvertedCourse(curveParsed, curveParsed.Courses[0]);
		const auto& curveEvents = curveReloaded.Measures[0].JPOSScrollChanges;
		check(curveEvents.size() == generated.Segments.size(), "Curved motion save and reload retain every event");
		std::complex<double> position = {};
		for (size_t index = 0; index < curveEvents.size() && index < generated.Segments.size(); ++index)
		{
			const auto& event = curveEvents[index];
			check(event.TimeWithinMeasure.Ticks == generated.Segments[index].BeatTicks && close(event.Duration, generated.Segments[index].DurationSeconds),
				"Curved motion save and reload retain placements and durations");
			position += std::complex<double>(event.Move.GetRealPart(), event.Move.GetImaginaryPart());
		}
		const bool hasDestination = type == JPOSMotionType::Bounce || type == JPOSMotionType::Zigzag || type == JPOSMotionType::Waypoints;
		check(close(position.real(), hasDestination ? 160 : 0) && close(position.imag(), hasDestination ? -30 : 0),
			"Curved motion save and reload retain the destination");
		ChartCourse curveCourse;
		curveCourse.JPOSScrollChanges.InsertOrUpdate(outside);
		std::vector<JPOSScrollChange> curveChartEvents;
		for (const auto& segment : generated.Segments)
			curveChartEvents.push_back({ Beat::FromTicks(segment.BeatTicks), Complex(segment.Move.real(), segment.Move.imag()), segment.DurationSeconds, false });
		Undo::UndoHistory curveUndo;
		curveUndo.Execute<Commands::AddMultipleChartEvents<JPOSScrollChange>>(&curveCourse, &curveCourse.JPOSScrollChanges, std::move(curveChartEvents));
		check(curveCourse.JPOSScrollChanges.size() == generated.Segments.size() + 1 && curveUndo.UndoStack.size() == 1, "Each curved motion creates one undo entry");
		curveUndo.Undo();
		check(curveCourse.JPOSScrollChanges.size() == 1 && curveCourse.JPOSScrollChanges[0].BeatTime == outside.BeatTime, "Curved motion undo preserves unrelated events");
		curveUndo.Redo();
		check(curveCourse.JPOSScrollChanges.size() == generated.Segments.size() + 1, "Curved motion redo restores every segment");
	}

	std::cout << (failures ? "JPOS easing tests failed\n" : "JPOS easing tests passed\n");
	return failures ? 1 : 0;
}
