#import "MetalView.h"
#import <QuartzCore/CAMetalLayer.h>

@implementation MetalView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

@end
