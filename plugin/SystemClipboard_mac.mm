#include "SystemClipboard.h"

#import <AppKit/AppKit.h>

// macOS: NSPasteboard generalPasteboard read/write. Uses only autoreleased
// convenience constructors so it is safe under ARC and manual retain-release.
// OnMessage runs on the UI thread, so touching AppKit directly is fine.
namespace dx10 {

bool SetSystemClipboardText(const std::string& utf8) {
  @autoreleasepool {
    NSString* str = [NSString stringWithUTF8String:utf8.c_str()];
    if (str == nil) return false;
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setString:str forType:NSPasteboardTypeString] ? true : false;
  }
}

bool GetSystemClipboardText(std::string& utf8Out) {
  @autoreleasepool {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* str = [pb stringForType:NSPasteboardTypeString];
    if (str == nil) {
      utf8Out.clear();
      return false;
    }
    const char* c = [str UTF8String];
    utf8Out = c ? c : "";
    return true;
  }
}

}  // namespace dx10
