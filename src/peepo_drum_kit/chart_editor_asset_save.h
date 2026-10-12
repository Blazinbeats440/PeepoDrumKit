#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace PeepoDrumKit::AssetSave
{
	enum class Error { None, Canceled, InvalidDestination, MissingSource, ReadFailed, WriteFailed, DestinationChanged, BackupFailed };
	enum class Action { KeepReference, Copy, Reuse };
	enum class Phase { Preparing, Copying, Saving };
	struct Failure
	{
		Error Code = Error::None;
		std::string Path;
		uint32_t SystemError = 0;
	};
	struct Progress
	{
		std::atomic<bool> Cancel { false };
		std::atomic<uint64_t> CompletedBytes { 0 }, TotalBytes { 0 };
		std::atomic<Phase> CurrentPhase { Phase::Preparing };
	};
	struct Input
	{
		std::string Reference;
		std::string SourceOverride;
		bool Selected = true;
	};
	struct Asset
	{
		std::string SourcePath, DestinationPath, SavedReference;
		uint64_t Size = 0;
		bool Selected = true, Missing = false;
		Action Operation = Action::KeepReference;
	};
	struct Plan
	{
		std::string ChartPath;
		std::vector<Asset> Assets;
		uint64_t CopyBytes = 0;
		Failure Failed;
		bool CanSave() const;
	};

	std::string ResolveSource(std::string_view reference, std::string_view chartPath);
	std::string RebaseReference(std::string_view reference, std::string_view oldChartPath, std::string_view newChartPath);
	Plan Prepare(std::string_view oldChartPath, std::string_view newChartPath, const std::vector<Input>& inputs, Progress& progress);
	Failure Save(const Plan& plan, std::string_view chartText, bool backupOriginal, Progress& progress);
}
