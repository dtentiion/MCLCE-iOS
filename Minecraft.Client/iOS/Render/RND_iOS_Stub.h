#pragma once

// Absolute bare minimum renderer interface. The upstream game calls into
// 4J_Render's C-style API. That API is huge and half of it is bound to D3D11.
// We will grow this header as we wire things up. For now it exposes the
// lifecycle hooks the app shell needs so we can verify the build links.

#ifdef __cplusplus
extern "C" {
#endif

// Called once from the main thread after the Metal / GL ES view is ready.
// Returns 0 on success, non-zero on failure.
int mcle_render_init(void* metal_layer_or_glcontext, int width, int height);

// Called when the view resizes (rotation, split view).
void mcle_render_resize(int width, int height);

// Called once per frame from the display link / CADisplayLink callback.
void mcle_render_frame(void);

// Called on app teardown.
void mcle_render_shutdown(void);

#ifdef __cplusplus
}
#endif
