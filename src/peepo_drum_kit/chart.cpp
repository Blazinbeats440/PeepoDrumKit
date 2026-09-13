#include "chart.h"
#include "core_build_info.h"
#include <algorithm>
#include <array>

namespace PeepoDrumKit
{
	void DebugCompareCharts(const ChartProject& chartA, const ChartProject& chartB, DebugCompareChartsOnMessageFunc onMessageFunc)
	{
		using ASCII::ToString;
		auto logf = [&](b8 isError, cstr fmt, auto&&... args)
		{
			char buffer[512];
			onMessageFunc(std::string_view(buffer, sprintf_s(buffer, ArrayCount(buffer), fmt, std::forward<decltype(args)>(args)...)), isError);
		};
		auto logErr = [&](cstr fmt, auto&&... args) { logf(true, fmt, std::forward<decltype(args)>(args)...); };
		auto logInfo = [&](cstr fmt, auto&&... args) { logf(false, fmt, std::forward<decltype(args)>(args)...); };

		if (chartA.Courses.size() != chartB.Courses.size()) { logErr("Course count mismatch (%zu != %zu)", chartA.Courses.size(), chartB.Courses.size()); return; }

		for (size_t i = 0; i < chartA.Courses.size(); i++)
		{
			const ChartCourse& courseA = *chartA.Courses[i];
			const ChartCourse& courseB = *chartB.Courses[i];
			if (i > 0)
				logInfo(""); // empty line
			logInfo("Course #%d: %.*s vs %.*s", i, FmtStrViewArgs(courseA.ToString()), FmtStrViewArgs(courseB.ToString()));

			for (GenericList list = {}; list < GenericList::Count; IncrementEnum(list))
			{
				const size_t countA = GetGenericListCount(courseA, list);
				const size_t countB = GetGenericListCount(courseB, list);
				const auto listName = ToString(list);
				if (countA != countB) { logErr("%.*s count mismatch (%zu != %zu)", FmtStrViewArgs(listName), countA, countB); continue; }

				for (size_t itemIndex = 0; itemIndex < countA; itemIndex++)
				{
					for (GenericMember member = {}; member < GenericMember::Count; IncrementEnum(member))
					{
						GenericMemberUnion valueA {}, valueB {};
						const b8 hasValueA = TryGet(courseA, list, itemIndex, member, valueA);
						const b8 hasValueB = TryGet(courseB, list, itemIndex, member, valueB);
						assert(hasValueA == hasValueB);
						if (!hasValueA || member == GenericMember::B8_IsSelected)
							continue;

						const auto memberName = ToString(member);
						TryDo([&](auto&& typedValueA, auto&& typedValueB)
						{
							auto checkMatch = [&](b8 match, auto&& a, auto&& b)
							{
								if (!match) {
									auto strA = ToString(a);
									auto strB = ToString(b);
									logErr("%.*s[%zu].%.*s value mismatch: %.*s vs. %.*s", FmtStrViewArgs(listName), itemIndex, FmtStrViewArgs(memberName), FmtStrViewArgs(strA), FmtStrViewArgs(strB));
								}
							};
							using T = decltype(typedValueA);
							if constexpr (expect_type_v<T, cstr>) {
								checkMatch((!typedValueA && !typedValueB) || (typedValueA && typedValueB && strcmp(typedValueA, typedValueB) == 0), typedValueA, typedValueB);
							} else if constexpr (expect_type_v<T, Complex>) {
								checkMatch(ApproxmiatelySame(typedValueA, typedValueB), typedValueA, typedValueB);
							} else if constexpr (expect_type_v<T, Time>) {
								checkMatch(ApproxmiatelySame(typedValueA.Seconds, typedValueB.Seconds), typedValueA.Seconds, typedValueB.Seconds);
							} else if constexpr (expect_type_v<T, Tempo>) {
								checkMatch(ApproxmiatelySame(typedValueA.BPM, typedValueB.BPM), typedValueA.BPM, typedValueB.BPM);
							} else if constexpr (expect_type_v<T, Beat>) {
								checkMatch(typedValueA == typedValueB, typedValueA.Ticks, typedValueB.Ticks);
							} else {
								checkMatch(typedValueA == typedValueB, typedValueA, typedValueB);
							}
						}, valueA, member, valueB);
					}
				}
			}
		}
	}

	struct TempTimedDelayCommand { Beat Beat; Time Delay; };

	template <>
	struct IsNonListChartEventTrait<TempTimedDelayCommand> : std::true_type { };

	template <GenericMember Member, typename TempTimedDelayCommandT, expect_type_t<TempTimedDelayCommandT, TempTimedDelayCommand> = true>
	constexpr decltype(auto) get(TempTimedDelayCommandT&& event)
	{
		if constexpr (Member == GenericMember::Beat_Start) return (std::forward<TempTimedDelayCommandT>(event).Beat);
	}

	static constexpr NoteType ConvertTJANoteType(TJA::NoteType tjaNoteType)
	{
		switch (tjaNoteType)
		{
		case TJA::NoteType::None: return NoteType::Count;
		case TJA::NoteType::Don: return NoteType::Don;
		case TJA::NoteType::Ka: return NoteType::Ka;
		case TJA::NoteType::DonBig: return NoteType::DonBig;
		case TJA::NoteType::KaBig: return NoteType::KaBig;
		case TJA::NoteType::Start_Drumroll: return NoteType::Drumroll;
		case TJA::NoteType::Start_DrumrollBig: return NoteType::DrumrollBig;
		case TJA::NoteType::Start_Balloon: return NoteType::Balloon;
		case TJA::NoteType::End_BalloonOrDrumroll: return NoteType::Count;
		case TJA::NoteType::Start_BaloonSpecial: return NoteType::BalloonSpecial;
		case TJA::NoteType::DonBigBoth: return NoteType::DonBigHand;
		case TJA::NoteType::KaBigBoth: return NoteType::KaBigHand;
		case TJA::NoteType::Hidden: return NoteType::Adlib;
		case TJA::NoteType::Bomb: return NoteType::Bomb;
		case TJA::NoteType::KaDon: return NoteType::KaDon;
		case TJA::NoteType::Fuse: return NoteType::Fuse;
		default: return NoteType::Count;
		}
	}

	static constexpr TJA::NoteType ConvertTJANoteType(NoteType noteType)
	{
		switch (noteType)
		{
		case NoteType::Don: return TJA::NoteType::Don;
		case NoteType::DonBig: return TJA::NoteType::DonBig;
		case NoteType::Ka: return TJA::NoteType::Ka;
		case NoteType::KaBig: return TJA::NoteType::KaBig;
		case NoteType::Drumroll: return TJA::NoteType::Start_Drumroll;
		case NoteType::DrumrollBig: return TJA::NoteType::Start_DrumrollBig;
		case NoteType::Balloon: return TJA::NoteType::Start_Balloon;
		case NoteType::BalloonSpecial: return TJA::NoteType::Start_BaloonSpecial;
		case NoteType::DonBigHand: return TJA::NoteType::DonBigBoth;
		case NoteType::KaBigHand: return TJA::NoteType::KaBigBoth;
		case NoteType::KaDon: return TJA::NoteType::KaDon;
		case NoteType::Adlib: return TJA::NoteType::Hidden;
		case NoteType::Fuse: return TJA::NoteType::Fuse;
		case NoteType::Bomb: return TJA::NoteType::Bomb;
		default: return TJA::NoteType::None;
		}
	}

	std::string ChartCourse::ToString(OmitLevel omitLevel) const
	{
		constexpr cstr fmts[] = { u8"%s ★%.0f%s %s", u8"%.0s★%.0f%s %s", u8"%.0s★%.0f%s%.0s" };
		cstr fmt = fmts[std::array{ 0, 1, 1, 2 } [EnumToIndex(omitLevel)] ];
		f64 levelRound = Round(Level, std::pow(10, -LevelDecimalPlaces));
		f64 levelWhole, levelFrac = std::modf(levelRound, &levelWhole);

		std::string buffer;
		buffer.resize(96);
		int len = sprintf_s(buffer.data(), buffer.size(), fmt,
			UI_StrRuntime(DifficultyTypeNames[EnumToIndex(Type)]),
			levelWhole,
			(LevelDecimalPlaces == 0) ? "" : (10 * levelFrac >= DifficultyLevelDecimal::PlusThreshold) ? "+" : "",
			GetStyleName(Style, PlayerSide, omitLevel >= ChartCourse::OmitLevel::PlayerCount).data());
		buffer.resize(std::max(0, len)); // set to empty if -1
		return buffer;
	}

	Beat FindCourseMaxUsedBeat(const ChartCourse& course)
	{
		// NOTE: Technically only need to look at the last item of each sorted list **but just to be sure**, in case there is something wonky going on with out-of-order durations or something
		Beat maxBeat = Beat::Zero();
		ApplyForEachGenericList([&](GenericList list, const auto& typedList)
		{
			for (const auto& v : typedList)
				maxBeat = Max(maxBeat, GetBeat(v) + Max(Beat::Zero(), GetBeatDuration(v)));
		}, course);
		for (const BranchRange& branch : course.Branches) maxBeat = Max(maxBeat, branch.GetEnd());
		for (Beat beat : course.BranchSections) maxBeat = Max(maxBeat, beat);
		for (const BranchLevelHold& levelHold : course.BranchLevelHolds) maxBeat = Max(maxBeat, levelHold.BeatTime);
		return maxBeat;
	}

	Beat FindCourseMaxUsedBeatFast(const ChartCourse& course)
	{
		// NOTE: Fast version by only look at the last item of each sorted list
		Beat maxBeat = Beat::Zero();
		ApplyForEachGenericList([&](GenericList list, const auto& typedList)
		{
			if (!typedList.empty()) {
				const auto& last = typedList[std::size(typedList) - 1];
				maxBeat = Max(maxBeat, GetBeat(last) + Max(Beat::Zero(), GetBeatDuration(last)));
			}
		}, course);
		for (const BranchRange& branch : course.Branches) maxBeat = Max(maxBeat, branch.GetEnd());
		for (Beat beat : course.BranchSections) maxBeat = Max(maxBeat, beat);
		for (const BranchLevelHold& levelHold : course.BranchLevelHolds) maxBeat = Max(maxBeat, levelHold.BeatTime);
		return maxBeat;
	}

	b8 CreateChartProjectFromTJA(const TJA::ParsedTJA& inTJA, ChartProject& out)
	{
		out.ChartDuration = Time::Zero();
		out.ChartTitle = inTJA.Metadata.TITLE;
		out.ChartTitleLocalized = inTJA.Metadata.TITLE_localized;
		out.ChartSubtitle = inTJA.Metadata.SUBTITLE;
		out.ChartSubtitleLocalized = inTJA.Metadata.SUBTITLE_localized;
		out.ChartCreator = inTJA.Metadata.MAKER;
		// out.ChartGenre = inTJA.Metadata.GENRE;
		// out.ChartLyricsFileName = inTJA.Metadata.LYRICS;
		out.SongOffset = inTJA.Metadata.OFFSET;
		out.SongDemoStartTime = inTJA.Metadata.DEMOSTART;
		out.SongFileName = inTJA.Metadata.WAVE;
		out.SongJacket = inTJA.Metadata.PREIMAGE;
		out.SongVolume = inTJA.Metadata.SONGVOL;
		out.SoundEffectVolume = inTJA.Metadata.SEVOL;
		// out.BackgroundImageFileName = inTJA.Metadata.BGIMAGE;
		// out.BackgroundMovieFileName = inTJA.Metadata.BGMOVIE;
		// out.MovieOffset = inTJA.Metadata.MOVIEOFFSET;
		out.OtherMetadata = inTJA.Metadata.Others;
		for (size_t i = 0; i < inTJA.Courses.size(); i++)
		{
			if (!inTJA.Courses[i].HasChart) // metadata-only TJA section
				continue;

			const TJA::ConvertedCourse& inCourse = TJA::ConvertParsedToConvertedCourse(inTJA, inTJA.Courses[i]);
			ChartCourse& outCourse = *out.Courses.emplace_back(std::make_unique<ChartCourse>());

			// HACK: Write proper enum conversion functions
			outCourse.Type = Clamp(static_cast<DifficultyType>(inCourse.CourseMetadata.COURSE), DifficultyType {}, DifficultyType::Count);
			outCourse.Level = inCourse.CourseMetadata.LEVEL;
			outCourse.LevelDecimalPlaces = inCourse.CourseMetadata.LEVEL_DECIMALPLACES;
			outCourse.Style = std::max(inCourse.CourseMetadata.STYLE, 1);
			outCourse.PlayerSide = std::clamp(inCourse.CourseMetadata.START_PLAYERSIDE, 1, outCourse.Style);

			outCourse.CourseCreator = inCourse.CourseMetadata.NOTESDESIGNER;

			outCourse.Life = Clamp(static_cast<TowerLives>(inCourse.CourseMetadata.LIFE), TowerLives::Min, TowerLives::Max);
			outCourse.Side = Clamp(static_cast<Side>(inCourse.CourseMetadata.SIDE), Side{}, Side::Count);

			outCourse.TempoMap.Tempo.Sorted = { TempoChange(Beat::Zero(), inTJA.Metadata.BPM) };
			outCourse.TempoMap.Signature.Sorted = { TimeSignatureChange(Beat::Zero(), TimeSignature(4, 4)) };
			TimeSignature lastSignature = TimeSignature(4, 4);

			auto importNotes = [&](const std::vector<TJA::ConvertedMeasure>& measures, SortedNotesList& outNotes, const std::vector<i32>& balloonPopCounts)
			{
				i32 currentBalloonIndex = 0;
				BeatSortedList<TempTimedDelayCommand> tempSortedDelayCommands;
				BeatSortedForwardIterator<TempTimedDelayCommand> tempDelayCommandsIt;
				for (const TJA::ConvertedMeasure& inMeasure : measures)
				{
					for (const TJA::ConvertedDelayChange& inDelayChange : inMeasure.DelayChanges)
						tempSortedDelayCommands.InsertOrUpdate(TempTimedDelayCommand { inMeasure.StartTime + inDelayChange.TimeWithinMeasure, inDelayChange.Delay });
				}

				for (const TJA::ConvertedMeasure& inMeasure : measures)
				{
					for (const TJA::ConvertedNote& inNote : inMeasure.Notes)
					{
						if (inNote.Type == TJA::NoteType::End_BalloonOrDrumroll)
						{
							if (!outNotes.Sorted.empty())
								outNotes.Sorted.back().BeatDuration = (inMeasure.StartTime + inNote.TimeWithinMeasure) - outNotes.Sorted.back().BeatTime;
							continue;
						}

						const NoteType outNoteType = ConvertTJANoteType(inNote.Type);
						if (outNoteType == NoteType::Count)
							continue;

						Note& outNote = outNotes.Sorted.emplace_back();
						outNote.BeatTime = (inMeasure.StartTime + inNote.TimeWithinMeasure);
						outNote.Type = outNoteType;

						const TempTimedDelayCommand* delayCommandForThisNote = tempDelayCommandsIt.Next(tempSortedDelayCommands.Sorted, outNote.BeatTime);
						outNote.TimeOffset = (delayCommandForThisNote != nullptr) ? delayCommandForThisNote->Delay : Time::Zero();

						if (IsBalloonNote(outNote.Type))
						{
							if (InBounds(currentBalloonIndex, balloonPopCounts))
								outNote.BalloonPopCount = balloonPopCounts[currentBalloonIndex];
							currentBalloonIndex++;
						}
					}
				}
			};

			const auto& balloonNormal = !inCourse.CourseMetadata.BALLOON_Normal.empty() ? inCourse.CourseMetadata.BALLOON_Normal : inCourse.CourseMetadata.BALLOON;
			const auto& balloonExpert = !inCourse.CourseMetadata.BALLOON_Expert.empty() ? inCourse.CourseMetadata.BALLOON_Expert : inCourse.CourseMetadata.BALLOON;
			const auto& balloonMaster = !inCourse.CourseMetadata.BALLOON_Master.empty() ? inCourse.CourseMetadata.BALLOON_Master : inCourse.CourseMetadata.BALLOON;
			importNotes(inCourse.Measures, outCourse.Notes_Normal, balloonNormal);
			if (!inCourse.Branches.empty())
			{
				importNotes(inCourse.Measures_Expert, outCourse.Notes_Expert, balloonExpert);
				importNotes(inCourse.Measures_Master, outCourse.Notes_Master, balloonMaster);
			}
			auto importScrollChanges = [](const std::vector<TJA::ConvertedMeasure>& measures, SortedScrollChangesList& outScrollChanges)
			{
				for (const TJA::ConvertedMeasure& measure : measures)
					for (const TJA::ConvertedScrollChange& scrollChange : measure.ScrollChanges)
						outScrollChanges.Sorted.push_back(ScrollChange { measure.StartTime + scrollChange.TimeWithinMeasure, scrollChange.ScrollSpeed });
			};
			importScrollChanges(inCourse.Measures, outCourse.ScrollChanges_Normal);
			if (!inCourse.Branches.empty())
			{
				importScrollChanges(inCourse.Measures_Expert, outCourse.ScrollChanges_Expert);
				importScrollChanges(inCourse.Measures_Master, outCourse.ScrollChanges_Master);
			}
			else
			{
				outCourse.ScrollChanges_Expert = outCourse.ScrollChanges_Normal;
				outCourse.ScrollChanges_Master = outCourse.ScrollChanges_Normal;
			}

			for (const TJA::ConvertedMeasure& inMeasure : inCourse.Measures)
			{
				if (inMeasure.TimeSignature != lastSignature)
				{
					outCourse.TempoMap.Signature.InsertOrUpdate(TimeSignatureChange(inMeasure.StartTime, inMeasure.TimeSignature));
					lastSignature = inMeasure.TimeSignature;
				}

				for (const TJA::ConvertedTempoChange& inTempoChange : inMeasure.TempoChanges)
					outCourse.TempoMap.Tempo.InsertOrUpdate(TempoChange(inMeasure.StartTime + inTempoChange.TimeWithinMeasure, inTempoChange.Tempo));

				for (const TJA::ConvertedScrollType& inScrollType : inMeasure.ScrollTypes)
					outCourse.ScrollTypes.Sorted.push_back(ScrollType{ (inMeasure.StartTime + inScrollType.TimeWithinMeasure),  static_cast<ScrollMethod>(inScrollType.Method) });

				for (const TJA::ConvertedSudden& inSuddenChange : inMeasure.SuddenChanges)
					outCourse.SuddenChanges.Sorted.push_back(SuddenChange{ (inMeasure.StartTime + inSuddenChange.TimeWithinMeasure), inSuddenChange.AppearanceOffset, inSuddenChange.MovementOffset, inSuddenChange.HideRoll });

				for (const TJA::ConvertedJPOSScroll& inJPOSScrollChange : inMeasure.JPOSScrollChanges)
					outCourse.JPOSScrollChanges.Sorted.push_back(JPOSScrollChange{ (inMeasure.StartTime + inJPOSScrollChange.TimeWithinMeasure), inJPOSScrollChange.Move, inJPOSScrollChange.Duration });


				for (const TJA::ConvertedBarLineChange& barLineChange : inMeasure.BarLineChanges)
					outCourse.BarLineChanges.Sorted.push_back(BarLineChange { (inMeasure.StartTime + barLineChange.TimeWithinMeasure), barLineChange.Visibile });

				for (const TJA::ConvertedLyricChange& lyricChange : inMeasure.LyricChanges)
					outCourse.Lyrics.Sorted.push_back(LyricChange { (inMeasure.StartTime + lyricChange.TimeWithinMeasure), lyricChange.Lyric });
			}

			for (const TJA::ConvertedGoGoRange& inGoGoRange : inCourse.GoGoRanges)
				outCourse.GoGoRanges.Sorted.push_back(GoGoRange { inGoGoRange.StartTime, (inGoGoRange.EndTime - inGoGoRange.StartTime) });
			for (const TJA::ConvertedMeasure& inMeasure : inCourse.Measures)
				for (Beat sectionTime : inMeasure.BranchSectionChanges)
					outCourse.BranchSections.push_back(inMeasure.StartTime + sectionTime);

			for (const TJA::ConvertedBranch& inBranch : inCourse.Branches)
				outCourse.Branches.push_back(BranchRange { inBranch.StartTime, inBranch.EndTime - inBranch.StartTime, inBranch.Condition, inBranch.RequirementExpert, inBranch.RequirementMaster, inBranch.EndsBranching });
			for (const TJA::ConvertedBranchLevelHold& levelHold : inCourse.BranchLevelHolds)
				outCourse.BranchLevelHolds.push_back({ levelHold.BeatTime, static_cast<BranchType>(levelHold.Branch) });

			//outCourse.TempoMap.SetTempoChange(TempoChange());
			//outCourse.TempoMap = inCourse.GoGoRanges;

			// outCourse.ScoreInit = inCourse.CourseMetadata.SCOREINIT;
			// outCourse.ScoreDiff = inCourse.CourseMetadata.SCOREDIFF;

			outCourse.OtherMetadata = inCourse.CourseMetadata.Others;

			outCourse.TempoMap.RebuildAccelerationStructure();
			outCourse.RecalculateNoteStates();

			// NOTE: use the non-0 shortest duration to prevent extra measures (editor need to display until max used beat in each difficulty)
			if (!inCourse.Measures.empty()) {
				Time courseDuration = outCourse.TempoMap.BeatToTime(inCourse.Measures.back().StartTime); // last measure end time
				if (out.ChartDuration <= Time::Zero())
					out.ChartDuration = courseDuration;
				else if (courseDuration <= Time::Zero())
					/* keep out.ChartDuration unchanged */;
				else
					out.ChartDuration = Min(out.ChartDuration, courseDuration);
			}
		}

		return true;
	}

	b8 ConvertChartProjectToTJA(const ChartProject& in, TJA::ParsedTJA& out, b8 includePeepoDrumKitComment)
	{
		static constexpr cstr FallbackTJAChartTitle = "Untitled Chart";
		out.Metadata.TITLE = !in.ChartTitle.empty() ? in.ChartTitle : FallbackTJAChartTitle;
		out.Metadata.TITLE_localized = in.ChartTitleLocalized;
		out.Metadata.SUBTITLE = in.ChartSubtitle;
		out.Metadata.SUBTITLE_localized = in.ChartSubtitleLocalized;
		out.Metadata.MAKER = in.ChartCreator;
		// out.Metadata.GENRE = in.ChartGenre;
		// out.Metadata.LYRICS = in.ChartLyricsFileName;
		out.Metadata.OFFSET = in.SongOffset;
		out.Metadata.DEMOSTART = in.SongDemoStartTime;
		out.Metadata.WAVE = in.SongFileName;
		out.Metadata.PREIMAGE = in.SongJacket;
		out.Metadata.SONGVOL = in.SongVolume;
		out.Metadata.SEVOL = in.SoundEffectVolume;
		// out.Metadata.BGIMAGE = in.BackgroundImageFileName;
		// out.Metadata.BGMOVIE = in.BackgroundMovieFileName;
		// out.Metadata.MOVIEOFFSET = in.MovieOffset;
		out.Metadata.Others = in.OtherMetadata;

		if (includePeepoDrumKitComment)
		{
			out.HasPeepoDrumKitComment = true;
			out.PeepoDrumKitCommentDate = BuildInfo::CompilationDateParsed;
		}

		if (!in.Courses.empty())
		{
			if (!in.Courses[0]->TempoMap.Tempo.empty())
			{
				const TempoChange* initialTempo = in.Courses[0]->TempoMap.Tempo.TryFindLastAtBeat(Beat::Zero());
				out.Metadata.BPM = (initialTempo != nullptr) ? initialTempo->Tempo : FallbackTempo;
			}
		}

		out.Courses.reserve(in.Courses.size());
		for (const std::unique_ptr<ChartCourse>& inCourseIt : in.Courses)
		{
			const ChartCourse& inCourse = *inCourseIt;
			TJA::ParsedCourse& outCourse = out.Courses.emplace_back();

			// HACK: Write proper enum conversion functions
			outCourse.Metadata.COURSE = static_cast<TJA::DifficultyType>(inCourse.Type);
			outCourse.Metadata.LEVEL = inCourse.Level;
			outCourse.Metadata.LEVEL_DECIMALPLACES = inCourse.LevelDecimalPlaces;
			outCourse.Metadata.STYLE = inCourse.Style;
			outCourse.Metadata.START_PLAYERSIDE = inCourse.PlayerSide;
			outCourse.Metadata.NOTESDESIGNER = inCourse.CourseCreator;
			for (const Note& inNote : inCourse.Notes_Normal) if (IsBalloonNote(inNote.Type)) { outCourse.Metadata.BALLOON.push_back(inNote.BalloonPopCount); }
			const b8 hasNonZeroLengthBranch = std::any_of(inCourse.Branches.begin(), inCourse.Branches.end(), [](const BranchRange& branch) { return branch.BeatDuration > Beat::Zero(); });
			if (hasNonZeroLengthBranch)
			{
				for (const Note& inNote : inCourse.Notes_Normal) if (IsBalloonNote(inNote.Type)) { outCourse.Metadata.BALLOON_Normal.push_back(inNote.BalloonPopCount); }
				for (const Note& inNote : inCourse.Notes_Expert) if (IsBalloonNote(inNote.Type)) { outCourse.Metadata.BALLOON_Expert.push_back(inNote.BalloonPopCount); }
				for (const Note& inNote : inCourse.Notes_Master) if (IsBalloonNote(inNote.Type)) { outCourse.Metadata.BALLOON_Master.push_back(inNote.BalloonPopCount); }
			}
			// outCourse.Metadata.SCOREINIT = inCourse.ScoreInit;
			// outCourse.Metadata.SCOREDIFF = inCourse.ScoreDiff;

			outCourse.Metadata.LIFE = static_cast<i32>(inCourse.Life);
			outCourse.Metadata.SIDE = static_cast<TJA::SongSelectSide>(inCourse.Side);

			outCourse.Metadata.Others = inCourse.OtherMetadata;

			// TODO: Is this implemented correctly..? Need to have enough measures to cover every note/command and pad with empty measures up to the chart duration
			// BUG: NOPE! "07 ゲームミュージック/003D. MagiCatz/MagiCatz.tja" for example still gets rounded up and then increased by a measure each time it gets saved
			// ... and even so does "Heat Haze Shadow 2.tja" without any weird time signatures..??
			// 1. rounded beat of time can have 1 extra tick which would become a whole measure -> used truncated beat
			// 2. use minimum length of difficulties in case the timing differ slightly
			const Beat inChartMaxUsedBeat = FindCourseMaxUsedBeat(inCourse);
			const Beat inChartBeatDuration = inCourse.TempoMap.TimeToBeat(in.GetDuration(), true);
			std::vector<TJA::ConvertedMeasure> outConvertedMeasures;

			inCourse.TempoMap.ForEachBeatBar([&](const SortedTempoMap::ForEachBeatBarData& it)
			{
				if (it.Beat > inChartMaxUsedBeat && it.Beat >= inChartBeatDuration) // ensure max used beat is converted
					return ControlFlow::Break;
				if (it.IsBar)
				{
					TJA::ConvertedMeasure& outConvertedMeasure = outConvertedMeasures.emplace_back();
					outConvertedMeasure.StartTime = it.Beat;
					outConvertedMeasure.TimeSignature = it.Signature;
				}
				// if max used beat is converted, can stop it now
				return (it.Beat >= inChartMaxUsedBeat && it.Beat >= inChartBeatDuration) ? ControlFlow::Break : ControlFlow::Fallthrough;
			});

			if (outConvertedMeasures.empty())
				outConvertedMeasures.push_back(TJA::ConvertedMeasure { Beat::Zero(), TimeSignature(4, 4) });

			static constexpr auto tryFindMeasureForBeat = [](std::vector<TJA::ConvertedMeasure>& measures, Beat beatToFind) -> TJA::ConvertedMeasure*
			{
				static constexpr auto isMoreBeat = [](const TJA::ConvertedMeasure& lhs, const TJA::ConvertedMeasure& rhs)
				{
					return lhs.StartTime > rhs.StartTime;
				};
				// Binary search in descending (ascending but reversed) list
				// if found: `it` is the last element such that `beatToFind >= it->StartTime`
				auto it = std::lower_bound(measures.rbegin(), measures.rend(), TJA::ConvertedMeasure { beatToFind }, isMoreBeat);
				return (it == measures.rend()) ? nullptr : &*it;
			};

			for (const TempoChange& inTempoChange : inCourse.TempoMap.Tempo)
			{
				if (!(&inTempoChange == &inCourse.TempoMap.Tempo[0] && inTempoChange.Tempo.BPM == out.Metadata.BPM.BPM))
				{
					TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(outConvertedMeasures, inTempoChange.Beat);
					if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
						outConvertedMeasure->TempoChanges.push_back(TJA::ConvertedTempoChange { (inTempoChange.Beat - outConvertedMeasure->StartTime), inTempoChange.Tempo });
				}
			}

			auto appendNotesToMeasures = [&](const SortedNotesList& notes, std::vector<TJA::ConvertedMeasure>& measures)
			{
				Time lastNoteTimeOffset = Time::Zero();
				for (const Note& inNote : notes)
				{
					TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(measures, inNote.BeatTime);
					if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
						outConvertedMeasure->Notes.push_back(TJA::ConvertedNote { (inNote.BeatTime - outConvertedMeasure->StartTime), ConvertTJANoteType(inNote.Type) });

					if (inNote.BeatDuration > Beat::Zero())
					{
						TJA::ConvertedMeasure* durationEndMeasure = tryFindMeasureForBeat(measures, inNote.BeatTime + inNote.BeatDuration);
						if (assert(durationEndMeasure != nullptr); durationEndMeasure != nullptr)
							durationEndMeasure->Notes.push_back(TJA::ConvertedNote { ((inNote.BeatTime + inNote.BeatDuration) - durationEndMeasure->StartTime), TJA::NoteType::End_BalloonOrDrumroll });
					}

					const Time thisNoteTimeOffset = ApproxmiatelySame(inNote.TimeOffset.Seconds, 0.0) ? Time::Zero() : inNote.TimeOffset;
					if (thisNoteTimeOffset != lastNoteTimeOffset)
					{
						outConvertedMeasure->DelayChanges.push_back(TJA::ConvertedDelayChange { (inNote.BeatTime - outConvertedMeasure->StartTime), thisNoteTimeOffset });
						lastNoteTimeOffset = thisNoteTimeOffset;
					}
				}
			};
			appendNotesToMeasures(inCourse.Notes_Normal, outConvertedMeasures);

			auto appendScrollChangesToMeasures = [&](const SortedScrollChangesList& scrollChanges, std::vector<TJA::ConvertedMeasure>& measures)
			{
				for (const ScrollChange& inScroll : scrollChanges)
				{
					TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(measures, inScroll.BeatTime);
					if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
						outConvertedMeasure->ScrollChanges.push_back(TJA::ConvertedScrollChange { (inScroll.BeatTime - outConvertedMeasure->StartTime), inScroll.ScrollSpeed });
				}
			};
			appendScrollChangesToMeasures(inCourse.ScrollChanges_Normal, outConvertedMeasures);

			for (const ScrollType& inScrollType : inCourse.ScrollTypes)
			{
				TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(outConvertedMeasures, inScrollType.BeatTime);
				if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
					outConvertedMeasure->ScrollTypes.push_back(TJA::ConvertedScrollType { (inScrollType.BeatTime - outConvertedMeasure->StartTime), static_cast<i8>(inScrollType.Method) });
			}

			for (const SuddenChange& inSudden : inCourse.SuddenChanges)
			{
				TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(outConvertedMeasures, inSudden.BeatTime);
				if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
					outConvertedMeasure->SuddenChanges.push_back(TJA::ConvertedSudden{ (inSudden.BeatTime - outConvertedMeasure->StartTime), inSudden.AppearanceOffset, inSudden.MovementOffset, inSudden.HideRoll });
			}

			for (const JPOSScrollChange& JPOSScroll : inCourse.JPOSScrollChanges)
			{
				TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(outConvertedMeasures, JPOSScroll.BeatTime);
				if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
					outConvertedMeasure->JPOSScrollChanges.push_back(TJA::ConvertedJPOSScroll { (JPOSScroll.BeatTime - outConvertedMeasure->StartTime), JPOSScroll.Move, JPOSScroll.Duration });
			}

			for (const BarLineChange& barLineChange : inCourse.BarLineChanges)
			{
				TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(outConvertedMeasures, barLineChange.BeatTime);
				if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
					outConvertedMeasure->BarLineChanges.push_back(TJA::ConvertedBarLineChange { (barLineChange.BeatTime - outConvertedMeasure->StartTime), barLineChange.IsVisible });
			}

			for (const LyricChange& inLyric : inCourse.Lyrics)
			{
				TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(outConvertedMeasures, inLyric.BeatTime);
				if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
					outConvertedMeasure->LyricChanges.push_back(TJA::ConvertedLyricChange { (inLyric.BeatTime - outConvertedMeasure->StartTime), inLyric.Lyric });
			}

			// For go-go time events, convert each range to a pair of start & end changes
			for (const GoGoRange& gogo : inCourse.GoGoRanges)
			{
				// start
				TJA::ConvertedMeasure* outConvertedMeasureStart = tryFindMeasureForBeat(outConvertedMeasures, gogo.BeatTime);
				if (assert(outConvertedMeasureStart != nullptr); outConvertedMeasureStart != nullptr)
					outConvertedMeasureStart->GoGoChanges.push_back(TJA::ConvertedGoGoChange{ (gogo.BeatTime - outConvertedMeasureStart->StartTime), true });
				// end
				const Beat endTime = gogo.BeatTime + Max(Beat::Zero(), gogo.BeatDuration);
				TJA::ConvertedMeasure* outConvertedMeasureEnd = tryFindMeasureForBeat(outConvertedMeasures, endTime);
				if (assert(outConvertedMeasureEnd != nullptr); outConvertedMeasureEnd != nullptr)
					outConvertedMeasureEnd->GoGoChanges.push_back(TJA::ConvertedGoGoChange{ (endTime - outConvertedMeasureEnd->StartTime), false });
			}

			for (Beat sectionBeat : inCourse.BranchSections)
			{
				TJA::ConvertedMeasure* outConvertedMeasure = tryFindMeasureForBeat(outConvertedMeasures, sectionBeat);
				if (assert(outConvertedMeasure != nullptr); outConvertedMeasure != nullptr)
					outConvertedMeasure->BranchSectionChanges.push_back(sectionBeat - outConvertedMeasure->StartTime);
			}

			if (inCourse.Branches.empty())
			{
				TJA::ConvertConvertedMeasuresToParsedCommands(outConvertedMeasures, outCourse.ChartCommands);
				continue;
			}

			std::array<std::vector<TJA::ConvertedMeasure>, 3> measuresByBranch = {
				outConvertedMeasures,
				outConvertedMeasures,
				outConvertedMeasures,
			};
			for (size_t branchIndex = 1; branchIndex < measuresByBranch.size(); branchIndex++)
			{
				for (TJA::ConvertedMeasure& measure : measuresByBranch[branchIndex])
				{
					measure.Notes.clear();
					measure.DelayChanges.clear();
					measure.ScrollChanges.clear();
					measure.BranchSectionChanges.clear();
				}
			}
			appendNotesToMeasures(inCourse.Notes_Expert, measuresByBranch[EnumToIndex(BranchType::Expert)]);
			appendNotesToMeasures(inCourse.Notes_Master, measuresByBranch[EnumToIndex(BranchType::Master)]);
			appendScrollChangesToMeasures(inCourse.ScrollChanges_Expert, measuresByBranch[EnumToIndex(BranchType::Expert)]);
			appendScrollChangesToMeasures(inCourse.ScrollChanges_Master, measuresByBranch[EnumToIndex(BranchType::Master)]);

			std::array<std::vector<TJA::ParsedChartCommand>, 3> commandsByBranch;
			for (size_t branchIndex = 0; branchIndex < commandsByBranch.size(); branchIndex++)
				TJA::ConvertConvertedMeasuresToParsedCommands(measuresByBranch[branchIndex], commandsByBranch[branchIndex]);

			auto groupCommandsByMeasure = [](std::vector<TJA::ParsedChartCommand>& commands)
			{
				std::vector<std::vector<TJA::ParsedChartCommand>> groups(1);
				for (TJA::ParsedChartCommand& command : commands)
				{
					groups.back().push_back(std::move(command));
					if (groups.back().back().Type == TJA::ParsedChartCommandType::MeasureEnd)
						groups.emplace_back();
				}
				if (groups.back().empty())
					groups.pop_back();
				return groups;
			};

			std::array<std::vector<std::vector<TJA::ParsedChartCommand>>, 3> commandGroupsByBranch;
			for (size_t branchIndex = 0; branchIndex < commandsByBranch.size(); branchIndex++)
				commandGroupsByBranch[branchIndex] = groupCommandsByMeasure(commandsByBranch[branchIndex]);

			auto beatToMeasureIndex = [&](Beat beat)
			{
				auto it = std::lower_bound(outConvertedMeasures.begin(), outConvertedMeasures.end(), beat,
					[](const TJA::ConvertedMeasure& measure, Beat value) { return measure.StartTime < value; });
				if (it != outConvertedMeasures.end() && it->StartTime == beat)
					return static_cast<size_t>(it - outConvertedMeasures.begin());
				return static_cast<size_t>(std::upper_bound(outConvertedMeasures.begin(), outConvertedMeasures.end(), beat,
					[](Beat value, const TJA::ConvertedMeasure& measure) { return value < measure.StartTime; }) - outConvertedMeasures.begin());
			};

			std::vector<BranchRange> branches = inCourse.Branches;
			std::sort(branches.begin(), branches.end(), [](const BranchRange& a, const BranchRange& b) { return a.BeatTime < b.BeatTime; });
			std::vector<BranchLevelHold> levelHolds = inCourse.BranchLevelHolds;
			std::sort(levelHolds.begin(), levelHolds.end(), [](const BranchLevelHold& a, const BranchLevelHold& b)
			{
				return a.BeatTime != b.BeatTime ? a.BeatTime < b.BeatTime : a.Branch < b.Branch;
			});
			auto appendLevelHoldsAt = [&](Beat beat, BranchType branch)
			{
				for (const BranchLevelHold& levelHold : levelHolds)
					if (levelHold.BeatTime == beat && levelHold.Branch == branch)
						outCourse.ChartCommands.push_back(TJA::ParsedChartCommand { TJA::ParsedChartCommandType::BranchLevelHold });
			};
			auto appendMeasureGroup = [&](BranchType branch, size_t measureIndex)
			{
				auto& groups = commandGroupsByBranch[EnumToIndex(branch)];
				if (measureIndex < groups.size())
					for (TJA::ParsedChartCommand& command : groups[measureIndex])
						outCourse.ChartCommands.push_back(std::move(command));
			};

			size_t currentMeasureIndex = 0;
			for (const BranchRange& branch : branches)
			{
				const size_t startMeasureIndex = beatToMeasureIndex(branch.GetStart());
				const size_t endMeasureIndex = beatToMeasureIndex(branch.GetEnd());
				if (startMeasureIndex < currentMeasureIndex || endMeasureIndex < startMeasureIndex || endMeasureIndex > outConvertedMeasures.size())
					continue;

				for (; currentMeasureIndex < startMeasureIndex; currentMeasureIndex++)
				{
					appendLevelHoldsAt(outConvertedMeasures[currentMeasureIndex].StartTime, BranchType::Normal);
					appendMeasureGroup(BranchType::Normal, currentMeasureIndex);
				}
				TJA::ParsedChartCommand branchStart { TJA::ParsedChartCommandType::BranchStart };
				branchStart.Param.BranchStart = { branch.Condition, branch.RequirementExpert, branch.RequirementMaster };
				outCourse.ChartCommands.push_back(branchStart);
				if (branch.BeatDuration > Beat::Zero())
				{
					for (BranchType branchType = BranchType::Normal; branchType < BranchType::Count; IncrementEnum(branchType))
					{
						const auto selector = (branchType == BranchType::Normal) ? TJA::ParsedChartCommandType::BranchNormal
							: (branchType == BranchType::Expert) ? TJA::ParsedChartCommandType::BranchExpert
							: TJA::ParsedChartCommandType::BranchMaster;
						outCourse.ChartCommands.push_back(TJA::ParsedChartCommand { selector });
						for (size_t measureIndex = startMeasureIndex; measureIndex < endMeasureIndex; measureIndex++)
						{
							appendLevelHoldsAt(outConvertedMeasures[measureIndex].StartTime, branchType);
							appendMeasureGroup(branchType, measureIndex);
						}
					}
				}
				if (branch.EndsBranching)
					outCourse.ChartCommands.push_back(TJA::ParsedChartCommand { TJA::ParsedChartCommandType::BranchEnd });
				currentMeasureIndex = endMeasureIndex;
			}

			for (; currentMeasureIndex < outConvertedMeasures.size(); currentMeasureIndex++)
			{
				appendLevelHoldsAt(outConvertedMeasures[currentMeasureIndex].StartTime, BranchType::Normal);
				appendMeasureGroup(BranchType::Normal, currentMeasureIndex);
			}
		}

		return true;
	}

	b8 RunTJAChartBranchSelfTest(std::string& outError)
	{
		static constexpr std::string_view source =
			"TITLE:Branch Test\n"
			"BPM:120\n"
			"COURSE:Oni\n"
			"LEVEL:5\n"
			"BALLOONNOR:5\n"
			"BALLOONEXP:6\n"
			"BALLOONMAS:7\n"
			"#START\n"
			"1111,\n"
			"#SECTION\n"
			"#BRANCHSTART p,70,80\n"
			"#N\n"
			"#LEVELHOLD\n"
			"#SCROLL 1.25\n"
			"7008,\n"
			"#E\n"
			"#LEVELHOLD\n"
			"#SCROLL 1.5\n"
			"2000,\n"
			"#M\n"
			"#LEVELHOLD\n"
			"#SCROLL 2\n"
			"3000,\n"
			"#BRANCHSTART r,10,20\n"
			"#N\n"
			"1111,\n"
			"#E\n"
			"2222,\n"
			"#M\n"
			"3333,\n"
			"#BRANCHEND\n"
			"1111,\n"
			"#END\n";

		auto parse = [&](std::string_view text, TJA::ParsedTJA& out)
		{
			if (UTF8::HasBOM(text)) text = UTF8::TrimBOM(text);
			TJA::ErrorList errors;
			out = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(text)), errors);
			if (!errors.Errors.empty())
			{
				outError = errors.Errors.front().Description;
				return false;
			}
			return true;
		};
		auto fail = [&](cstr message) { outError = message; return false; };

		TJA::ParsedTJA parsed;
		if (!parse(source, parsed))
			return false;
		ChartProject chart;
		if (!CreateChartProjectFromTJA(parsed, chart) || chart.Courses.size() != 1)
			return fail("Failed to create a single-course branch chart");

		ChartCourse& course = *chart.Courses.front();
		if (course.Branches.size() != 2 || course.BranchSections.size() != 1 || course.BranchLevelHolds.size() != 3)
			return fail("Branch range, #SECTION, or #LEVELHOLD was not imported");
		for (BranchType branch : { BranchType::Normal, BranchType::Expert, BranchType::Master })
			if (std::none_of(course.BranchLevelHolds.begin(), course.BranchLevelHolds.end(), [&](const BranchLevelHold& levelHold)
				{ return levelHold.BeatTime == Beat::FromBars(1) && levelHold.Branch == branch; }))
				return fail("Branch-specific #LEVELHOLD was not imported");
		if (course.Branches[0].Condition != TJA::BranchCondition::Precise || course.Branches[0].RequirementExpert != 70 || course.Branches[0].RequirementMaster != 80)
			return fail("Branch condition was not imported");
		if (course.Branches[0].GetEnd() != Beat::FromBars(2) || course.Branches[0].EndsBranching
			|| course.Branches[1].GetStart() != Beat::FromBars(2) || course.Branches[1].GetEnd() != Beat::FromBars(3) || !course.Branches[1].EndsBranching)
			return fail("Consecutive #BRANCHSTART commands were not imported as adjacent branch ranges");

		const Beat branchStart = Beat::FromBars(1);
		const Note* normal = course.Notes_Normal.TryFindExactAtBeat(branchStart);
		const Note* expert = course.Notes_Expert.TryFindExactAtBeat(branchStart);
		const Note* master = course.Notes_Master.TryFindExactAtBeat(branchStart);
		if (normal == nullptr || normal->Type != NoteType::Balloon || normal->BalloonPopCount != 5)
			return fail("Normal branch note was not imported");
		if (expert == nullptr || expert->Type != NoteType::Ka)
			return fail("Expert branch note was not imported");
		if (master == nullptr || master->Type != NoteType::DonBig)
			return fail("Master branch note was not imported");
		const ScrollChange* normalScroll = course.ScrollChanges_Normal.TryFindExactAtBeat(branchStart);
		const ScrollChange* expertScroll = course.ScrollChanges_Expert.TryFindExactAtBeat(branchStart);
		const ScrollChange* masterScroll = course.ScrollChanges_Master.TryFindExactAtBeat(branchStart);
		if (normalScroll == nullptr || normalScroll->ScrollSpeed != Complex(1.25f, 0.0f)
			|| expertScroll == nullptr || expertScroll->ScrollSpeed != Complex(1.5f, 0.0f)
			|| masterScroll == nullptr || masterScroll->ScrollSpeed != Complex(2.0f, 0.0f))
			return fail("Branch scroll speeds were not imported separately");

		const Beat zeroLengthBranchBeat = Beat::FromBars(3);
		course.Branches.push_back(BranchRange { zeroLengthBranchBeat, Beat::Zero(), TJA::BranchCondition::Precise, 50, 75 });

		TJA::ParsedTJA exported;
		if (!ConvertChartProjectToTJA(chart, exported, false))
			return fail("Failed to export branch chart");
		std::string exportedText;
		TJA::ConvertParsedToText(exported, exportedText, TJA::SaveFormat::Current);
		for (std::string_view marker : { "#SECTION", "#BRANCHSTART p,70,80", "#N", "#E", "#M", "#BRANCHEND", "#LEVELHOLD", "BALLOONNOR:5", "BALLOONEXP:", "BALLOONMAS:" })
			if (exportedText.find(marker) == std::string::npos)
				return fail("Exported TJA is missing branch data");
		auto countOccurrences = [](std::string_view text, std::string_view value)
		{
			size_t count = 0;
			for (size_t offset = 0; (offset = text.find(value, offset)) != std::string_view::npos; offset += value.size())
				count++;
			return count;
		};
		if (countOccurrences(exportedText, "\n#N\n") != 1 || countOccurrences(exportedText, "\n#E\n") != 1 || countOccurrences(exportedText, "\n#M\n") != 1)
			return fail("Zero-length branch unexpectedly exported branch selectors");

		TJA::ParsedTJA reparsed;
		if (!parse(exportedText, reparsed))
			return false;
		ChartProject roundTripped;
		if (!CreateChartProjectFromTJA(reparsed, roundTripped) || roundTripped.Courses.size() != 1 || roundTripped.Courses[0]->Branches.size() != 3)
			return fail("Exported branch chart could not be imported again");
		if (roundTripped.Courses[0]->BranchSections.size() != 1)
			return fail("#SECTION was lost during round trip");
		if (roundTripped.Courses[0]->BranchLevelHolds.size() != 3)
			return fail("Branch-specific #LEVELHOLD was lost during round trip");
		for (BranchType branch : { BranchType::Normal, BranchType::Expert, BranchType::Master })
			if (std::none_of(roundTripped.Courses[0]->BranchLevelHolds.begin(), roundTripped.Courses[0]->BranchLevelHolds.end(), [&](const BranchLevelHold& levelHold)
				{ return levelHold.BeatTime == Beat::FromBars(1) && levelHold.Branch == branch; }))
				return fail("Branch-specific #LEVELHOLD was assigned to the wrong branch during round trip");
		if (roundTripped.Courses[0]->Branches[2].GetStart() != zeroLengthBranchBeat || roundTripped.Courses[0]->Branches[2].BeatDuration != Beat::Zero() || !roundTripped.Courses[0]->Branches[2].EndsBranching)
			return fail("Zero-length branch was lost during round trip");
		if (roundTripped.Courses[0]->Notes_Expert.TryFindExactAtBeat(branchStart) == nullptr || roundTripped.Courses[0]->Notes_Master.TryFindExactAtBeat(branchStart) == nullptr)
			return fail("Branch notes were lost during round trip");
		const ChartCourse& roundTrippedCourse = *roundTripped.Courses[0];
		const ScrollChange* roundTrippedNormalScroll = roundTrippedCourse.ScrollChanges_Normal.TryFindExactAtBeat(branchStart);
		const ScrollChange* roundTrippedExpertScroll = roundTrippedCourse.ScrollChanges_Expert.TryFindExactAtBeat(branchStart);
		const ScrollChange* roundTrippedMasterScroll = roundTrippedCourse.ScrollChanges_Master.TryFindExactAtBeat(branchStart);
		if (roundTrippedNormalScroll == nullptr || roundTrippedNormalScroll->ScrollSpeed != Complex(1.25f, 0.0f)
			|| roundTrippedExpertScroll == nullptr || roundTrippedExpertScroll->ScrollSpeed != Complex(1.5f, 0.0f)
			|| roundTrippedMasterScroll == nullptr || roundTrippedMasterScroll->ScrollSpeed != Complex(2.0f, 0.0f))
			return fail("Branch scroll speeds were lost during round trip");

		ChartCourse& zeroLengthOnlyCourse = *roundTripped.Courses[0];
		zeroLengthOnlyCourse.Branches.erase(std::remove_if(zeroLengthOnlyCourse.Branches.begin(), zeroLengthOnlyCourse.Branches.end(),
			[](const BranchRange& branch) { return branch.BeatDuration > Beat::Zero(); }), zeroLengthOnlyCourse.Branches.end());
		TJA::ParsedTJA zeroLengthOnlyExported;
		if (!ConvertChartProjectToTJA(roundTripped, zeroLengthOnlyExported, false))
			return fail("Failed to export zero-length-only branch chart");
		std::string zeroLengthOnlyText;
		TJA::ConvertParsedToText(zeroLengthOnlyExported, zeroLengthOnlyText, TJA::SaveFormat::Current);
		if (zeroLengthOnlyText.find("BALLOONNOR:") != std::string::npos || zeroLengthOnlyText.find("BALLOONEXP:") != std::string::npos || zeroLengthOnlyText.find("BALLOONMAS:") != std::string::npos)
			return fail("Zero-length-only branch chart unexpectedly exported branch balloon metadata");
		if (zeroLengthOnlyText.find("\n#N\n") != std::string::npos || zeroLengthOnlyText.find("\n#E\n") != std::string::npos || zeroLengthOnlyText.find("\n#M\n") != std::string::npos)
			return fail("Zero-length-only branch chart unexpectedly exported branch selectors");
		if (zeroLengthOnlyText.find("#BRANCHSTART") == std::string::npos || zeroLengthOnlyText.find("#BRANCHEND") == std::string::npos)
			return fail("Zero-length-only branch commands were not exported");

		outError.clear();
		return true;
	}
}
