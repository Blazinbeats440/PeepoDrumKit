#include "chart_editor.h"
#include "chart_editor_i18n.h"
#include "core_io.h"
#include <filesystem>

namespace PeepoDrumKit
{
	static cstr AssetSaveErrorText(AssetSave::Error error)
	{
		switch (error)
		{
		case AssetSave::Error::MissingSource: return UI_Str("ASSET_SAVE_MISSING");
		case AssetSave::Error::DestinationChanged: return UI_Str("ASSET_SAVE_CHANGED");
		case AssetSave::Error::BackupFailed: return UI_Str("ASSET_SAVE_BACKUP_FAILED");
		default: return UI_Str("ASSET_SAVE_FAILED");
		}
	}

	void ChartEditor::ShowChartSaveFailure(const AssetSave::Failure& failure)
	{
		std::string message = AssetSaveErrorText(failure.Code);
		if (!failure.Path.empty()) message += "\n\n" + failure.Path;
		Shell::ShowMessageBox(message, UI_Str("ASSET_SAVE_TITLE"), Shell::MessageBoxButtons::OK,
			Shell::MessageBoxIcon::Warning, ApplicationHost::GlobalState.NativeWindowHandle);
	}

	void ChartEditor::FinishChartSave(const AssetSave::Plan& plan, b8 reloadAssets)
	{
		context.Chart.SongFileName = plan.Assets[0].SavedReference;
		context.Chart.SongJacket = plan.Assets[1].SavedReference;
		for (auto& [key, value] : context.Chart.OtherMetadata)
			if (ASCII::MatchesInsensitive(key, "BGMOVIE")) value = plan.Assets[2].SavedReference;
		context.ChartFilePath = plan.ChartPath;
		createBackupOfOriginalTJABeforeOverwriteSave = false;
		for (auto& recording : gamePreview.RecordedVideoRoutes)
		{
			if (recording.Course && recording.Changes == context.Undo.NumberOfChangesMade) recording.Changes = 0;
			else recording = {};
		}
		context.Undo.ClearChangesWereMade();
		PersistentApp.RecentFiles.Add(plan.ChartPath);
		if (reloadAssets)
		{
			StartAsyncLoadingSongAudioFile(AssetSave::ResolveSource(context.Chart.SongFileName, context.ChartFilePath), false);
			StartAsyncLoadingSongJacketFile(AssetSave::ResolveSource(context.Chart.SongJacket, context.ChartFilePath));
		}
		else
		{
			if (!context.SongSourceFilePath.empty()) context.SongSourceFilePath = plan.Assets[0].SourcePath;
			if (!context.SongJacketFilePath.empty()) context.SongJacketFilePath = plan.Assets[1].SourcePath;
		}
	}

	void ChartEditor::StartAssetSave(std::string_view chartPath)
	{
		if (assetSave.Active || importChartFuture.valid() || videoExport.Exporting || videoExport.Preparing) return;
		context.SetIsPlayback(false);
		context.Undo.FlushAndExecuteEndOfFrameCommands();
		assetSave = {};
		assetSave.Active = assetSave.OpenOnNextFrame = true;
		assetSave.OldChartPath = context.ChartFilePath;
		assetSave.ChartPath = chartPath;
		assetSave.Inputs = {
			{ context.Chart.SongFileName }, { context.Chart.SongJacket },
			{ ReadBackgroundMovieDefinition(context.Chart.OtherMetadata).FileName },
		};
		PrepareAssetSave();
	}

	void ChartEditor::PrepareAssetSave()
	{
		assetSave.Failed = {};
		assetSave.Progress = std::make_shared<AssetSave::Progress>();
		assetSave.PrepareFuture = std::async(std::launch::async,
			[oldPath = assetSave.OldChartPath, newPath = assetSave.ChartPath, inputs = assetSave.Inputs, progress = assetSave.Progress]
		{
			try { return AssetSave::Prepare(oldPath, newPath, inputs, *progress); }
			catch (const std::exception&) { AssetSave::Plan plan; plan.Failed = { AssetSave::Error::ReadFailed, newPath }; return plan; }
		});
	}

	void ChartEditor::DrawAssetSavePopup()
	{
		bool close = false, saved = false;
		if (assetSave.PrepareFuture.valid() && assetSave.PrepareFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			assetSave.Plan = assetSave.PrepareFuture.get();
			assetSave.Failed = assetSave.Plan.Failed;
			close = assetSave.Progress->Cancel.load() || assetSave.Failed.Code == AssetSave::Error::Canceled;
		}
		if (assetSave.SaveFuture.valid() && assetSave.SaveFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			assetSave.Failed = assetSave.SaveFuture.get();
			saved = assetSave.Failed.Code == AssetSave::Error::None;
			close = saved || assetSave.Progress->Cancel.load() || assetSave.Failed.Code == AssetSave::Error::Canceled;
		}
		if (assetSave.OpenOnNextFrame && !saveConfirmationPopup.OpenOnNextFrame && Gui::GetCurrentContext()->OpenPopupStack.Size == 0)
		{
			assetSave.OpenOnNextFrame = false;
			Gui::OpenPopup(UI_WindowName("ASSET_SAVE_TITLE"));
		}
		const auto* viewport = Gui::GetMainViewport();
		Gui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, vec2(0.5f));
		Gui::SetNextWindowSize(GuiScale(vec2(740.0f, 0.0f)), ImGuiCond_Appearing);
		if (Gui::BeginPopupModal(UI_WindowName("ASSET_SAVE_TITLE"), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
		{
			if (close) Gui::CloseCurrentPopup();
			const bool preparing = assetSave.PrepareFuture.valid(), saving = assetSave.SaveFuture.valid();
			const bool busy = preparing || saving;
			Gui::PushTextWrapPos(GuiScale(710.0f));
			Gui::TextUnformatted(assetSave.ChartPath.c_str());
			Gui::TextUnformatted(UI_Str("ASSET_SAVE_DESCRIPTION"));
			Gui::Separator();
			bool changed = false, hasAsset = false;
			Gui::BeginDisabled(busy || close);
			Gui::BeginChild("AssetList", GuiScale(vec2(710.0f, 380.0f)), true);
			for (size_t index = 0; index < assetSave.Inputs.size(); index++)
			{
				auto& input = assetSave.Inputs[index];
				if (input.Reference.empty()) continue;
				hasAsset = true;
				const AssetSave::Asset* asset = index < assetSave.Plan.Assets.size() ? &assetSave.Plan.Assets[index] : nullptr;
				const cstr labels[] = { UI_Str("ASSET_SAVE_AUDIO"), UI_Str("ASSET_SAVE_JACKET"), UI_Str("ASSET_SAVE_MOVIE") };
				Gui::PushID(static_cast<int>(index));
				changed |= Gui::Checkbox(labels[index], &input.Selected);
				Gui::SameLine();
				if (Gui::Button(UI_Str("ASSET_SAVE_RESELECT")))
				{
					Shell::FileDialog dialog;
					dialog.InTitle = UI_Str("ASSET_SAVE_RESELECT");
					std::string* lastDirectories[] = { &PersistentApp.RelatedFileDirectories.Audio, &PersistentApp.RelatedFileDirectories.Jacket, &PersistentApp.RelatedFileDirectories.Movie };
					const std::string directory = GetRelatedFileOpenDirectory(input.SourceOverride.empty() ? input.Reference : input.SourceOverride,
						assetSave.OldChartPath, *lastDirectories[index]);
					dialog.InInitialDirectory = directory;
					dialog.InFilters = { { Shell::AllFilesFilterName, Shell::AllFilesFilterSpec } };
					dialog.InParentWindowHandle = ApplicationHost::GlobalState.NativeWindowHandle;
					if (dialog.OpenRead() == Shell::FileDialogResult::OK)
					{
						input.SourceOverride = dialog.OutFilePath;
						RememberRelatedFileDirectory(dialog.OutFilePath, {}, *lastDirectories[index]);
						changed = true;
					}
				}
				const std::string source = AssetSave::ResolveSource(input.SourceOverride.empty() ? input.Reference : input.SourceOverride, assetSave.OldChartPath);
				Gui::TextWrapped("%s: %s", UI_Str("ASSET_SAVE_SOURCE"), source.c_str());
				if (asset)
				{
					if (asset->Missing) Gui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", UI_Str("ASSET_SAVE_MISSING"));
					else Gui::Text("%s: %.2f MiB", UI_Str("ASSET_SAVE_SIZE"), static_cast<double>(asset->Size) / (1024 * 1024));
					Gui::TextWrapped("%s: %s", UI_Str("ASSET_SAVE_DESTINATION"), asset->SavedReference.c_str());
					if (asset->Selected && asset->Operation == AssetSave::Action::Reuse) Gui::TextUnformatted(UI_Str("ASSET_SAVE_REUSE"));
				}
				Gui::Separator();
				Gui::PopID();
			}
			if (!hasAsset) Gui::TextUnformatted(UI_Str("ASSET_SAVE_NO_ASSETS"));
			Gui::EndChild();
			Gui::EndDisabled();
			if (changed) PrepareAssetSave();
			Gui::Text("%s: %.2f MiB", UI_Str("ASSET_SAVE_TOTAL"), static_cast<double>(assetSave.Plan.CopyBytes) / (1024 * 1024));
			if (assetSave.Failed.Code != AssetSave::Error::None && assetSave.Failed.Code != AssetSave::Error::Canceled)
			{
				Gui::TextWrapped("%s", AssetSaveErrorText(assetSave.Failed.Code));
				Gui::TextWrapped("%s", assetSave.Failed.Path.c_str());
				if (!busy && Gui::Button(UI_Str("ASSET_SAVE_RETRY"))) { PrepareAssetSave(); changed = true; }
			}
			if (busy || changed)
			{
				if (assetSave.Progress->Cancel.load()) Gui::TextUnformatted(UI_Str("ASSET_SAVE_CANCELING"));
				else if (preparing || changed) Gui::TextUnformatted(UI_Str("ASSET_SAVE_PREPARING"));
				else
				{
					Gui::TextUnformatted(UI_Str("ASSET_SAVE_PROGRESS"));
					const auto total = assetSave.Progress->TotalBytes.load(), done = assetSave.Progress->CompletedBytes.load();
					const float fraction = total == 0 ? 0.0f : std::min(1.0f, static_cast<float>(static_cast<double>(done) / total));
					Gui::ProgressBar(fraction, vec2(-1.0f, 0.0f));
				}
			}
			Gui::BeginDisabled(busy || changed || close || !assetSave.Plan.CanSave() ||
				assetSave.Failed.Code != AssetSave::Error::None || loadSongFuture.valid() || loadJacketFuture.valid());
			if (Gui::Button(UI_Str("ASSET_SAVE_START")))
			{
				std::string text = BuildChartSaveText(assetSave.Plan);
				assetSave.Progress = std::make_shared<AssetSave::Progress>();
				const bool backup = createBackupOfOriginalTJABeforeOverwriteSave;
				assetSave.SaveFuture = std::async(std::launch::async,
					[plan = assetSave.Plan, text = std::move(text), backup, progress = assetSave.Progress]
				{
					try { return AssetSave::Save(plan, text, backup, *progress); }
					catch (const std::exception&) { return AssetSave::Failure { AssetSave::Error::WriteFailed, plan.ChartPath }; }
				});
			}
			Gui::EndDisabled();
			Gui::SameLine();
			if (Gui::Button(UI_Str("ACT_MSGBOX_CANCEL")) || Gui::IsAnyPressed(*Settings.Input.Dialog_Cancel, false))
			{
				assetSave.Progress->Cancel = true;
				if (!assetSave.PrepareFuture.valid() && !assetSave.SaveFuture.valid()) { close = true; Gui::CloseCurrentPopup(); }
			}
			Gui::PopTextWrapPos();
			Gui::EndPopup();
		}
		if (close)
		{
			if (saved) FinishChartSave(assetSave.Plan, true);
			auto onSuccess = saved ? std::move(assetSave.OnSuccess) : std::function<void()> {};
			assetSave = {};
			if (onSuccess) onSuccess();
		}
	}
}
