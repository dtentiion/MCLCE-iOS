#pragma once

#ifdef __OBJC__
#  import <Metal/Metal.h>
#  import <QuartzCore/CAMetalLayer.h>
#endif

// Shared Metal state used by both the placeholder triangle renderer and
// the GameSWF render_handler. Keeps the device / queue / drawable in one
// place so the two draw paths can coexist on a single frame.
//
// Usage (called from the view controller's display link):
//   mcle_metal_frame_begin(layer, width, height);   // starts command buf + pass
//   ...drawing...                                    // encoders on the shared pass
//   mcle_metal_frame_end();                          // presents + commits

#ifdef __cplusplus
extern "C" {
#endif

// Lazy one-time init of device + command queue. Idempotent.
int  mcle_metal_ensure_device(void);

// True if the Metal device is ready.
int  mcle_metal_available(void);

// Configure the layer with our device + pixel format. Call once after the
// view is created, or whenever the drawable size changes.
void mcle_metal_attach_layer(void* ca_metal_layer, int width, int height);

// Start a new frame: grabs a drawable, opens a command buffer + render pass.
// Clears to `bg_rgba` (8-bit per channel). Returns 0 on success.
int  mcle_metal_frame_begin(float r, float g, float b, float a);

// Returns the open render command encoder for this frame, or NULL if there
// is no frame in progress. Callable from C++ as `void*` and cast inside .mm.
void* mcle_metal_current_encoder(void);

// Returns the current drawable size in pixels.
void  mcle_metal_current_size(int* out_width, int* out_height);

// Commit + present + teardown the current frame.
void mcle_metal_frame_end(void);

#ifdef __cplusplus
}
#endif
