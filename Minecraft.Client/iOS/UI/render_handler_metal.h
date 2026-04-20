#pragma once

// Factory for GameSWF's render_handler backed by our Metal renderer.
// The returned pointer is owned by the caller; pass it to
// gameswf::set_render_handler() and delete on shutdown.

namespace gameswf { struct render_handler; }

gameswf::render_handler* create_render_handler_metal();
