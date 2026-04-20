#pragma once

// Factory for GameSWF's render_handler backed by our Metal renderer.
// The returned pointer is owned by the caller; pass it to
// gameswf::set_render_handler() and delete on shutdown.

namespace gameswf { struct render_handler; }

gameswf::render_handler* create_render_handler_metal();

// Diagnostics: totals of draws issued across the full program lifetime.
// Both monotonic. Read from C to show on the status overlay.
#ifdef __cplusplus
extern "C" {
#endif
unsigned long long mcle_swf_total_mesh_strips(void);
unsigned long long mcle_swf_total_triangles(void);
unsigned long long mcle_swf_total_frames(void);

// Counters for paths we currently stub. If these climb while triangles stay
// at zero, the bundled SWF relies on features the Metal backend has not
// implemented yet (bitmaps, lines, masks).
unsigned long long mcle_swf_total_bitmap_draws(void);
unsigned long long mcle_swf_total_line_strips(void);
unsigned long long mcle_swf_total_masks(void);
unsigned long long mcle_swf_total_fill_bitmaps(void);
#ifdef __cplusplus
}
#endif
