#include "platform/platform.h"

#if defined(GAME_PLATFORM_IOS) || defined(__IOS__) || defined(__IPHONEOS__) || defined(IOS_BUILD)

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include <SDL.h>
#include <unistd.h>
#include <sys/stat.h>

#include "core/bstring.h"
#include "core/log.h"
#include "content/vfs.h"
#include "input/touch.h"

uint32_t platform_init_sdl_flags() {
    return SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS;
}

void platform_post_hint_init() {
    // Restrict orientations to landscape (matches CaesarPad / iPad design)
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    
    // Hide iOS home indicator bar during gameplay
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    
    // Disable accelerometer-as-joystick emulation to prevent unwanted drift
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    
    // Enable direct touch events
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
}

void platform_setup_begin() {
    // Default to direct touch mode on iOS for immediate tap response & long-press inspection
    touch_set_mode(TOUCH_MODE_DIRECT);
}

void platform_run_init_callback() {
}

void platform_run_per_frame_callback() {
}

int platform_poll_event(CoreEvent* event) {
    int result = SDL_PollEvent(reinterpret_cast<SDL_Event*>(event));
    if (result && event) {
        SDL_Event* sdl_ev = reinterpret_cast<SDL_Event*>(event);
        if (sdl_ev->type == SDL_APP_WILLENTERBACKGROUND) {
            logs::info("iOS: App entering background - triggering lifecycle safeguard");
        } else if (sdl_ev->type == SDL_APP_WILLENTERFOREGROUND) {
            logs::info("iOS: App returning to foreground");
        }
    }
    return result;
}

// Locate Documents directory where users drop Pharaoh game files via iOS Files app or iTunes/Finder
pcstr platform_request_initial_data_directory() {
    static char game_path[1024];
    @autoreleasepool {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if ([paths count] == 0) {
            return nullptr;
        }
        NSString *docsDir = [paths firstObject];
        NSFileManager *fm = [NSFileManager defaultManager];
        
        // Priority search locations inside app sandbox Documents directory:
        // 1. Documents/PharaohData
        // 2. Documents/Pharaoh
        // 3. Documents/data
        // 4. Documents root
        NSArray<NSString*> *candidateFolders = @[
            [docsDir stringByAppendingPathComponent:@"PharaohData"],
            [docsDir stringByAppendingPathComponent:@"Pharaoh"],
            [docsDir stringByAppendingPathComponent:@"data"],
            docsDir
        ];
        
        for (NSString *cand in candidateFolders) {
            NSString *campaign = [cand stringByAppendingPathComponent:@"campaign.txt"];
            NSString *eng = [cand stringByAppendingPathComponent:@"Pharaoh.eng"];
            NSString *sg2 = [cand stringByAppendingPathComponent:@"Pharaoh.sg2"];
            if ([fm fileExistsAtPath:campaign] || [fm fileExistsAtPath:eng] || [fm fileExistsAtPath:sg2]) {
                [cand getFileSystemRepresentation:game_path maxLength:sizeof(game_path)];
                logs::info("iOS: Detected Pharaoh game data at %s", game_path);
                return game_path;
            }
        }
        
        // Fallback: return Documents root so user can place files directly there
        [docsDir getFileSystemRepresentation:game_path maxLength:sizeof(game_path)];
        return game_path;
    }
}

void platform_append_startup_log(pcstr message) {
    if (message) {
        NSLog(@"[PharaohPad/iOS] %s", message);
    }
}

void platform_hide_startup_log() {
}

bool platform_run_main_loop(platform_pump_frame_cb pump_frame, platform_should_continue_cb should_continue) {
    while (should_continue()) {
        pump_frame();
    }
    return true;
}

void platform_resolve_user_directory(bstring512& dir) {
    @autoreleasepool {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if ([paths count] > 0) {
            NSString *docsDir = [paths firstObject];
            NSString *saveDir = [docsDir stringByAppendingPathComponent:@"Save"];
            
            // Create Save subdirectory if it doesn't exist
            [[NSFileManager defaultManager] createDirectoryAtPath:saveDir
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:nil];
            
            char c_path[512];
            [docsDir getFileSystemRepresentation:c_path maxLength:sizeof(c_path)];
            dir = c_path;
        } else {
            dir = ".";
        }
    }
}

void platform_t::open_url(pcstr url, pcstr prefix) {
    if (!url) return;
    @autoreleasepool {
        NSString *urlString = [NSString stringWithUTF8String:url];
        NSURL *nsUrl = [NSURL URLWithString:urlString];
        if (nsUrl) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [[UIApplication sharedApplication] openURL:nsUrl options:@{} completionHandler:nil];
            });
        }
    }
}

pcstr platform_t::get_steam_path() {
    return nullptr;
}

void platform_show_virtual_keyboard(const uint8_t *text, int max_length) {
    (void)text;
    (void)max_length;
    SDL_StartTextInput();
}

void platform_hide_virtual_keyboard(void) {
    SDL_StopTextInput();
}

#ifdef main
#undef main
#endif

extern "C" int SDL_UIKitRunApp(int argc, char *argv[], int (*mainFunction)(int, char **));
extern "C" int SDL_main(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    return SDL_UIKitRunApp(argc, argv, SDL_main);
}

#endif
