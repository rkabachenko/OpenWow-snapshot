#include "mobile_ios_platform.h"

#include "openwow/foundation/diagnostics/log_snapshot.h"
#include "openwow/foundation/diagnostics/logging.h"

#import <UIKit/UIKit.h>

#include <exception>
#include <string>

namespace openwow::client::mobile {
namespace {

// Accessed only on the UIKit/game main thread. One preparation or share sheet
// at a time also bounds the number of temporary files and background jobs.
bool export_active = false;

NSString* ExportText(NSString* chinese, NSString* english) {
  return [NSLocale.preferredLanguages.firstObject hasPrefix:@"zh"] ? chinese : english;
}

void LogExport(const openwow::diagnostics::LogLevel level,
               const std::string& context) {
  openwow::diagnostics::Log(level, "Log export " + context);
}

void RemoveExport(NSURL* directory) {
  NSError* error = nil;
  if (![NSFileManager.defaultManager removeItemAtURL:directory error:&error] &&
      error.code != NSFileNoSuchFileError) {
    LogExport(openwow::diagnostics::LogLevel::kWarn,
              "stage=cleanup reason=" + std::string(error.localizedDescription.UTF8String));
  }
}

bool CanPresent(UIViewController* presenter) {
  UIWindow* window = presenter.view.window;
  if (window == nil || presenter.isBeingDismissed) return false;
  return window.windowScene != nil
      ? window.windowScene.activationState == UISceneActivationStateForegroundActive
      : UIApplication.sharedApplication.applicationState == UIApplicationStateActive;
}

void ShowExportError(UIViewController* presenter, NSString* detail) {
  export_active = false;
  if (!CanPresent(presenter) || presenter.presentedViewController != nil) return;
  export_active = true;
  UIAlertController* alert = [UIAlertController
      alertControllerWithTitle:ExportText(@"日志导出失败", @"Log export failed")
      message:detail preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:ExportText(@"好", @"OK")
      style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { export_active = false; }]];
  [presenter presentViewController:alert animated:YES completion:nil];
}

void ShareExport(UIViewController* presenter, NSURL* file, NSURL* directory) {
  if (!CanPresent(presenter) || presenter.presentedViewController != nil) {
    LogExport(openwow::diagnostics::LogLevel::kInfo,
              "stage=share result=cancelled reason=presenter-unavailable");
    RemoveExport(directory);
    export_active = false;
    return;
  }
  UIActivityViewController* activity = [[UIActivityViewController alloc]
      initWithActivityItems:@[file] applicationActivities:nil];
  UIPopoverPresentationController* popover = activity.popoverPresentationController;
  popover.sourceView = presenter.view;
  const CGRect bounds = presenter.view.bounds;
  popover.sourceRect = CGRectMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds), 1, 1);
  popover.permittedArrowDirections = 0;
  __weak UIViewController* weak_presenter = presenter;
  activity.completionWithItemsHandler = ^(UIActivityType, BOOL completed, NSArray*, NSError* error) {
    dispatch_async(dispatch_get_main_queue(), ^{
      RemoveExport(directory);
      export_active = false;
      LogExport(error ? openwow::diagnostics::LogLevel::kWarn
                      : openwow::diagnostics::LogLevel::kInfo,
                "stage=share result=" + std::string(error ? "failed" : completed ? "completed" : "cancelled") +
                    (error ? " reason=" + std::string(error.localizedDescription.UTF8String) : ""));
      if (error) {
        UIViewController* owner = weak_presenter;
        if (owner.presentedViewController != nil) {
          [owner dismissViewControllerAnimated:YES completion:^{
            ShowExportError(owner, error.localizedDescription);
          }];
        } else {
          ShowExportError(owner, error.localizedDescription);
        }
      }
    });
  };
  [presenter presentViewController:activity animated:YES completion:^{
    LogExport(openwow::diagnostics::LogLevel::kInfo, "stage=share result=presented");
  }];
}

}

bool IsLogExportActive() noexcept { return export_active; }

void PresentLogExport(void* native_window) {
  if (![NSThread isMainThread]) {
    LogExport(openwow::diagnostics::LogLevel::kError,
              "stage=request reason=not-main-thread");
    return;
  }
  if (export_active) {
    LogExport(openwow::diagnostics::LogLevel::kInfo, "stage=request result=already-active");
    return;
  }
  UIWindow* window = (__bridge UIWindow*)native_window;
  UIViewController* presenter = window.rootViewController;
  if (presenter == nil || !CanPresent(presenter) || presenter.presentedViewController != nil) {
    LogExport(openwow::diagnostics::LogLevel::kWarn,
              "stage=request reason=presenter-unavailable");
    return;
  }
  export_active = true;
  UIAlertController* progress = [UIAlertController
      alertControllerWithTitle:ExportText(@"正在准备日志…", @"Preparing logs…")
      message:ExportText(@"准备完成后，可保存到文件或选择其他分享方式。",
                         @"You can save the log to Files or choose a sharing app when it is ready.")
      preferredStyle:UIAlertControllerStyleAlert];
  [presenter presentViewController:progress animated:YES completion:^{
    NSString* version = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"unknown";
    NSString* build = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"] ?: @"unknown";
    LogExport(openwow::diagnostics::LogLevel::kInfo,
              "stage=prepare app_version=" + std::string(version.UTF8String) +
                  " app_build=" + build.UTF8String);
    NSURL* root = [[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
        URLByAppendingPathComponent:@"OpenWoWLogExports" isDirectory:YES];
    NSString* identifier = NSUUID.UUID.UUIDString;
    NSURL* directory = [root URLByAppendingPathComponent:identifier isDirectory:YES];
    NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
    formatter.locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
    formatter.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
    formatter.dateFormat = @"yyyyMMdd-HHmmss";
    NSString* name = [NSString stringWithFormat:@"OpenWoW-iOS-%@Z-%@.log",
        [formatter stringFromDate:NSDate.date], [identifier substringToIndex:8]];
    NSURL* file = [directory URLByAppendingPathComponent:name];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
      @autoreleasepool {
        std::string failure;
        std::uintmax_t bytes = 0;
        NSError* error = nil;
        NSFileManager* files = NSFileManager.defaultManager;
        // A previous process may have been terminated while its share sheet
        // was open. This directory contains only copies owned by this exporter.
        if ([files fileExistsAtPath:root.path] &&
            ![files removeItemAtURL:root error:&error]) {
          failure = "prepare-cleanup: " + std::string(error.localizedDescription.UTF8String);
        } else if (![files createDirectoryAtURL:directory withIntermediateDirectories:YES
                         attributes:nil error:&error]) {
          failure = "prepare-directory: " + std::string(error.localizedDescription.UTF8String);
        } else {
          try {
            const auto result = openwow::diagnostics::CopyCurrentLogSnapshot(file.fileSystemRepresentation);
            failure = result.error;
            bytes = result.bytes;
          } catch (const std::exception& exception) {
            failure = std::string("copy: ") + exception.what();
          }
        }
        LogExport(failure.empty() ? openwow::diagnostics::LogLevel::kInfo
                                  : openwow::diagnostics::LogLevel::kWarn,
                  "stage=copy bytes=" + std::to_string(bytes) +
                      (failure.empty() ? " result=completed" : " result=failed reason=" + failure));
        const bool failed = !failure.empty();
        dispatch_async(dispatch_get_main_queue(), ^{
          [progress dismissViewControllerAnimated:YES completion:^{
            if (failed) {
              RemoveExport(directory);
              ShowExportError(presenter, ExportText(
                  @"暂时无法读取或保存日志文件，请稍后重试。",
                  @"The log could not be read or saved. Please try again."));
            } else {
              ShareExport(presenter, file, directory);
            }
          }];
        });
      }
    });
  }];
}

}
