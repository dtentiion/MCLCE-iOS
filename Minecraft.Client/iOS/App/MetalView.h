#import <UIKit/UIKit.h>

// Plain UIView whose backing layer is a CAMetalLayer. We drive our own
// CADisplayLink from the view controller, so we do not need MTKView and the
// extra redraw semantics it brings.
@interface MetalView : UIView
@end
