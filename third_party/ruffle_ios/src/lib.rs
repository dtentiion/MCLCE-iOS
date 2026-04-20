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
static TICK_COUNT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

// Stage counters for tick diagnostics. If `ticks` climbs but `cur_frame`
// stays at 0, comparing these tells us which stage of the tick path is
// dropping out silently.
static TICK_LOCK_OK:       std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
static TICK_AFTER_TICK:    std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
static TICK_AFTER_RUNFRAME:std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
static TICK_AFTER_RENDER:  std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
// 0 = unknown, 1 = true, 2 = false. Written every tick.
static IS_PLAYING_SAMPLE:  std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);

// Frame samples taken around the tick/run_frame pair. `cf_pre` is the
// current frame just after we grab the player lock; `cf_mid` is after
// `Player::tick`; `cf_post` is after `Player::run_frame`. If `run_frame`
// actually advances the root clip, cf_post > cf_pre for at least some
// ticks and FRAME_ADVANCES climbs.
static LATEST_CF_PRE:  std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-1);
static LATEST_CF_MID:  std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-1);
static LATEST_CF_POST: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-1);
static FRAME_ADVANCES: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

// Burn-frames diagnostic: at player create time we hammer run_frame()
// BURN_N times back to back and record cur_frame after each call. If the
// root clip never advances under a tight back-to-back loop, run_frame is
// either a real no-op for this movie or it's gated on something we haven't
// wired. Independent of tick-time accumulation and of tracing delivery.
const BURN_N: usize = 100;
static BURN_FIRST_CF: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-99);
static BURN_FINAL_CF: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-99);
static BURN_MAX_CF:   std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-99);
static BURN_UNIQUE:   std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);
static BURN_DONE:     std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

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

// AS3 trace() and AVM warnings go through a LogBackend. Capture them into
// a separate ring buffer so we can see what Ruffle is complaining about
// while the menu SWF runs.
static AVM_LOG: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());

fn avm_log_push(line: String) {
    if let Ok(mut log) = AVM_LOG.lock() {
        if log.len() >= 256 { log.remove(0); }
        log.push(line);
    }
}

/// Writer that shunts `tracing` events into AVM_LOG so users can see what
/// Ruffle is complaining about on-device without Mac-side log streaming.
struct RingBufferWriter;

impl std::io::Write for RingBufferWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        if let Ok(s) = std::str::from_utf8(buf) {
            for line in s.lines().filter(|l| !l.is_empty()) {
                avm_log_push(format!("trc {}", line));
            }
        }
        Ok(buf.len())
    }
    fn flush(&mut self) -> std::io::Result<()> { Ok(()) }
}

fn init_tracing_subscriber_once() {
    use tracing_subscriber::fmt;
    use tracing_subscriber::fmt::MakeWriter;
    use std::sync::OnceLock;
    static ONCE: OnceLock<()> = OnceLock::new();
    struct Maker;
    impl<'a> MakeWriter<'a> for Maker {
        type Writer = RingBufferWriter;
        fn make_writer(&'a self) -> Self::Writer { RingBufferWriter }
    }
    ONCE.get_or_init(|| {
        use tracing_subscriber::EnvFilter;
        // Default: ruffle modules at debug, everything else at warn so we
        // don't drown in tracing from wgpu/naga.
        let filter = EnvFilter::try_new(
            "warn,ruffle_core=trace,ruffle_render=debug,ruffle_render_wgpu=debug,ruffle_common=debug"
        ).unwrap_or_else(|_| EnvFilter::new("info"));
        let installed = fmt()
            .with_writer(Maker)
            .with_ansi(false)
            .with_target(true)
            .with_level(true)
            .with_env_filter(filter)
            .try_init()
            .is_ok();
        // Drop a direct breadcrumb into the AVM log so the overlay proves
        // the subscriber was actually installed (not silently stepped on
        // by some other global dispatcher).
        avm_log_push(format!(
            "[ruffle_ios] tracing subscriber installed={} (filter=ruffle_core=trace)",
            installed
        ));
    });
}

pub struct CapturingLogBackend;

impl ruffle_core::backend::log::LogBackend for CapturingLogBackend {
    fn avm_trace(&self, message: &str) {
        avm_log_push(format!("TRACE {message}"));
    }
    fn avm_warning(&self, message: &str) {
        avm_log_push(format!("WARN {message}"));
    }
}

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_avm_log(out: *mut u8, cap: usize) -> usize {
    if out.is_null() || cap == 0 { return 0; }
    let Ok(log) = AVM_LOG.lock() else { return 0; };
    let joined = log.join("\n");
    let bytes = joined.as_bytes();
    let n = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    *out.add(n) = 0;
    n
}

/// Diagnostic: returns the current frame number the player is on, or -1
/// if no player / no root movie. Tells us if the movie is advancing at all.
/// Pixel width of the currently-loaded root movie, or -1 on error.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_movie_width(raw: *mut PlayerHandle) -> c_int {
    let Some(handle) = borrow_handle(raw) else { return -1; };
    if let Ok(mut p) = handle.player.lock() {
        p.movie_width() as c_int
    } else { -2 }
}

/// Pixel height of the currently-loaded root movie, or -1 on error.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_movie_height(raw: *mut PlayerHandle) -> c_int {
    let Some(handle) = borrow_handle(raw) else { return -1; };
    if let Ok(mut p) = handle.player.lock() {
        p.movie_height() as c_int
    } else { -2 }
}

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_current_frame(raw: *mut PlayerHandle) -> c_int {
    let Some(handle) = borrow_handle(raw) else { return -1; };
    if let Ok(p) = handle.player.lock() {
        match p.current_frame() {
            Some(n) => n as c_int,
            None => -1,
        }
    } else { -2 }
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
    init_tracing_subscriber_once();
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
        .with_log(CapturingLogBackend)
        .build();

    // Force full preload synchronously, then explicitly set the run state
    // to Playing. `with_autoplay(true)` is documented as the trigger for
    // play, but in practice AVM2 movies sometimes start suspended until
    // preload completes. Running preload to completion here removes that
    // ambiguity.
    {
        use ruffle_core::limits::ExecutionLimit;
        let mut p = player.lock().expect("player lock");
        let mut limit = ExecutionLimit::none();
        let mut guard = 0;
        while !p.preload(&mut limit) && guard < 1024 {
            guard += 1;
        }
        p.set_is_playing(true);
        eprintln!(
            "[ruffle_ios] preload rounds={} movie={}x{} fps={}",
            guard, p.movie_width(), p.movie_height(), p.frame_rate()
        );

        // Burn-frames probe: call run_frame() BURN_N times and record what
        // the root clip's frame number looks like after each call. Writes
        // summary stats (first / final / max / distinct) into atomics.
        use std::sync::atomic::Ordering::Relaxed;
        let first = p.current_frame().map(|n| n as i32).unwrap_or(-1);
        BURN_FIRST_CF.store(first, Relaxed);
        let mut max = first;
        let mut prev = first;
        let mut unique = 1i32;
        for _ in 0..BURN_N {
            p.run_frame();
            let cf = p.current_frame().map(|n| n as i32).unwrap_or(-1);
            if cf > max { max = cf; }
            if cf != prev { unique += 1; prev = cf; }
        }
        let final_cf = p.current_frame().map(|n| n as i32).unwrap_or(-1);
        BURN_FINAL_CF.store(final_cf, Relaxed);
        BURN_MAX_CF.store(max, Relaxed);
        BURN_UNIQUE.store(unique, Relaxed);
        BURN_DONE.store(1, Relaxed);
        avm_log_push(format!(
            "[ruffle_ios] burn_frames N={} first={} final={} max={} unique_values={}",
            BURN_N, first, final_cf, max, unique
        ));
    }

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
    use std::sync::atomic::Ordering::Relaxed;
    let Some(handle) = borrow_handle(raw) else { return; };
    TICK_COUNT.fetch_add(1, Relaxed);
    if let Ok(mut p) = handle.player.lock() {
        TICK_LOCK_OK.fetch_add(1, Relaxed);
        IS_PLAYING_SAMPLE.store(if p.is_playing() { 1 } else { 2 }, Relaxed);
        use ruffle_common::duration::FloatDuration;
        let dt = FloatDuration::from_secs(dt_seconds as f64);
        let cf_pre = p.current_frame().map(|n| n as i32).unwrap_or(-1);
        LATEST_CF_PRE.store(cf_pre, Relaxed);
        p.tick(dt);
        TICK_AFTER_TICK.fetch_add(1, Relaxed);
        let cf_mid = p.current_frame().map(|n| n as i32).unwrap_or(-1);
        LATEST_CF_MID.store(cf_mid, Relaxed);
        // Kick the frame logic hard, in case tick's dt accumulator hasn't
        // crossed the frame-time threshold yet.
        p.run_frame();
        TICK_AFTER_RUNFRAME.fetch_add(1, Relaxed);
        let cf_post = p.current_frame().map(|n| n as i32).unwrap_or(-1);
        LATEST_CF_POST.store(cf_post, Relaxed);
        if cf_post != cf_pre && cf_pre >= 0 && cf_post >= 0 {
            FRAME_ADVANCES.fetch_add(1, Relaxed);
        }
        p.render();
        TICK_AFTER_RENDER.fetch_add(1, Relaxed);
    }
}

/// Burn-frames diag: stats from the back-to-back run_frame loop we
/// performed at player create time. Answers whether the root clip can
/// advance at all under a tight loop, independent of per-tick dt timing.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_burn_diag(
    done: *mut c_int,
    first: *mut c_int,
    final_cf: *mut c_int,
    max_cf: *mut c_int,
    unique_vals: *mut c_int,
) {
    use std::sync::atomic::Ordering::Relaxed;
    if !done.is_null()        { *done        = BURN_DONE.load(Relaxed) as c_int; }
    if !first.is_null()       { *first       = BURN_FIRST_CF.load(Relaxed) as c_int; }
    if !final_cf.is_null()    { *final_cf    = BURN_FINAL_CF.load(Relaxed) as c_int; }
    if !max_cf.is_null()      { *max_cf      = BURN_MAX_CF.load(Relaxed) as c_int; }
    if !unique_vals.is_null() { *unique_vals = BURN_UNIQUE.load(Relaxed) as c_int; }
}

/// Frame-transition diag: pre/mid/post cur_frame samples from the last
/// tick, and the count of ticks where cur_frame actually changed.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_frame_diag(
    cf_pre: *mut c_int,
    cf_mid: *mut c_int,
    cf_post: *mut c_int,
    frame_advances: *mut u64,
) {
    use std::sync::atomic::Ordering::Relaxed;
    if !cf_pre.is_null()  { *cf_pre  = LATEST_CF_PRE.load(Relaxed)  as c_int; }
    if !cf_mid.is_null()  { *cf_mid  = LATEST_CF_MID.load(Relaxed)  as c_int; }
    if !cf_post.is_null() { *cf_post = LATEST_CF_POST.load(Relaxed) as c_int; }
    if !frame_advances.is_null() { *frame_advances = FRAME_ADVANCES.load(Relaxed); }
}

/// Fill `out_counters` with the tick-stage breakdown:
///   out[0] = lock_ok, out[1] = after_tick, out[2] = after_run_frame,
///   out[3] = after_render.
/// `is_playing` receives 0 (unknown), 1 (true), or 2 (false).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_diag(out_counters: *mut u64, len: usize,
                                                is_playing: *mut c_int) {
    use std::sync::atomic::Ordering::Relaxed;
    if !out_counters.is_null() && len >= 4 {
        *out_counters.add(0) = TICK_LOCK_OK.load(Relaxed);
        *out_counters.add(1) = TICK_AFTER_TICK.load(Relaxed);
        *out_counters.add(2) = TICK_AFTER_RUNFRAME.load(Relaxed);
        *out_counters.add(3) = TICK_AFTER_RENDER.load(Relaxed);
    }
    if !is_playing.is_null() {
        *is_playing = IS_PLAYING_SAMPLE.load(Relaxed) as c_int;
    }
}

#[no_mangle]
pub extern "C" fn ruffle_ios_tick_count() -> u64 {
    TICK_COUNT.load(std::sync::atomic::Ordering::Relaxed)
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
