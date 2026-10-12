#include "chart_editor_asset_save.h"
#include <filesystem>
#include <algorithm>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace PeepoDrumKit::AssetSave
{
	namespace fs = std::filesystem;
	static constexpr size_t BufferSize = 1024 * 1024;

	struct Handle
	{
		HANDLE Value = INVALID_HANDLE_VALUE;
		explicit Handle(HANDLE value) : Value(value) {}
		~Handle() { if (Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		bool Valid() const { return Value != INVALID_HANDLE_VALUE; }
	};

	static bool SamePath(const fs::path& left, const fs::path& right)
	{
		const std::wstring leftName = left.lexically_normal().generic_wstring(), rightName = right.lexically_normal().generic_wstring();
		if (CompareStringOrdinal(leftName.c_str(), -1, rightName.c_str(), -1, TRUE) == CSTR_EQUAL) return true;
		std::error_code error;
		return fs::equivalent(left, right, error) && !error;
	}

	std::string ResolveSource(std::string_view reference, std::string_view chartPath)
	{
		if (reference.empty()) return {};
		fs::path path = fs::u8path(reference);
		if (path.is_relative() && !chartPath.empty()) path = fs::u8path(chartPath).parent_path() / path;
		std::error_code error;
		const fs::path absolute = fs::absolute(path, error);
		return (error ? path : absolute).lexically_normal().generic_u8string();
	}

	std::string RebaseReference(std::string_view reference, std::string_view oldChartPath, std::string_view newChartPath)
	{
		if (reference.empty() || oldChartPath == newChartPath) return std::string(reference);
		const fs::path source = fs::u8path(ResolveSource(reference, oldChartPath));
		const fs::path directory = fs::u8path(ResolveSource(newChartPath, {})).parent_path();
		const fs::path relative = source.lexically_relative(directory);
		return (relative.empty() ? source : relative).generic_u8string();
	}

	bool Plan::CanSave() const
	{
		if (Failed.Code != Error::None) return false;
		for (const Asset& asset : Assets) if (asset.Selected && asset.Missing) return false;
		return true;
	}

	static Failure LastFailure(Error code, const fs::path& path)
	{
		return { code, path.generic_u8string(), GetLastError() };
	}

	static bool SameContent(const fs::path& left, const fs::path& right, Progress& progress, Failure& failed)
	{
		if (progress.Cancel.load()) { failed = { Error::Canceled }; return false; }
		Handle source(CreateFileW(left.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (!source.Valid()) { failed = LastFailure(Error::ReadFailed, left); return false; }
		Handle destination(CreateFileW(right.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (!destination.Valid()) { failed = LastFailure(Error::ReadFailed, right); return false; }
		LARGE_INTEGER sourceSize {}, destinationSize {};
		if (!GetFileSizeEx(source.Value, &sourceSize)) { failed = LastFailure(Error::ReadFailed, left); return false; }
		if (!GetFileSizeEx(destination.Value, &destinationSize)) { failed = LastFailure(Error::ReadFailed, right); return false; }
		if (sourceSize.QuadPart != destinationSize.QuadPart) return false;
		if (SamePath(left, right)) return true;
		std::vector<char> sourceBuffer(BufferSize), destinationBuffer(BufferSize);
		for (;;)
		{
			if (progress.Cancel.load()) { failed = { Error::Canceled }; return false; }
			DWORD sourceRead = 0, destinationRead = 0;
			if (!ReadFile(source.Value, sourceBuffer.data(), static_cast<DWORD>(BufferSize), &sourceRead, nullptr)) { failed = LastFailure(Error::ReadFailed, left); return false; }
			if (!ReadFile(destination.Value, destinationBuffer.data(), static_cast<DWORD>(BufferSize), &destinationRead, nullptr)) { failed = LastFailure(Error::ReadFailed, right); return false; }
			if (progress.CurrentPhase.load() == Phase::Preparing) progress.CompletedBytes.fetch_add(sourceRead);
			if (sourceRead != destinationRead || !std::equal(sourceBuffer.begin(), sourceBuffer.begin() + sourceRead, destinationBuffer.begin())) return false;
			if (sourceRead == 0) return true;
		}
	}

	Plan Prepare(std::string_view oldChartPath, std::string_view newChartPath, const std::vector<Input>& inputs, Progress& progress)
	{
		Plan plan;
		plan.ChartPath = ResolveSource(newChartPath, {});
		const fs::path chart = fs::u8path(plan.ChartPath), directory = chart.parent_path();
		std::error_code error;
		if (newChartPath.empty() || !fs::is_directory(directory, error) || error)
		{
			plan.Failed = { Error::InvalidDestination, plan.ChartPath, static_cast<uint32_t>(error.value()) };
			return plan;
		}
		progress.CurrentPhase = Phase::Preparing;
		for (const Input& input : inputs)
		{
			if (progress.Cancel.load()) { plan.Failed = { Error::Canceled }; return plan; }
			Asset asset;
			asset.Selected = input.Selected && !input.Reference.empty();
			asset.SourcePath = ResolveSource(input.SourceOverride.empty() ? input.Reference : input.SourceOverride, oldChartPath);
			asset.SavedReference = input.SourceOverride.empty()
				? RebaseReference(input.Reference, oldChartPath, newChartPath)
				: RebaseReference(asset.SourcePath, {}, newChartPath);
			const fs::path source = fs::u8path(asset.SourcePath);
			if (!asset.SourcePath.empty() && SamePath(source, chart))
			{
				plan.Failed = { Error::InvalidDestination, plan.ChartPath };
				return plan;
			}
			error.clear();
			asset.Missing = !fs::is_regular_file(source, error) || error;
			if (!asset.Missing)
			{
				asset.Size = fs::file_size(source, error);
				if (error) asset.Missing = true;
			}
			if (asset.Selected && !asset.Missing)
			{
				bool resolved = false;
				// A single source referenced more than once is published only once.
				for (const Asset& previous : plan.Assets)
					if (previous.Selected && !previous.Missing && SamePath(source, fs::u8path(previous.SourcePath)))
					{
						asset.DestinationPath = previous.DestinationPath;
						asset.SavedReference = previous.SavedReference;
						asset.Operation = Action::Reuse;
						resolved = true;
						break;
					}
				for (uint64_t suffix = 1; !resolved; suffix++)
				{
					if (progress.Cancel.load()) { plan.Failed = { Error::Canceled }; return plan; }
					const fs::path name = suffix == 1 ? source.filename() : fs::u8path(source.stem().u8string() + " (" + std::to_string(suffix) + ")" + source.extension().u8string());
					const fs::path destination = directory / name;
					if (SamePath(destination, chart)) continue;
					bool reserved = false;
					for (const Asset& previous : plan.Assets)
						if (!previous.DestinationPath.empty() && SamePath(destination, fs::u8path(previous.DestinationPath))) { reserved = true; break; }
					if (reserved) continue;
					error.clear();
					const bool exists = fs::exists(destination, error);
					if (error) { plan.Failed = { Error::ReadFailed, destination.generic_u8string(), static_cast<uint32_t>(error.value()) }; return plan; }
					if (exists)
					{
						if (!fs::is_regular_file(destination, error)) continue;
						if (!SameContent(source, destination, progress, plan.Failed))
						{
							if (plan.Failed.Code != Error::None) return plan;
							continue;
						}
					}
					asset.Operation = exists ? Action::Reuse : Action::Copy;
					asset.DestinationPath = destination.generic_u8string();
					asset.SavedReference = name.generic_u8string();
					if (!exists) plan.CopyBytes += asset.Size;
					resolved = true;
				}
			}
			plan.Assets.push_back(std::move(asset));
		}
		return plan;
	}

	struct SaveFiles
	{
		std::vector<fs::path> Temporary, Published;
		bool Committed = false;
		void Moved(const fs::path& path) { Temporary.erase(std::remove(Temporary.begin(), Temporary.end(), path), Temporary.end()); }
		~SaveFiles()
		{
			std::error_code error;
			for (const fs::path& path : Temporary) fs::remove(path, error);
			if (!Committed) for (auto it = Published.rbegin(); it != Published.rend(); ++it) fs::remove(*it, error);
		}
	};

	static HANDLE CreateTemporary(const fs::path& directory, SaveFiles& files, fs::path& path)
	{
		static std::atomic<uint64_t> sequence { 0 };
		for (size_t attempt = 0; attempt < 100; attempt++)
		{
			path = directory / (L".pdk-save-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(sequence.fetch_add(1)) + L".tmp");
			HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle != INVALID_HANDLE_VALUE) { files.Temporary.push_back(path); return handle; }
			if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
		}
		return INVALID_HANDLE_VALUE;
	}

	static Failure StageCopy(const Asset& asset, const fs::path& directory, SaveFiles& files, fs::path& temporary, Progress& progress)
	{
		const fs::path sourcePath = fs::u8path(asset.SourcePath);
		Handle source(CreateFileW(sourcePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (!source.Valid()) return LastFailure(Error::ReadFailed, sourcePath);
		LARGE_INTEGER size {};
		if (!GetFileSizeEx(source.Value, &size)) return LastFailure(Error::ReadFailed, sourcePath);
		if (static_cast<uint64_t>(size.QuadPart) != asset.Size) return { Error::DestinationChanged, asset.SourcePath };
		Handle output(CreateTemporary(directory, files, temporary));
		if (!output.Valid()) return LastFailure(Error::WriteFailed, directory);
		std::vector<char> buffer(BufferSize);
		for (;;)
		{
			if (progress.Cancel.load()) return { Error::Canceled };
			DWORD read = 0, written = 0;
			if (!ReadFile(source.Value, buffer.data(), static_cast<DWORD>(BufferSize), &read, nullptr)) return LastFailure(Error::ReadFailed, sourcePath);
			if (read == 0) break;
			if (!WriteFile(output.Value, buffer.data(), read, &written, nullptr) || read != written) return LastFailure(Error::WriteFailed, temporary);
			progress.CompletedBytes.fetch_add(read);
		}
		if (!FlushFileBuffers(output.Value)) return LastFailure(Error::WriteFailed, temporary);
		return {};
	}

	Failure Save(const Plan& plan, std::string_view chartText, bool backupOriginal, Progress& progress)
	{
		if (!plan.CanSave()) return plan.Failed.Code != Error::None ? plan.Failed : Failure { Error::MissingSource };
		if (progress.Cancel.load()) return { Error::Canceled };
		SaveFiles files;
		const fs::path chart = fs::u8path(plan.ChartPath), directory = chart.parent_path();
		std::vector<std::pair<fs::path, fs::path>> copies;
		progress.CurrentPhase = Phase::Copying;
		progress.CompletedBytes = 0;
		progress.TotalBytes = plan.CopyBytes;
		for (const Asset& asset : plan.Assets)
		{
			if (!asset.Selected || asset.Operation != Action::Copy) continue;
			fs::path temporary;
			if (Failure failed = StageCopy(asset, directory, files, temporary, progress); failed.Code != Error::None) return failed;
			copies.emplace_back(temporary, fs::u8path(asset.DestinationPath));
		}
		// Recheck reused files because the confirmation screen may have stayed open.
		for (const Asset& asset : plan.Assets)
		{
			if (!asset.Selected || asset.Operation != Action::Reuse) continue;
			if (std::any_of(copies.begin(), copies.end(), [&](const auto& copy) { return SamePath(copy.second, fs::u8path(asset.DestinationPath)); })) continue;
			Failure failed;
			if (!SameContent(fs::u8path(asset.SourcePath), fs::u8path(asset.DestinationPath), progress, failed))
				return failed.Code != Error::None ? failed : Failure { Error::DestinationChanged, asset.DestinationPath };
		}
		progress.CurrentPhase = Phase::Saving;
		fs::path temporaryChart;
		{
			Handle output(CreateTemporary(directory, files, temporaryChart));
			if (!output.Valid()) return LastFailure(Error::WriteFailed, directory);
			for (size_t offset = 0; offset < chartText.size();)
			{
				if (progress.Cancel.load()) return { Error::Canceled };
				const DWORD count = static_cast<DWORD>(std::min(BufferSize, chartText.size() - offset));
				DWORD written = 0;
				if (!WriteFile(output.Value, chartText.data() + offset, count, &written, nullptr) || count != written) return LastFailure(Error::WriteFailed, temporaryChart);
				offset += written;
			}
			if (!FlushFileBuffers(output.Value)) return LastFailure(Error::WriteFailed, temporaryChart);
		}
		for (const auto& [temporary, destination] : copies)
		{
			if (progress.Cancel.load()) return { Error::Canceled };
			if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return LastFailure(Error::DestinationChanged, destination);
			files.Moved(temporary);
			files.Published.push_back(destination);
		}
		if (progress.Cancel.load()) return { Error::Canceled };
		if (backupOriginal)
		{
			const fs::path backup = fs::u8path(plan.ChartPath + ".bak");
			if (CopyFileW(chart.c_str(), backup.c_str(), TRUE)) files.Published.push_back(backup);
			else if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_NOT_FOUND)
				return LastFailure(Error::BackupFailed, backup);
		}
		if (progress.Cancel.load()) return { Error::Canceled };
		// Rename a complete sibling file; a failed write never truncates the old TJA.
		if (!MoveFileExW(temporaryChart.c_str(), chart.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return LastFailure(Error::WriteFailed, chart);
		files.Moved(temporaryChart);
		files.Committed = true;
		return {};
	}
}
