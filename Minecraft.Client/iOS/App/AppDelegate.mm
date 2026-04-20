#import "AppDelegate.h"
#import "MinecraftViewController.h"

#include "INP_iOS_Bridge.h"
#include "mcle_swf_bridge.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {

    // Start listening for controllers before the view controller comes up so
    // we don't miss a connect notification from an already-paired pad.
    mcle_ios_input_init();

    // Bring up the SWF runtime (render_handler + player). If this fails the
    // app still runs, just without future SWF-driven UI. The log line in
    // mcle_swf_init tells us what happened.
    if (mcle_swf_init() != 0) {
        NSLog(@"[AppDelegate] mcle_swf_init reported failure; continuing");
    }

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [[MinecraftViewController alloc] init];
    [self.window makeKeyAndVisible];

    return YES;
}

- (void)applicationWillTerminate:(UIApplication*)application {
    mcle_swf_shutdown();
    mcle_ios_input_shutdown();
}

@end
