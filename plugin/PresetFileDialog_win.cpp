#include "PresetFileDialog.h"

#ifdef _WIN32

#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>
#include <shobjidl.h>

namespace dx10::preset {
namespace {

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0) return {};
  std::wstring wide(static_cast<size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
  return wide;
}

// Append ".dx10p" if the supplied name doesn't already end with it.
std::wstring DefaultFileName(const std::string& initialName) {
  std::wstring name = Utf8ToWide(initialName.empty() ? "MyPreset" : initialName);
  const std::wstring ext = L".dx10p";
  std::wstring lower = name;
  for (wchar_t& ch : lower) ch = static_cast<wchar_t>(towlower(ch));
  if (lower.size() < ext.size() || lower.substr(lower.size() - ext.size()) != ext) {
    name += ext;
  }
  return name;
}

}  // namespace

// IFileSaveDialog / IFileOpenDialog (Vista+). SetFolder() overrides the shell MRU
// so the dialog always opens at the requested initial directory.
bool PromptForPresetFile(void* nativeParent, FileDialogAction action,
                         const std::filesystem::path& initialDir,
                         const std::string& initialName,
                         std::filesystem::path& selectedPath) {
  const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool comInitNeedsRelease = SUCCEEDED(comInit);

  bool ok = false;
  IFileDialog* dialog = nullptr;
  const CLSID clsid = (action == FileDialogAction::Save) ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
  const IID iid = (action == FileDialogAction::Save) ? IID_IFileSaveDialog : IID_IFileOpenDialog;
  HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid,
                                reinterpret_cast<void**>(&dialog));

  if (SUCCEEDED(hr) && dialog) {
    const COMDLG_FILTERSPEC filters[] = {
        {L"DX10R Preset (*.dx10p)", L"*.dx10p"},
        {L"All Files (*.*)", L"*.*"},
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"dx10p");

    const std::wstring defaultName = DefaultFileName(initialName);
    if (!defaultName.empty()) dialog->SetFileName(defaultName.c_str());

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
    options |= (action == FileDialogAction::Save) ? FOS_OVERWRITEPROMPT
                                                  : (FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetOptions(options);

    if (!initialDir.empty()) {
      IShellItem* folderItem = nullptr;
      const std::wstring initialDirW = initialDir.wstring();
      if (SUCCEEDED(SHCreateItemFromParsingName(initialDirW.c_str(), nullptr,
                                                IID_PPV_ARGS(&folderItem)))) {
        dialog->SetFolder(folderItem);
        folderItem->Release();
      }
    }

    hr = dialog->Show(static_cast<HWND>(nativeParent));
    if (SUCCEEDED(hr)) {
      IShellItem* item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item)) && item) {
        PWSTR pathW = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)) && pathW) {
          selectedPath = std::filesystem::path(pathW);
          CoTaskMemFree(pathW);
          ok = true;
        }
        item->Release();
      }
    }
    dialog->Release();
  }

  if (comInitNeedsRelease) CoUninitialize();
  return ok;
}

}  // namespace dx10::preset

#endif  // _WIN32
