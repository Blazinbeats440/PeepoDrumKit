#include "chart_editor_asset_paths.h"
#include "chart_editor_asset_save.h"
#include "core_io.h"
#include "core_string.h"
#include <filesystem>

namespace PeepoDrumKit
{
	namespace fs = std::filesystem;
	static std::string ExistingDirectory(const fs::path& path)
	{
		std::error_code error;
		if (path.empty() || !fs::is_directory(path, error) || error) return {};
		const auto absolute = fs::absolute(path, error);
		return error ? std::string {} : absolute.lexically_normal().generic_u8string();
	}

	std::string GetRelatedFileOpenDirectory(std::string_view reference, std::string_view chartFilePath, std::string_view lastDirectory)
	{
		const auto executableDirectory = fs::u8path(Directory::GetExecutableDirectory());
		if (!reference.empty())
			if (auto directory = ExistingDirectory(fs::u8path(AssetSave::ResolveSource(reference, chartFilePath)).parent_path()); !directory.empty()) return directory;
		if (!chartFilePath.empty())
			if (auto directory = ExistingDirectory(fs::u8path(AssetSave::ResolveSource(chartFilePath, {})).parent_path()); !directory.empty()) return directory;
		if (!lastDirectory.empty())
		{
			auto path = fs::u8path(lastDirectory);
			if (path.is_relative()) path = executableDirectory / path;
			if (auto directory = ExistingDirectory(path); !directory.empty()) return directory;
		}
		return executableDirectory.generic_u8string();
	}

	void RememberRelatedFileDirectory(std::string_view reference, std::string_view chartFilePath, std::string& lastDirectory)
	{
		if (reference.empty()) return;
		if (auto directory = ExistingDirectory(fs::u8path(AssetSave::ResolveSource(reference, chartFilePath)).parent_path()); !directory.empty())
			lastDirectory = std::move(directory);
	}

	std::string NormalizeChartAssetReference(std::string_view reference, std::string_view chartFilePath)
	{
		if (!reference.empty() && !chartFilePath.empty() && !Path::IsRelative(reference))
			return AssetSave::RebaseReference(reference, {}, chartFilePath);
		return Path::CopyAndNormalize(reference);
	}

	ChartAssetReference::ChartAssetReference(std::string_view reference, std::string_view chartFilePath, b8 trimSource)
		: Reference(reference), AbsolutePath(AssetSave::ResolveSource(trimSource ? ASCII::Trim(reference) : reference, chartFilePath)), ChartFilePath(chartFilePath) {}

	std::string ChartAssetReference::ForChart(std::string_view chartFilePath) const
	{
		// Keep the physical source stable when Save As changes the base of relative paths.
		if (chartFilePath == ChartFilePath || AbsolutePath.empty()) return Reference;
		return chartFilePath.empty() ? AbsolutePath : AssetSave::RebaseReference(AbsolutePath, {}, chartFilePath);
	}

	Commands::ChangeChartAssetReference::ChangeChartAssetReference(std::string* reference, const std::string* chartFilePath,
		std::string_view newReference, ReloadFunction reload, std::string_view description)
		: Reference(reference), ChartFilePath(chartFilePath), OldValue(*reference, *chartFilePath),
		NewValue(newReference, *chartFilePath), Reload(std::move(reload)), Description(description) {}

	void Commands::ChangeChartAssetReference::Undo()
	{
		*Reference = OldValue.ForChart(*ChartFilePath);
		if (Reload) Reload(OldValue.AbsolutePath, false);
	}

	void Commands::ChangeChartAssetReference::Redo()
	{
		*Reference = NewValue.ForChart(*ChartFilePath);
		if (Reload) Reload(NewValue.AbsolutePath, !Applied);
		Applied = true;
	}

	Commands::ChangeBackgroundMovieReference::ChangeBackgroundMovieReference(std::map<std::string, std::string>* metadata,
		const std::string* chartFilePath, std::string_view newReference, std::string_view description)
		: Metadata(metadata), ChartFilePath(chartFilePath), Description(description)
	{
		for (const auto& [key, value] : *metadata)
			if (ASCII::MatchesInsensitive(key, "BGMOVIE")) OldValues.emplace_back(key, ChartAssetReference(value, *chartFilePath, true));
		if (!newReference.empty()) NewValues.emplace_back("BGMOVIE", ChartAssetReference(newReference, *chartFilePath));
	}

	void Commands::ChangeBackgroundMovieReference::Apply(const Entries& values)
	{
		for (auto entry = Metadata->begin(); entry != Metadata->end();)
		{
			if (ASCII::MatchesInsensitive(entry->first, "BGMOVIE")) entry = Metadata->erase(entry);
			else ++entry;
		}
		for (const auto& [key, value] : values) Metadata->emplace(key, value.ForChart(*ChartFilePath));
	}
}
