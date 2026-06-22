#include "PresetFileDialog.h"

#import <AppKit/AppKit.h>

#include <filesystem>
#include <string>

namespace dx10::preset {
namespace {

std::string PathToUtf8(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
  const auto u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
#else
  return path.u8string();
#endif
}

NSString* NSStringFromUtf8(const std::string& text) {
  return [NSString stringWithUTF8String:text.c_str()];
}

std::string DefaultFileName(const std::string& initialName) {
  std::string name = initialName.empty() ? "MyPreset" : initialName;
  const std::string ext = ".dx10p";
  if (name.size() < ext.size() || name.substr(name.size() - ext.size()) != ext) {
    name += ext;
  }
  return name;
}

}  // namespace

bool PromptForPresetFile(void* nativeParent, FileDialogAction action,
                         const std::filesystem::path& initialDir,
                         const std::string& initialName,
                         std::filesystem::path& selectedPath) {
  (void) nativeParent;

  bool ok = false;
  std::string selectedUtf8;

  auto runDialog = [&]() {
    @autoreleasepool {
      const std::string dirUtf8 = PathToUtf8(initialDir);
      NSURL* directoryURL = [NSURL fileURLWithPath:NSStringFromUtf8(dirUtf8)];
      NSURL* selectedURL = nil;

      if (action == FileDialogAction::Save) {
        NSSavePanel* savePanel = [NSSavePanel savePanel];
        [savePanel setAllowedFileTypes:@[@"dx10p"]];
        [savePanel setAllowsOtherFileTypes:NO];
        [savePanel setDirectoryURL:directoryURL];
        [savePanel setNameFieldStringValue:NSStringFromUtf8(DefaultFileName(initialName))];
        [savePanel setFloatingPanel:YES];
        if ([savePanel runModal] != NSModalResponseOK) return;
        selectedURL = [savePanel URL];
      } else {
        NSOpenPanel* openPanel = [NSOpenPanel openPanel];
        [openPanel setAllowedFileTypes:@[@"dx10p"]];
        [openPanel setDirectoryURL:directoryURL];
        [openPanel setCanChooseFiles:YES];
        [openPanel setCanChooseDirectories:NO];
        [openPanel setResolvesAliases:YES];
        [openPanel setFloatingPanel:YES];
        if ([openPanel runModal] != NSModalResponseOK) return;
        selectedURL = [openPanel URL];
      }

      if (!selectedURL) return;
      selectedUtf8 = [[selectedURL path] UTF8String];
      ok = true;
    }
  };

  if ([NSThread isMainThread]) {
    runDialog();
  } else {
    dispatch_sync(dispatch_get_main_queue(), ^{
      runDialog();
    });
  }

  if (!ok) return false;
  selectedPath = std::filesystem::u8path(selectedUtf8);
  return true;
}

}  // namespace dx10::preset
