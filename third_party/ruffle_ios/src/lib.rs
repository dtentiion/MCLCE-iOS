// ruffle_ios: FFI wrapper around Ruffle for the MCLCE-iOS app.
//
// Exposes a compact C ABI so the Objective-C++ app can drive a ruffle_core
// Player without having to speak Rust. Every function is `extern "C"` with
// `#[no_mangle]` so the linker finds it by the exact C name.
//
// The rendering path is still the NullRenderer provided by Ruffle; our
// Metal backend goes in next. All other backends (audio, navigator, etc.)
// stay at their safe Null defaults for now.

#![allow(clippy::missing_safety_doc)]

use std::os::raw::{c_char, c_int};
use std::sync::{Arc, Mutex};

use ruffle_common::tag_utils::SwfMovie;
use ruffle_core::external::{ExternalInterfaceProvider, FsCommandProvider, Value as ExtValue};
use ruffle_core::context::UpdateContext;
use ruffle_core::{Player, PlayerBuilder};

// --- Captured ExternalInterface log -----------------------------------------
//
// LCE's menu calls ExternalInterface.call(name, args) repeatedly during
// init. Stubbing each call by name is how we eventually get the menu to
// render its visible content. To find out what names it calls we log every
// invocation into this global ring buffer and expose it to the iOS app.

static EXTINT_LOG: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());

pub struct LoggingExternalInterface;

impl ExternalInterfaceProvider for LoggingExternalInterface {
    fn call_method(&self, _context: &mut UpdateContext<'_>, name: &str, args: &[ExtValue]) -> ExtValue {
        let rendered_args: Vec<String> = args.iter().map(|v| format!("{v:?}")).collect();
        let line = format!("{name}({})", rendered_args.join(", "));
        if let Ok(mut log) = EXTINT_LOG.lock() {
            // Cap memory to a reasonable upper bound; drop oldest.
            if log.len() >= 256 {
                log.remove(0);
            }
            log.push(line);
        }
        // For now, every call returns Null. Specific menus may need better
        // responses; we wire those once we know what the names are.
        ExtValue::Null
    }

    fn on_callback_available(&self, _name: &str) {}

    fn get_id(&self) -> Option<String> { None }
}

pub struct LoggingFsCommands;

impl FsCommandProvider for LoggingFsCommands {
    fn on_fs_command(&self, command: &str, args: &str) -> bool {
        let line = format!("fscommand::{command}({args})");
        if let Ok(mut log) = EXTINT_LOG.lock() {
            if log.len() >= 256 { log.remove(0); }
            log.push(line);
        }
        true
    }
}

// --- Boxed handle shared across the C boundary -------------------------------

/// Opaque handle type the C side receives from `ruffle_ios_player_create`.
/// Storing an `Arc<Mutex<Player>>` so Ruffle's internal self-reference
/// stays valid; the Box is the heap anchor the C pointer points at.
pub struct PlayerHandle {
    player: Arc<Mutex<Player>>,
}

fn to_handle(p: Arc<Mutex<Player>>) -> *mut PlayerHandle {
    Box::into_raw(Box::new(PlayerHandle { player: p }))
}

unsafe fn borrow_handle<'a>(raw: *mut PlayerHandle) -> Option<&'a PlayerHandle> {
    if raw.is_null() { None } else { Some(&*raw) }
}

// --- Diagnostic probes (kept so older builds keep building) ------------------

#[no_mangle]
pub extern "C" fn ruffle_ios_init() -> c_int {
    eprintln!("[ruffle_ios] init");
    0
}

#[no_mangle]
pub extern "C" fn ruffle_ios_shutdown() {}

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_load_swf(_path: *const c_char) -> c_int {
    // Old path stub. New callers go through player_create + load_bytes.
    0
}

#[no_mangle]
pub extern "C" fn ruffle_ios_tick(_dt_seconds: f32, _vp_w: c_int, _vp_h: c_int) {}

#[no_mangle]
pub extern "C" fn ruffle_ios_magic() -> c_int {
    0x5255_4646
}

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

#[no_mangle]
pub extern "C" fn ruffle_ios_render_probe() -> c_int {
    use ruffle_render::quality::StageQuality;
    StageQuality::High as c_int
}

/// Probe: create a wgpu Surface over the given CAMetalLayer, then a
/// compatible Adapter and Device. `layer_ptr` must be a pointer to a live
/// CAMetalLayer obtained on the Obj-C side.
/// Returns:
///   1  = full chain set up (surface + adapter + device)
///   -1 = layer_ptr is null
///   -2 = instance / surface creation failed
///   -3 = no adapter
///   -4 = no device
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_surface_probe(layer_ptr: *mut std::ffi::c_void) -> c_int {
    if layer_ptr.is_null() { return -1; }

    let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends: wgpu::Backends::METAL,
        ..Default::default()
    });

    let surface = unsafe {
        instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::CoreAnimationLayer(layer_ptr))
    };
    let surface = match surface {
        Ok(s) => s,
        Err(e) => {
            eprintln!("[ruffle_ios] create_surface_unsafe failed: {e:?}");
            return -2;
        }
    };

    let adapter = pollster::block_on(instance.request_adapter(
        &wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::default(),
            force_fallback_adapter: false,
            compatible_surface: Some(&surface),
        },
    ));
    let adapter = match adapter {
        Ok(a) => a,
        Err(e) => {
            eprintln!("[ruffle_ios] surface adapter failed: {e:?}");
            return -3;
        }
    };

    let dev = pollster::block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("mcle-ios-surface-device"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::downlevel_defaults(),
            memory_hints: wgpu::MemoryHints::Performance,
            trace: wgpu::Trace::Off,
            experimental_features: wgpu::ExperimentalFeatures::default(),
        },
    ));
    match dev {
        Ok(_) => 1,
        Err(e) => {
            eprintln!("[ruffle_ios] surface device failed: {e:?}");
            -4
        }
    }
}

/// Probe: create a wgpu Instance with the Metal backend, request an adapter,
/// then a device. Returns:
///   1  = all three succeeded (Metal GPU is reachable from our Rust side)
///   -1 = no adapter
///   -2 = no device
/// This does NOT create a Surface (no CAMetalLayer yet) and does NOT draw
/// anything; it just proves we can talk to the GPU from Rust on iOS.
#[no_mangle]
pub extern "C" fn ruffle_ios_wgpu_probe() -> c_int {
    let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends: wgpu::Backends::METAL,
        ..Default::default()
    });

    let adapter = pollster::block_on(instance.request_adapter(
        &wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::default(),
            force_fallback_adapter: false,
            compatible_surface: None,
        },
    ));
    let adapter = match adapter {
        Ok(a) => a,
        Err(e) => {
            eprintln!("[ruffle_ios] wgpu adapter failed: {e:?}");
            return -1;
        }
    };

    let dev = pollster::block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("mcle-ios-device"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::downlevel_defaults(),
            memory_hints: wgpu::MemoryHints::Performance,
            trace: wgpu::Trace::Off,
            experimental_features: wgpu::ExperimentalFeatures::default(),
        },
    ));
    match dev {
        Ok(_) => 1,
        Err(e) => {
            eprintln!("[ruffle_ios] wgpu device failed: {e:?}");
            -2
        }
    }
}

// --- Real Player API ---------------------------------------------------------

/// Create a Player with default (Null) backends and no movie loaded.
/// Returns an opaque handle the caller must later pass to
/// `ruffle_ios_player_destroy`. Returns NULL on failure.
#[no_mangle]
pub extern "C" fn ruffle_ios_player_create(vp_w: c_int, vp_h: c_int) -> *mut PlayerHandle {
    let width = vp_w.max(1) as u32;
    let height = vp_h.max(1) as u32;

    let player = PlayerBuilder::new()
        .with_viewport_dimensions(width, height, 1.0)
        .with_autoplay(true)
        .build();

    to_handle(player)
}

/// Create a Player with the given SWF bytes pre-loaded. Preferred path:
/// PlayerBuilder::with_movie takes care of wiring the movie into the player
/// during construction, so we don't need a separate load step.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_create_with_swf(
    vp_w: c_int,
    vp_h: c_int,
    data: *const u8,
    len: usize,
) -> *mut PlayerHandle {
    if data.is_null() || len < 8 {
        return std::ptr::null_mut();
    }
    let bytes = std::slice::from_raw_parts(data, len).to_vec();
    let movie = match SwfMovie::from_data(&bytes, String::from("file://mcle.swf"), None) {
        Ok(m) => m,
        Err(e) => {
            eprintln!("[ruffle_ios] SwfMovie::from_data failed: {e:?}");
            return std::ptr::null_mut();
        }
    };

    let width = vp_w.max(1) as u32;
    let height = vp_h.max(1) as u32;
    let player = PlayerBuilder::new()
        .with_movie(movie)
        .with_viewport_dimensions(width, height, 1.0)
        .with_autoplay(true)
        .build();

    to_handle(player)
}

/// Full wgpu-backed Player: creates a wgpu Surface over the given
/// CAMetalLayer, wires a WgpuRenderBackend into PlayerBuilder, and
/// pre-loads the SWF. Every Player tick after this will draw real Ruffle
/// output into the layer. Returns NULL on failure.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_create_wgpu(
    layer_ptr: *mut std::ffi::c_void,
    vp_w: c_int,
    vp_h: c_int,
    data: *const u8,
    len: usize,
) -> *mut PlayerHandle {
    use ruffle_render_wgpu::backend::{WgpuRenderBackend, request_adapter_and_device};
    use ruffle_render_wgpu::descriptors::Descriptors;
    use ruffle_render_wgpu::target::SwapChainTarget;

    if layer_ptr.is_null() || data.is_null() || len < 8 {
        return std::ptr::null_mut();
    }

    let width = vp_w.max(1) as u32;
    let height = vp_h.max(1) as u32;

    let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends: wgpu::Backends::METAL,
        ..Default::default()
    });

    let surface = match instance.create_surface_unsafe(
        wgpu::SurfaceTargetUnsafe::CoreAnimationLayer(layer_ptr)
    ) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("[ruffle_ios] create_surface_unsafe: {e:?}");
            return std::ptr::null_mut();
        }
    };

    let (adapter, device, queue) = match pollster::block_on(
        request_adapter_and_device(
            wgpu::Backends::METAL,
            &instance,
            Some(&surface),
            wgpu::PowerPreference::default(),
        ),
    ) {
        Ok(tup) => tup,
        Err(e) => {
            eprintln!("[ruffle_ios] request_adapter_and_device: {e:?}");
            return std::ptr::null_mut();
        }
    };

    let descriptors = std::sync::Arc::new(Descriptors::new(instance, adapter, device, queue));
    let target = SwapChainTarget::new(surface, &descriptors.adapter, (width, height), &descriptors.device);

    let backend = match WgpuRenderBackend::new(descriptors, target) {
        Ok(b) => b,
        Err(e) => {
            eprintln!("[ruffle_ios] WgpuRenderBackend::new: {e:?}");
            return std::ptr::null_mut();
        }
    };

    let bytes = std::slice::from_raw_parts(data, len).to_vec();
    let movie = match SwfMovie::from_data(&bytes, String::from("file://mcle.swf"), None) {
        Ok(m) => m,
        Err(e) => {
            eprintln!("[ruffle_ios] SwfMovie::from_data: {e:?}");
            return std::ptr::null_mut();
        }
    };

    let player = PlayerBuilder::new()
        .with_renderer(backend)
        .with_movie(movie)
        .with_viewport_dimensions(width, height, 1.0)
        .with_autoplay(true)
        .with_external_interface(Box::new(LoggingExternalInterface))
        .with_fs_commands(Box::new(LoggingFsCommands))
        .build();

    eprintln!("[ruffle_ios] wgpu player built, {}x{}", width, height);
    to_handle(player)
}

/// Copy the captured ExternalInterface call log into `out` as a
/// newline-separated UTF-8 string. Writes at most `cap - 1` bytes plus a
/// trailing NUL. Returns the number of bytes written (excluding NUL).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_extint_log(out: *mut u8, cap: usize) -> usize {
    if out.is_null() || cap == 0 { return 0; }
    let Ok(log) = EXTINT_LOG.lock() else { return 0; };
    let joined = log.join("\n");
    let bytes = joined.as_bytes();
    let n = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    *out.add(n) = 0;
    n
}

/// Drop a Player created by `ruffle_ios_player_create`.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_destroy(raw: *mut PlayerHandle) {
    if raw.is_null() {
        return;
    }
    drop(Box::from_raw(raw));
}

/// Advance the player by `dt_seconds` and run pending frame logic.
/// The render happens against the current RenderBackend (NullRenderer for now).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_tick(raw: *mut PlayerHandle, dt_seconds: f32) {
    let Some(handle) = borrow_handle(raw) else { return; };
    if let Ok(mut p) = handle.player.lock() {
        use ruffle_common::duration::FloatDuration;
        let dt = FloatDuration::from_secs(dt_seconds as f64);
        p.tick(dt);
        p.render();
    }
}

/// Diagnostic: return the player's current SWF frame rate (times 1000 so we
/// can fit it in an int without losing precision on typical values).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_framerate_mHz(raw: *mut PlayerHandle) -> c_int {
    let Some(handle) = borrow_handle(raw) else { return -1; };
    if let Ok(p) = handle.player.lock() {
        (p.frame_rate() * 1000.0) as c_int
    } else {
        -2
    }
}
