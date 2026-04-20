#import "AppDelegate.h"
#import "MinecraftViewController.h"

#include "INP_iOS_Bridge.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {

    // Start listening for controllers before the view controller comes up so
    // we don't miss a connect notification from an already-paired pad.
    mcle_ios_input_init();

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [[MinecraftViewController alloc] init];
    [self.window makeKeyAndVisible];

    return YES;
}

- (void)applicationWillTerminate:(UIApplication*)application {
    mcle_ios_input_shutdown();
}

@end
