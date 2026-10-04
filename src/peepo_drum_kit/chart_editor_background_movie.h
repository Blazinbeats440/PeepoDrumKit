#pragma once
#include "core_types.h"
#include "core_beat.h"
#include <map>
#include <vector>

namespace PeepoDrumKit
{
	struct BackgroundMovieDefinition
	{
		std::string FileName;
		Time Offset = Time::Zero();
		std::string Error;
	};

	BackgroundMovieDefinition ReadBackgroundMovieDefinition(const std::map<std::string, std::string>& metadata);
	inline Time GetBackgroundMovieTime(Time chartTime, Time songOffset, Time movieOffset)
	{
		return chartTime - songOffset - movieOffset;
	}

	enum class BackgroundMovieStatus { Pending, Ready, Inactive, Failed };
	struct BackgroundMovieFrame
	{
		ivec2 Size = {};
		Time Start = Time::Zero(), End = Time::Zero();
		u64 Sequence = 0;
		std::vector<u8> Pixels;
	};

	// The reader owns no presentation clock or GPU resources. All decoding runs on its worker.
	class BackgroundMovieReader : NonCopyable
	{
	public:
		BackgroundMovieReader();
		~BackgroundMovieReader();
		void Request(std::string_view filePathUTF8, Time time);
		BackgroundMovieStatus Poll(BackgroundMovieFrame& frame);
		std::string GetError() const;
		void Close();
	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
