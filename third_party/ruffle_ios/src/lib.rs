// ruffle_ios: FFI wrapper around Ruffle for the MCLCE-iOS app.
//
// Skeleton pass. Exposes three C entry points so the Objective-C++ side can
// link today. Each is stubbed until we pull the actual Ruffle dep in and
// implement the RenderBackend against our Metal context.
//
// All functions are `extern "C"` and `#[no_mangle]` so the linker finds
// them by the exact names the iOS app looks up.

#![allow(clippy::missing_safety_doc)]

use std::os::raw::{c_char, c_int};

/// Initialize the Ruffle runtime. Returns 0 on success, non-zero on error.
#[no_mangle]
pub extern "C" fn ruffle_ios_init() -> c_int {
    eprintln!("[ruffle_ios] init (stub)");
    0
}

/// Shut down the runtime.
#[no_mangle]
pub extern "C" fn ruffle_ios_shutdown() {}

/// Load a SWF from the given filesystem path. Returns 0 on success.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_load_swf(path: *const c_char) -> c_int {
    if path.is_null() {
        return 1;
    }
    // Just demonstrate we can round-trip a C string. Real load happens when
    // the ruffle dep is wired.
    let cstr = std::ffi::CStr::from_ptr(path);
    if let Ok(s) = cstr.to_str() {
        eprintln!("[ruffle_ios] load_swf (stub): {s}");
    }
    0
}

/// Advance playback by `dt_seconds` and draw the current frame into the
/// viewport. A no-op today; the real call wires into Ruffle once added.
#[no_mangle]
pub extern "C" fn ruffle_ios_tick(_dt_seconds: f32, _vp_w: c_int, _vp_h: c_int) {}

/// Probe: returns a build-identifying integer so the iOS side can tell the
/// Rust crate was actually linked in. Handy for on-device diagnostics while
/// the real runtime is being wired.
#[no_mangle]
pub extern "C" fn ruffle_ios_magic() -> c_int {
    0x5255_4646  // "RUFF"
}

/// Force a reference to ruffle's `swf` crate so the linker can't garbage
/// collect it before we have other code using it. Returns the declared
/// SWF version from the header struct -- proves the parser crate's types
/// are reachable from iOS ARM64.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_swf_probe(data: *const u8, len: usize) -> c_int {
    if data.is_null() || len < 8 {
        return -1;
    }
    let buf = std::slice::from_raw_parts(data, len);
    match swf::decompress_swf(buf) {
        Ok(decompressed) => decompressed.header.version() as c_int,
        Err(_) => -2,
    }
}

/// Probe the ruffle_render crate by referencing a known type. Returns the
/// numeric tag of ruffle_render::quality::StageQuality::High, which is
/// stable across patch versions. Succeeding here proves the render-backend
/// trait crate compiles cleanly for iOS ARM64 and its symbols are linked.
#[no_mangle]
pub extern "C" fn ruffle_ios_render_probe() -> c_int {
    use ruffle_render::quality::StageQuality;
    StageQuality::High as c_int
}
