#import <UIKit/UIKit.h>

// Root view controller that hosts the game's render surface. Owns the
// CADisplayLink that drives the frame tick. For now the surface is a plain
// CALayer-backed UIView; once the renderer comes online we switch to MTKView.
@interface MinecraftViewController : UIViewController
@end
