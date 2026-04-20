#import <Foundation/Foundation.h>
#include "STO_iOS_Paths.h"

#include <string>
#include <mutex>

namespace {

std::string resolve_dir(NSSearchPathDirectory kind) {
    NSArray<NSString*>* paths =
        NSSearchPathForDirectoriesInDomains(kind, NSUserDomainMask, YES);
    NSString* path = [paths firstObject] ?: @"";

    // Ensure the directory exists so callers can write to it unconditionally.
    if (path.length) {
        [[NSFileManager defaultManager] createDirectoryAtPath:path
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
    }
    const char* c = [path UTF8String];
    return c ? std::string(c) : std::string();
}

} // namespace

const char* ios_documents_dir(void) {
    static std::string cached;
    static std::once_flag once;
    std::call_once(once, []{ cached = resolve_dir(NSDocumentDirectory); });
    return cached.c_str();
}

const char* ios_app_support_dir(void) {
    static std::string cached;
    static std::once_flag once;
    std::call_once(once, []{ cached = resolve_dir(NSApplicationSupportDirectory); });
    return cached.c_str();
}

const char* ios_caches_dir(void) {
    static std::string cached;
    static std::once_flag once;
    std::call_once(once, []{ cached = resolve_dir(NSCachesDirectory); });
    return cached.c_str();
}

const char* ios_bundle_resources_dir(void) {
    static std::string cached;
    static std::once_flag once;
    std::call_once(once, []{
        NSString* p = [[NSBundle mainBundle] resourcePath] ?: @"";
        const char* c = [p UTF8String];
        cached = c ? std::string(c) : std::string();
    });
    return cached.c_str();
}

const char* ios_gamehdd_dir(void) {
    static std::string cached;
    static std::once_flag once;
    std::call_once(once, []{
        NSString* base = [NSString stringWithUTF8String:ios_app_support_dir()];
        NSString* p = [base stringByAppendingPathComponent:@"GameHDD"];
        [[NSFileManager defaultManager] createDirectoryAtPath:p
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        const char* c = [p UTF8String];
        cached = c ? std::string(c) : std::string();
    });
    return cached.c_str();
}
