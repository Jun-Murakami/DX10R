#pragma once

#include <filesystem>
#include <string>

namespace dx10::preset {

enum class FileDialogAction {
  Open,
  Save,
};

// Native Save/Open dialog for a single DX10R patch file (.dx10p). Windows uses
// IFileSaveDialog/IFileOpenDialog (Vista+); macOS uses NSSavePanel/NSOpenPanel.
// Returns true and fills selectedPath on confirm; false on cancel/error.
bool PromptForPresetFile(void* nativeParent, FileDialogAction action,
                         const std::filesystem::path& initialDir,
                         const std::string& initialName,
                         std::filesystem::path& selectedPath);

}  // namespace dx10::preset
