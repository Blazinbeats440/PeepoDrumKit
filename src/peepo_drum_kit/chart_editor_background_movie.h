#pragma once
#include "core_types.h"
#include "core_beat.h"
#include <map>
#include <vector>

namespace PeepoDrumKit
{
	inline constexpr std::string_view BackgroundMovieExtensions = ".mp4;.m4v;.mov;.wmv;.avi;.mpg;.mpeg;.m2v;.m2ts;.mts;.ts;.asf;.mkv;.webm";
	inline constexpr std::string_view BackgroundMovieFilterSpec = "*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mpg;*.mpeg;*.m2v;*.m2ts;*.mts;*.ts;*.asf;*.mkv;*.webm";

	struct BackgroundMovieDefinition
	{
		std::string FileName;
		Time Offset = Time::Zero();
		std::string Error;
	};

	BackgroundMovieDefinition ReadBackgroundMovieDefinition(const std::map<std::string, std::string>& metadata);
	void SetBackgroundMovieFileName(std::map<std::string, std::string>& metadata, std::string_view filePath, std::string_view chartFilePath);
	void SetBackgroundMovieOffset(std::map<std::string, std::string>& metadata, Time offset);
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
