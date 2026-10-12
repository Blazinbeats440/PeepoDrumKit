#pragma once
#include "core_undo.h"
#include <functional>
#include <map>

namespace PeepoDrumKit
{
	std::string GetRelatedFileOpenDirectory(std::string_view reference, std::string_view chartFilePath, std::string_view lastDirectory);
	void RememberRelatedFileDirectory(std::string_view reference, std::string_view chartFilePath, std::string& lastDirectory);
	std::string NormalizeChartAssetReference(std::string_view reference, std::string_view chartFilePath);

	struct ChartAssetReference
	{
		std::string Reference, AbsolutePath, ChartFilePath;
		ChartAssetReference(std::string_view reference, std::string_view chartFilePath, b8 trimSource = false);
		std::string ForChart(std::string_view chartFilePath) const;
	};

	namespace Commands
	{
		struct ChangeChartAssetReference : Undo::Command
		{
			using ReloadFunction = std::function<void(std::string_view, b8)>;
			ChangeChartAssetReference(std::string* reference, const std::string* chartFilePath, std::string_view newReference,
				ReloadFunction reload, std::string_view description);
			void Undo() override;
			void Redo() override;
			Undo::MergeResult TryMerge(Undo::Command&) override { return Undo::MergeResult::Failed; }
			Undo::CommandInfo GetInfo() const override { return { Description }; }
		private:
			std::string* Reference;
			const std::string* ChartFilePath;
			ChartAssetReference OldValue, NewValue;
			ReloadFunction Reload;
			std::string Description;
			b8 Applied = false;
		};

		struct ChangeBackgroundMovieReference : Undo::Command
		{
			ChangeBackgroundMovieReference(std::map<std::string, std::string>* metadata, const std::string* chartFilePath,
				std::string_view newReference, std::string_view description);
			void Undo() override { Apply(OldValues); }
			void Redo() override { Apply(NewValues); }
			Undo::MergeResult TryMerge(Undo::Command&) override { return Undo::MergeResult::Failed; }
			Undo::CommandInfo GetInfo() const override { return { Description }; }
		private:
			using Entries = std::vector<std::pair<std::string, ChartAssetReference>>;
			void Apply(const Entries& values);
			std::map<std::string, std::string>* Metadata;
			const std::string* ChartFilePath;
			Entries OldValues, NewValues;
			std::string Description;
		};
	}
}
