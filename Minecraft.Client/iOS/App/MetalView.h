#import <UIKit/UIKit.h>

// Plain UIView whose backing layer is a CAMetalLayer. The view controller
// drives its own CADisplayLink, so there's no need for MTKView and the
// extra redraw semantics it brings.
@interface MetalView : UIView
@end
