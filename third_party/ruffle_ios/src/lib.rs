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
use ruffle_core::backend::navigator::{NullExecutor, NullNavigatorBackend};
use ruffle_core::backend::ui::FontDefinition;
use ruffle_core::events::{GamepadButton, KeyCode, PlayerEvent};
use ruffle_core::font::FontFileData;
use ruffle_core::{Player, PlayerBuilder};

// --- Captured ExternalInterface log -----------------------------------------
//
// LCE's menu calls ExternalInterface.call(name, args) repeatedly during
// init. Stubbing each call by name is how we eventually get the menu to
// render its visible content. To find out what names it calls we log every
// invocation into this global ring buffer and expose it to the iOS app.

static EXTINT_LOG: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());
static TICK_COUNT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

// Device fonts the iOS app stages *before* player creation so they can be
// registered between PlayerBuilder::build() and the preload/class-construct
// pass inside ruffle_ios_player_create_wgpu. Registering after create returns
// is too late: text fields lookup font "Mojangles7" during AS3 construction
// and cache a miss.
struct StagedFont {
    name: String,
    is_bold: bool,
    is_italic: bool,
    data: Vec<u8>,
}
static STAGED_FONTS: std::sync::Mutex<Vec<StagedFont>> = std::sync::Mutex::new(Vec::new());

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

// Host-registered callback fired when LCE AS3 reports a setting
// change via ExternalInterface. handleCheckboxToggled(id, checked)
// and handleSliderMove(id, current) are the two we care about for
// now; console UIScene.cpp:1251/1271 dispatches them as virtual
// method overrides per scene. On iOS the host maps (current scene,
// id) to an mcle_setting index and writes through the store.
pub type SettingsEventCallback = extern "C" fn(
    method: *const c_char,
    id: f64,
    value: f64,
);
static SETTINGS_EVENT_CB: Mutex<Option<SettingsEventCallback>> = Mutex::new(None);

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_set_settings_event_callback(cb: SettingsEventCallback) {
    if let Ok(mut slot) = SETTINGS_EVENT_CB.lock() {
        *slot = Some(cb);
        avm_log_push("[ruffle_ios] settings_event_callback registered".to_string());
    }
}

fn dispatch_settings_event(name: &str, args: &[ExtValue]) {
    // Routed event shapes. Matches console's ExternalInterface.call
    // names from FJ_Document.as (handlePress line 170-191,
    // handleFocusChange / handleInitFocus line 270-298) plus the
    // per-scene setting events (handleCheckboxToggled /
    // handleSliderMove). Host receives them with a unified
    // (method, id, value) shape; semantics depend on method:
    //   handleCheckboxToggled(controlId, checked)
    //   handleSliderMove(controlId, currentValue)
    //   handlePress(controlId, childId) - button / list press
    //   handleInitFocus(controlId, childId) - scene-entry focus
    let (id_val, value_val) = match name {
        "handleCheckboxToggled" if args.len() >= 2 => {
            let id = match &args[0] { ExtValue::Number(n) => *n, _ => return };
            let checked = match &args[1] {
                ExtValue::Bool(b) => if *b { 1.0 } else { 0.0 },
                ExtValue::Number(n) => if *n != 0.0 { 1.0 } else { 0.0 },
                _ => return,
            };
            (id, checked)
        }
        "handleSliderMove" if args.len() >= 2 => {
            let id = match &args[0] { ExtValue::Number(n) => *n, _ => return };
            let value = match &args[1] { ExtValue::Number(n) => *n, _ => return };
            (id, value)
        }
        "handlePress" | "handleInitFocus" if args.len() >= 2 => {
            let id = match &args[0] { ExtValue::Number(n) => *n, _ => return };
            let child = match &args[1] { ExtValue::Number(n) => *n, _ => return };
            (id, child)
        }
        _ => return,
    };
    let cb_opt = SETTINGS_EVENT_CB.lock().ok().and_then(|s| *s);
    match cb_opt {
        Some(cb) => {
            if let Ok(c_name) = std::ffi::CString::new(name) {
                avm_log_push(format!(
                    "[ruffle_ios] dispatch_settings_event firing: {} id={} value={}",
                    name, id_val, value_val
                ));
                cb(c_name.as_ptr(), id_val, value_val);
            }
        }
        None => {
            avm_log_push(format!(
                "[ruffle_ios] dispatch_settings_event {} but callback is None",
                name
            ));
        }
    }
}

impl ExternalInterfaceProvider for LoggingExternalInterface {
    fn call_method(&self, _context: &mut UpdateContext<'_>, name: &str, args: &[ExtValue]) -> ExtValue {
        let rendered_args: Vec<String> = args.iter().map(|v| format!("{v:?}")).collect();
        let line = format!("{name}({})", rendered_args.join(", "));
        if let Ok(mut log) = EXTINT_LOG.lock() {
            // Cap memory to a reasonable upper bound; drop oldest.
            if log.len() >= 256 {
                log.remove(0);
            }
            log.push(line.clone());
        }
        // Mirror to AVM_LOG so the line lands in crash_log.txt too. The
        // overlay has its own ExtInt panel, but crash_log.txt only sees
        // what goes through avm_log_push, and we need those calls in the
        // persisted log when diagnosing why text fields never populate.
        avm_log_push(format!("[extint] call {line}"));
        // LCE setting events route to the iOS settings store if a
        // callback was registered.
        dispatch_settings_event(name, args);
        // For now, every call returns Null. Specific menus may need better
        // responses; we wire those once we know what the names are.
        ExtValue::Null
    }

    fn on_callback_available(&self, name: &str) {
        // SWF registered an AS3 callback the host can invoke. For LCE we
        // almost certainly never invoke these, but seeing their names is
        // how we learn what host-side methods the menu expects to exist.
        let line = format!("addCallback({name})");
        if let Ok(mut log) = EXTINT_LOG.lock() {
            if log.len() >= 256 { log.remove(0); }
            log.push(line.clone());
        }
        avm_log_push(format!("[extint] {line}"));
    }

    fn get_id(&self) -> Option<String> { None }
}

pub struct LoggingFsCommands;

impl FsCommandProvider for LoggingFsCommands {
    fn on_fs_command(&self, command: &str, args: &str) -> bool {
        let line = format!("fscommand::{command}({args})");
        if let Ok(mut log) = EXTINT_LOG.lock() {
            if log.len() >= 256 { log.remove(0); }
            log.push(line.clone());
        }
        avm_log_push(format!("[extint] {line}"));
        true
    }
}

// AS3 trace() and AVM warnings go through a LogBackend. Capture them into
// a separate ring buffer so we can see what Ruffle is complaining about
// while the menu SWF runs.
static AVM_LOG: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());

// Persistent log file in iOS Documents. Opened once at player init,
// appended on every push, flushed immediately so the trail survives a
// crash. User can read crash_log.txt via iOS Files app after the app
// dies. Keeps a mirror of AVM_LOG plus [ruffle_ios] markers so the
// whole startup cascade is visible post-mortem.
static LOG_FILE: std::sync::Mutex<Option<std::fs::File>> =
    std::sync::Mutex::new(None);

fn init_crash_log_file(base_path: &std::path::Path) {
    let path = base_path.join("crash_log.txt");
    if let Ok(mut guard) = LOG_FILE.lock() {
        *guard = std::fs::OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&path)
            .ok();
    }
}

fn avm_log_push(line: String) {
    if let Ok(mut log) = AVM_LOG.lock() {
        if log.len() >= 256 {
            log.remove(0);
        }
        log.push(line.clone());
    }
    if let Ok(mut guard) = LOG_FILE.lock() {
        if let Some(ref mut f) = *guard {
            use std::io::Write;
            let _ = writeln!(f, "{}", line);
            let _ = f.flush();
        }
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
        // Keep ruffle_core at info (not trace): trace floods the ring
        // buffer with every DefineShape/DefineSprite/tag parse during
        // preload and pushes the try_settle_imports/drain diagnostics
        // out of the head slice. info still captures the load cascade
        // and resolver outcomes we actually need on-device.
        let filter = EnvFilter::try_new(
            "warn,ruffle_core=info,ruffle_render=debug,ruffle_render_wgpu=debug,ruffle_common=debug"
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
            "[ruffle_ios] tracing subscriber installed={} (filter=ruffle_core=info)",
            installed
        ));
    });
}

pub struct CapturingLogBackend;

// Count of AS3 trace() calls seen so far. Surfaced via FFI so the overlay
// can show live evidence that AS3 is still running past the constructor.
// If this stays at 1 across many seconds of ticks, AS3 execution stalled
// after the initial Document-class constructor and no event handler
// (enterFrame, added_to_stage, activate) is waking it up.
static AVM_TRACE_COUNT: std::sync::atomic::AtomicU64 =
    std::sync::atomic::AtomicU64::new(0);
static AVM_WARN_COUNT: std::sync::atomic::AtomicU64 =
    std::sync::atomic::AtomicU64::new(0);

impl ruffle_core::backend::log::LogBackend for CapturingLogBackend {
    fn avm_trace(&self, message: &str) {
        AVM_TRACE_COUNT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        avm_log_push(format!("TRACE {message}"));
    }
    fn avm_warning(&self, message: &str) {
        AVM_WARN_COUNT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        avm_log_push(format!("WARN {message}"));
    }
}

/// Return (trace_count, warn_count) through out params.
/// trace_count is the number of AS3 `trace()` calls the player has made so
/// far; warn_count counts AVM warnings. If both stay low and flat across
/// many ticks, AS3 is not executing past whatever it ran at startup.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_avm_counts(
    traces: *mut u64,
    warns: *mut u64,
) {
    use std::sync::atomic::Ordering::Relaxed;
    if !traces.is_null() { *traces = AVM_TRACE_COUNT.load(Relaxed); }
    if !warns.is_null()  { *warns  = AVM_WARN_COUNT.load(Relaxed); }
}

/// Fill `out` (UTF-8, NUL-terminated, up to `cap` bytes) with a listing of
/// the root clip's direct children as `instance_name\tclass_name\n` lines.
/// Intended as a discovery pass for the LCE host bootstrap: the menu SWFs
/// expect the host to invoke `Init(label, id)` on each button by instance
/// name, and we don't know those names until we see them.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_enumerate_root_children(
    raw: *mut PlayerHandle,
    out: *mut u8,
    cap: usize,
) -> usize {
    if raw.is_null() || out.is_null() || cap == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let Ok(mut p) = handle.player.lock() else { return 0; };
    let children = p.enumerate_root_children();
    drop(p);
    let rendered: String = children
        .into_iter()
        .map(|(n, c)| format!("{}\t{}", if n.is_empty() { "<unnamed>" } else { &n }, c))
        .collect::<Vec<_>>()
        .join("\n");
    // Also drop it into the persistent log so we can grep later.
    avm_log_push(format!(
        "[ruffle_ios] root children ({} total):\n{}",
        rendered.lines().count(),
        rendered
    ));
    let bytes = rendered.as_bytes();
    let n = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    *out.add(n) = 0;
    n
}

/// Toggle the `visible` flag on a named direct child of the root clip.
/// Used to hide 4J's `iggy_Splash` loading placeholder, which the
/// console engine hides the moment a scene is ready but our port
/// leaves in the display list as a permanent gray strip.
/// Returns 1 if the child was found and updated, 0 on bad args or
/// not found, -1 on player lock failure.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_set_root_child_visible(
    raw: *mut PlayerHandle,
    name_ptr: *const u8,
    name_len: usize,
    visible: c_int,
) -> c_int {
    if raw.is_null() || name_ptr.is_null() || name_len == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let name = match std::str::from_utf8(std::slice::from_raw_parts(name_ptr, name_len)) {
        Ok(s) => s.to_string(),
        Err(_) => return 0,
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let found = p.set_root_child_visible(&name, visible != 0);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] set_root_child_visible('{}', {}) -> {}",
        name, visible != 0, found
    ));
    if found { 1 } else { 0 }
}

/// Instantiate an AS3 class by name (looked up in the root movie's
/// domain) and attach its resulting DisplayObject to the root clip at
/// the specified depth. Used by the iOS host to add the menu panorama
/// under the MainMenu buttons without modifying the MainMenu SWF.
///
/// Depth tip: MainMenu's own buttons live at depths 3..8; pass depth=0
/// or a negative number to place the panorama beneath them.
///
/// Returns 1 on success, 0 on bad args, -1 on player lock failure,
/// -2 on AS3/domain resolution failure (see AVM_LOG for details).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_instantiate_class_on_root(
    raw: *mut PlayerHandle,
    class_name_ptr: *const u8,
    class_name_len: usize,
    depth: c_int,
) -> c_int {
    if raw.is_null() || class_name_ptr.is_null() || class_name_len == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let class_name = match std::str::from_utf8(
        std::slice::from_raw_parts(class_name_ptr, class_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.instantiate_class_on_root(class_name, depth);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] instantiate_class_on_root('{}', depth={}) -> {}",
        class_name, depth, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

/// Register an externally-sourced PNG as a Ruffle Bitmap character
/// keyed by an AS3 class name. When a subsequently-loaded SWF's
/// PlaceObject3 references that class name and AVM2 class resolution
/// fails (e.g. Panorama_Background_S, which lives in a PNG on console
/// via skin_Minecraft.xui), the resolver's XUI fallback hits this
/// registry and returns the Bitmap character.
///
/// Returns 1 on success, 0 on bad args, -1 on player lock failure,
/// -2 on PNG decode or registration failure.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_register_xui_bitmap(
    raw: *mut PlayerHandle,
    class_name_ptr: *const u8,
    class_name_len: usize,
    png_ptr: *const u8,
    png_len: usize,
    display_scale_x: f32,
    display_scale_y: f32,
) -> c_int {
    if raw.is_null() || class_name_ptr.is_null() || class_name_len == 0
        || png_ptr.is_null() || png_len == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let class_name = match std::str::from_utf8(
        std::slice::from_raw_parts(class_name_ptr, class_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let png = std::slice::from_raw_parts(png_ptr, png_len).to_vec();
    let Ok(mut p) = handle.player.lock() else { return -1; };
    match p.register_xui_bitmap(class_name, png, display_scale_x, display_scale_y) {
        Ok(chid) => {
            drop(p);
            avm_log_push(format!(
                "[ruffle_ios] register_xui_bitmap('{}') -> chid={} scale={}x{}",
                class_name, chid, display_scale_x, display_scale_y
            ));
            1
        }
        Err(e) => {
            drop(p);
            avm_log_push(format!(
                "[ruffle_ios] register_xui_bitmap('{}') err: {}", class_name, e
            ));
            -2
        }
    }
}

/// Load a sibling SWF (Panorama1080.swf, ComponentLogo1080.swf, etc.)
/// from bytes and attach its root timeline as a child of the current
/// root clip at the given depth. Mirrors how console LCE layers
/// multiple Iggy-loaded movies on the same stage.
///
/// Typical depths: panorama = 0 (below menu), logo = 100 (above).
/// Returns 1 ok, 0 bad args, -1 lock fail, -2 parse / attach failure.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_add_sibling_swf_to_root(
    raw: *mut PlayerHandle,
    data: *const u8,
    data_len: usize,
    url_ptr: *const u8,
    url_len: usize,
    depth: c_int,
    scale_x: f32,
    scale_y: f32,
    tx: f32,
    ty: f32,
) -> c_int {
    if raw.is_null() || data.is_null() || data_len == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let bytes = std::slice::from_raw_parts(data, data_len).to_vec();
    let url = if url_ptr.is_null() || url_len == 0 {
        String::from("file://sibling.swf")
    } else {
        match std::str::from_utf8(std::slice::from_raw_parts(url_ptr, url_len)) {
            Ok(s) => s.to_string(),
            Err(_) => return 0,
        }
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.add_sibling_swf_to_root(bytes, url, depth, scale_x, scale_y, tx, ty);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] add_sibling_swf_to_root(depth={}, scale={}x{}, t={},{}) -> {}",
        depth, scale_x, scale_y, tx, ty, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

/// Replace the root movie on an existing player. Used by the iOS host
/// for menu scene transitions (MainMenu -> HelpAndOptions etc.) without
/// tearing down the wgpu surface. Returns 1 on success, 0 on bad args,
/// -1 on player lock failure, -2 on SWF parse failure.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_replace_swf(
    raw: *mut PlayerHandle,
    data: *const u8,
    data_len: usize,
    url_ptr: *const u8,
    url_len: usize,
) -> c_int {
    if raw.is_null() || data.is_null() || data_len == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let bytes = std::slice::from_raw_parts(data, data_len).to_vec();
    let url = if url_ptr.is_null() || url_len == 0 {
        String::from("file://mcle.swf")
    } else {
        match std::str::from_utf8(std::slice::from_raw_parts(url_ptr, url_len)) {
            Ok(s) => s.to_string(),
            Err(_) => return 0,
        }
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    match p.replace_root_movie_from_bytes(bytes, url) {
        Ok(()) => {
            drop(p);
            avm_log_push(String::from("[ruffle_ios] replace_root_movie ok"));
            1
        }
        Err(e) => {
            drop(p);
            avm_log_push(format!("[ruffle_ios] replace_root_movie err: {}", e));
            -2
        }
    }
}

/// One level deeper than `ruffle_ios_enumerate_root_children`: list the
/// direct children of a named root child, e.g. what `Button1` actually
/// contains. Hypothesis we're chasing: PlaceObject places Button1 by class
/// name only (no char id), Ruffle builds the AS3 object but doesn't clone
/// the bound sprite's timeline, so `FJ_TextContainer` (needed by
/// `FJ_Base.GetTextField`) never exists. This dumps Button1's children so
/// we can confirm in the log.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_enumerate_named_child_children(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
    out: *mut u8,
    cap: usize,
) -> usize {
    if raw.is_null() || child_name_ptr.is_null() || child_name_len == 0
        || out.is_null() || cap == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let Ok(mut p) = handle.player.lock() else { return 0; };
    let kids = p.enumerate_named_child_children(child_name);
    drop(p);
    let rendered: String = kids
        .into_iter()
        .map(|(n, c)| format!("{}\t{}", if n.is_empty() { "<unnamed>" } else { &n }, c))
        .collect::<Vec<_>>()
        .join("\n");
    avm_log_push(format!(
        "[ruffle_ios] '{}' children ({} total):\n{}",
        child_name,
        rendered.lines().count().max(if rendered.is_empty() { 0 } else { 1 }),
        rendered
    ));
    let bytes = rendered.as_bytes();
    let n = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    *out.add(n) = 0;
    n
}

/// Recursively enumerate a named root child's subtree up to `max_depth`
/// levels. Output format (pushed to AVM_LOG and copied to `out`): each
/// line is `<depth spaces><name>\t<class>` so indentation reveals the
/// tree structure. FJ_Base.GetTextField walks 3 levels from the button,
/// so max_depth=3 is the useful setting for the label diagnostic.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_enumerate_subtree_of(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
    max_depth: usize,
    out: *mut u8,
    cap: usize,
) -> usize {
    if raw.is_null() || child_name_ptr.is_null() || child_name_len == 0
        || out.is_null() || cap == 0 {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let Ok(mut p) = handle.player.lock() else { return 0; };
    let nodes = p.enumerate_subtree_of(child_name, max_depth);
    drop(p);
    let rendered: String = nodes
        .into_iter()
        .map(|(d, n, c)| {
            let indent = "  ".repeat(d.saturating_sub(1));
            format!("{}{}\t{}", indent, if n.is_empty() { "<unnamed>" } else { &n }, c)
        })
        .collect::<Vec<_>>()
        .join("\n");
    avm_log_push(format!(
        "[ruffle_ios] subtree of '{}' (depth<={}, {} nodes):\n{}",
        child_name,
        max_depth,
        rendered.lines().count().max(if rendered.is_empty() { 0 } else { 1 }),
        rendered
    ));
    let bytes = rendered.as_bytes();
    let n = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    *out.add(n) = 0;
    n
}

/// Invoke AS3 `childName.methodName(label, id)` on a direct child of the
/// root clip. Returns 1 on success, 0 on bad args, -1 if the player lock
/// can't be taken. A human-readable status line is pushed to AVM_LOG
/// (visible in the overlay and crash_log.txt) regardless of outcome, so
/// the host can see exactly what happened without needing a second FFI
/// call for the result.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_call_init_on_named_child(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
    method_name_ptr: *const u8,
    method_name_len: usize,
    label_ptr: *const u8,
    label_len: usize,
    id: f64,
) -> c_int {
    if raw.is_null()
        || child_name_ptr.is_null() || child_name_len == 0
        || method_name_ptr.is_null() || method_name_len == 0
    {
        return 0;
    }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let method_name = match std::str::from_utf8(
        std::slice::from_raw_parts(method_name_ptr, method_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let label = if label_ptr.is_null() || label_len == 0 {
        ""
    } else {
        match std::str::from_utf8(std::slice::from_raw_parts(label_ptr, label_len)) {
            Ok(s) => s,
            Err(_) => return 0,
        }
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.call_init_on_named_child(child_name, method_name, label, id);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] call_init_on_named_child('{}', '{}', '{}', {}) -> {}",
        child_name, method_name, label, id, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

/// Set AS3 stage focus to a named root child. Needed on scenes
/// like SettingsOptionsMenu where the SWF expects keyboard input
/// routing, but no control is auto-focused on scene enter. Without
/// it, arrow-key events (which our gamepad mapping translates from
/// DPad) go to no DisplayObject and sliders/checkboxes look
/// frozen. Mirrors UIScene::sendInputToMovie's null-focus recovery
/// block on console (Common/UI/UIScene.cpp:1066-1081).
/// Toggle Ruffle's built-in Flash Player yellow focus rectangle.
/// LCE supplies its own focus outline art (FJ_Slider_Outline etc),
/// so the host turns Ruffle's auto rectangle off at startup.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_suppress_auto_focus_highlight(
    raw: *mut PlayerHandle,
    suppress: c_int,
) {
    let Some(handle) = borrow_handle(raw) else { return; };
    let Ok(mut p) = handle.player.lock() else { return; };
    p.set_suppress_auto_highlight(suppress != 0);
}

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_clear_focus(raw: *mut PlayerHandle) {
    let Some(handle) = borrow_handle(raw) else { return; };
    let Ok(mut p) = handle.player.lock() else { return; };
    p.clear_focus();
}

/// Call SetFocus(id) on the root SWF's document class. Wrapper
/// around Player::call_root_method_number("SetFocus", id). Matches
/// console's UIScene::gainFocus path (UIScene.cpp:1003-1012) and
/// UIScene::SetFocusToElement (UIScene.cpp:246-258): for initial
/// scene entry pass id=-1, which FJ_Document.SetFocus treats as
/// "auto-focus the child with tabIndex == 1" and also fires
/// handleInitFocus through ExternalInterface.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_call_root_set_focus(
    raw: *mut PlayerHandle,
    id: f64,
) -> c_int {
    if raw.is_null() { return 0; }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.call_root_method_number("SetFocus", id);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] call_root_set_focus({}) -> {}", id, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_focus_named_child(
    raw: *mut PlayerHandle,
    name_ptr: *const u8,
    name_len: usize,
) -> c_int {
    if raw.is_null() || name_ptr.is_null() || name_len == 0 { return 0; }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let name = match std::str::from_utf8(std::slice::from_raw_parts(name_ptr, name_len)) {
        Ok(s) => s.to_string(),
        Err(_) => return 0,
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let ok = p.set_focus_to_named_child(&name);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] focus_named_child('{}') -> {}", name, ok
    ));
    if ok { 1 } else { -2 }
}

/// Init(label, id, checked) on an FJ_CheckBox child, mirroring
/// UIControl_CheckBox::init on console. Returns 1 ok, 0 bad args,
/// -1 lock fail, -2 AVM2 call err.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_call_init_checkbox(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
    label_ptr: *const u8,
    label_len: usize,
    id: f64,
    checked: c_int,
) -> c_int {
    if raw.is_null() || child_name_ptr.is_null() || child_name_len == 0 { return 0; }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let label = if label_ptr.is_null() || label_len == 0 { "" } else {
        match std::str::from_utf8(std::slice::from_raw_parts(label_ptr, label_len)) {
            Ok(s) => s, Err(_) => return 0,
        }
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.call_init_checkbox(child_name, label, id, checked != 0);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] call_init_checkbox('{}', '{}', {}, {}) -> {}",
        child_name, label, id, checked != 0, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

/// Init(label, id, min, max, current) on an FJ_Slider child,
/// mirroring UIControl_Slider::init on console. Returns 1 ok, 0
/// bad args, -1 lock fail, -2 AVM2 call err.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_call_init_slider(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
    label_ptr: *const u8,
    label_len: usize,
    id: f64,
    min: i32,
    max: i32,
    current: i32,
) -> c_int {
    if raw.is_null() || child_name_ptr.is_null() || child_name_len == 0 { return 0; }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let label = if label_ptr.is_null() || label_len == 0 { "" } else {
        match std::str::from_utf8(std::slice::from_raw_parts(label_ptr, label_len)) {
            Ok(s) => s, Err(_) => return 0,
        }
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.call_init_slider(child_name, label, id, min, max, current);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] call_init_slider('{}', '{}', id={}, [{}..{}]={}) -> {}",
        child_name, label, id, min, max, current, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

/// Init(id) on an FJ_ButtonList child. Wrapper around
/// Player::call_list_init. Mirrors UIControl_ButtonList::init
/// (UIControl_ButtonList.cpp:28-50) on console.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_call_list_init(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
    id: f64,
) -> c_int {
    if raw.is_null() || child_name_ptr.is_null() || child_name_len == 0 { return 0; }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.call_list_init(child_name, id);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] call_list_init('{}', id={}) -> {}",
        child_name, id, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

/// addNewItem(label, data, iconName) on an
/// FJ_ButtonList_ListIconLeft child. Wrapper around
/// Player::call_list_add_item. Mirrors UIControl_SaveList::addItem
/// (UIControl_SaveList.cpp:48-89). Pass empty icon_name for no icon.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_call_list_add_item(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
    label_ptr: *const u8,
    label_len: usize,
    data: f64,
    icon_name_ptr: *const u8,
    icon_name_len: usize,
) -> c_int {
    if raw.is_null() || child_name_ptr.is_null() || child_name_len == 0 { return 0; }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let label = if label_ptr.is_null() || label_len == 0 { "" } else {
        match std::str::from_utf8(std::slice::from_raw_parts(label_ptr, label_len)) {
            Ok(s) => s, Err(_) => return 0,
        }
    };
    let icon_name = if icon_name_ptr.is_null() || icon_name_len == 0 { "" } else {
        match std::str::from_utf8(std::slice::from_raw_parts(icon_name_ptr, icon_name_len)) {
            Ok(s) => s, Err(_) => return 0,
        }
    };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.call_list_add_item(child_name, label, data, icon_name);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] call_list_add_item('{}', '{}', data={}, icon='{}') -> {}",
        child_name, label, data, icon_name, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

/// removeAllItems() on an FJ_ButtonList child. Wrapper around
/// Player::call_list_remove_all. Mirrors
/// UIControl_ButtonList::clearList (UIControl_ButtonList.cpp:60-66).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_call_list_remove_all(
    raw: *mut PlayerHandle,
    child_name_ptr: *const u8,
    child_name_len: usize,
) -> c_int {
    if raw.is_null() || child_name_ptr.is_null() || child_name_len == 0 { return 0; }
    let Some(handle) = borrow_handle(raw) else { return 0; };
    let child_name = match std::str::from_utf8(
        std::slice::from_raw_parts(child_name_ptr, child_name_len)
    ) { Ok(s) => s, Err(_) => return 0 };
    let Ok(mut p) = handle.player.lock() else { return -1; };
    let status = p.call_list_remove_all(child_name);
    drop(p);
    avm_log_push(format!(
        "[ruffle_ios] call_list_remove_all('{}') -> {}", child_name, status
    ));
    if status.starts_with("ok:") { 1 } else { -2 }
}

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_avm_log(out: *mut u8, cap: usize) -> usize {
    if out.is_null() || cap == 0 { return 0; }
    let Ok(log) = AVM_LOG.lock() else { return 0; };
    let joined = log.join("\n");
    let bytes = joined.as_bytes();
    // Prefer the TAIL of the log so live errors don't fall off the bottom
    // of the overlay when earlier noise fills the buffer.
    let budget = cap - 1;
    let (src, n) = if bytes.len() <= budget {
        (bytes, bytes.len())
    } else {
        let start = bytes.len() - budget;
        (&bytes[start..], budget)
    };
    std::ptr::copy_nonoverlapping(src.as_ptr(), out, n);
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
    // NullExecutor wraps a futures::executor::LocalPool, which is !Send.
    // iOS only ticks us from the main thread, so we hand out Send+Sync
    // via unsafe impls below. RefCell gives interior mutability without
    // needing a lock.
    executor: std::cell::RefCell<NullExecutor>,
    // Monotonic count of executor.run() calls, for diagnostics.
    executor_runs: std::sync::atomic::AtomicU64,
}

// Safety: the iOS app always invokes ruffle_ios_player_* functions from the
// main UIKit thread (CADisplayLink callback). The Mutex on `player` handles
// internal exclusion; the `executor` RefCell is single-threaded by
// construction.
unsafe impl Send for PlayerHandle {}
unsafe impl Sync for PlayerHandle {}

fn to_handle(p: Arc<Mutex<Player>>, executor: NullExecutor) -> *mut PlayerHandle {
    Box::into_raw(Box::new(PlayerHandle {
        player: p,
        executor: std::cell::RefCell::new(executor),
        executor_runs: std::sync::atomic::AtomicU64::new(0),
    }))
}

unsafe fn borrow_handle<'a>(raw: *mut PlayerHandle) -> Option<&'a PlayerHandle> {
    if raw.is_null() { None } else { Some(&*raw) }
}

/// Stage a TTF/OTF font for registration during the *next* call to
/// `ruffle_ios_player_create_wgpu`. Unlike `ruffle_ios_register_device_font`,
/// this can be called before a player exists, and the font lands in time
/// for the preload pass so text fields find their device font on first
/// layout instead of caching a miss.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_stage_device_font(
    name_ptr: *const u8,
    name_len: usize,
    data_ptr: *const u8,
    data_len: usize,
    is_bold: c_int,
    is_italic: c_int,
) -> c_int {
    if name_ptr.is_null() || data_ptr.is_null() || name_len == 0 || data_len == 0 {
        return 0;
    }
    let name_bytes = std::slice::from_raw_parts(name_ptr, name_len);
    let name = match std::str::from_utf8(name_bytes) {
        Ok(s) => s.to_string(),
        Err(_) => return 0,
    };
    let data = std::slice::from_raw_parts(data_ptr, data_len).to_vec();
    let mut fonts = match STAGED_FONTS.lock() {
        Ok(f) => f,
        Err(_) => return -1,
    };
    fonts.push(StagedFont {
        name: name.clone(),
        is_bold: is_bold != 0,
        is_italic: is_italic != 0,
        data,
    });
    avm_log_push(format!(
        "[ruffle_ios] staged device font '{}' ({} bytes, bold={}, italic={}), pending={}",
        name, data_len, is_bold != 0, is_italic != 0, fonts.len()
    ));
    1
}

/// Register a TTF/OTF font as a Ruffle device font. The Flash SWF can then
/// pick it up when it asks for a device font by `name`. Returns 1 on success,
/// 0 on any argument failure, -1 if the player lock can't be acquired.
///
/// Prefer `ruffle_ios_stage_device_font` for normal use: registering through
/// this function after player creation is too late for text fields that
/// already did their first glyph lookup during preload.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_register_device_font(
    raw: *mut PlayerHandle,
    name_ptr: *const u8,
    name_len: usize,
    data_ptr: *const u8,
    data_len: usize,
    is_bold: c_int,
    is_italic: c_int,
) -> c_int {
    let Some(handle) = borrow_handle(raw) else { return 0; };
    if name_ptr.is_null() || data_ptr.is_null() || name_len == 0 || data_len == 0 {
        return 0;
    }
    let name_bytes = std::slice::from_raw_parts(name_ptr, name_len);
    let name = match std::str::from_utf8(name_bytes) {
        Ok(s) => s.to_string(),
        Err(_) => return 0,
    };
    let data = std::slice::from_raw_parts(data_ptr, data_len).to_vec();
    let ok = if let Ok(mut player) = handle.player.lock() {
        player.register_device_font(FontDefinition::FontFile {
            name: name.clone(),
            is_bold: is_bold != 0,
            is_italic: is_italic != 0,
            data: FontFileData::new(data),
            index: 0,
        });
        true
    } else {
        false
    };
    if ok {
        avm_log_push(format!(
            "[ruffle_ios] registered device font '{}' ({} bytes, bold={}, italic={})",
            name, data_len, is_bold != 0, is_italic != 0
        ));
        1
    } else {
        -1
    }
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

    let executor = NullExecutor::new();
    let player = PlayerBuilder::new()
        .with_viewport_dimensions(width, height, 1.0)
        .with_autoplay(true)
        .build();

    to_handle(player, executor)
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
    let executor = NullExecutor::new();
    let player = PlayerBuilder::new()
        .with_movie(movie)
        .with_viewport_dimensions(width, height, 1.0)
        .with_autoplay(true)
        .build();

    to_handle(player, executor)
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
    base_path_c: *const c_char,
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

    // NullNavigatorBackend's default constructor creates its own internal
    // LocalPool executor we can never drive, so any future Ruffle spawns
    // during preload / AS3 setup never gets polled. Use `with_base_path`
    // instead, which lets us share an executor we can run each tick.
    //
    // `base_path_c` is where the SWF's relative fetches resolve (Loader
    // for sister assets, etc.). iOS passes the Documents directory so
    // MainMenu1080.swf's sibling SWFs (skinHD.swf, etc.) resolve there.
    let executor = NullExecutor::new();
    let base_path: std::path::PathBuf = if base_path_c.is_null() {
        std::env::temp_dir()
    } else {
        match std::ffi::CStr::from_ptr(base_path_c).to_str() {
            Ok(s) => std::path::PathBuf::from(s),
            Err(_) => std::env::temp_dir(),
        }
    };
    // Persistent crash log in Documents so trace output survives
    // abort() / SIGABRT and can be retrieved via iOS Files app.
    init_crash_log_file(&base_path);
    avm_log_push(format!(
        "[ruffle_ios] crash_log_file opened at {}",
        base_path.join("crash_log.txt").display()
    ));
    let navigator = match NullNavigatorBackend::with_base_path(&base_path, &executor) {
        Ok(nav) => nav,
        Err(e) => {
            eprintln!(
                "[ruffle_ios] with_base_path({}) failed: {e:?}",
                base_path.display()
            );
            return std::ptr::null_mut();
        }
    };

    // Translate controller buttons into keyboard events. Without this
    // mapping Ruffle's InputManager silently drops GamepadButtonDown /
    // GamepadButtonUp events; AS3 menus listen on KeyboardEvent, not
    // gamepad events, so the menu would look dead to controller input
    // even with our FFI plumbing working.
    let mut gamepad_map: std::collections::HashMap<GamepadButton, KeyCode> =
        std::collections::HashMap::new();
    gamepad_map.insert(GamepadButton::South, KeyCode::ENTER);
    gamepad_map.insert(GamepadButton::East, KeyCode::ESCAPE);
    gamepad_map.insert(GamepadButton::North, KeyCode::SPACE);
    gamepad_map.insert(GamepadButton::West, KeyCode::SPACE);
    gamepad_map.insert(GamepadButton::Start, KeyCode::ENTER);
    gamepad_map.insert(GamepadButton::Select, KeyCode::ESCAPE);
    gamepad_map.insert(GamepadButton::DPadUp, KeyCode::UP);
    gamepad_map.insert(GamepadButton::DPadDown, KeyCode::DOWN);
    gamepad_map.insert(GamepadButton::DPadLeft, KeyCode::LEFT);
    gamepad_map.insert(GamepadButton::DPadRight, KeyCode::RIGHT);

    let player = PlayerBuilder::new()
        .with_renderer(backend)
        .with_navigator(navigator)
        .with_movie(movie)
        .with_viewport_dimensions(width, height, 1.0)
        .with_autoplay(true)
        .with_external_interface(Box::new(LoggingExternalInterface))
        .with_fs_commands(Box::new(LoggingFsCommands))
        .with_log(CapturingLogBackend)
        .with_gamepad_button_mapping(gamepad_map)
        .build();

    // Drive preload and executor alternately. `preload(limit)` advances all
    // the synchronous bytes-into-frames work; `executor.run()` polls any
    // futures Ruffle's internals spawn via the navigator (root Loader
    // completion among them). Both must run or AS3 document-class setup
    // stalls and run_frame becomes a no-op.
    //
    // CRITICAL: the futures Ruffle spawns call `player.lock().unwrap()` on
    // their own, so we MUST release our player lock before `executor.run()`
    // or we deadlock against ourselves.
    let mut executor = executor;
    {
        // Apply any fonts staged by the iOS shell BEFORE preload. Registering
        // after preload is too late: text fields lookup device fonts during
        // their first glyph layout and cache a miss if the font isn't ready.
        let staged: Vec<StagedFont> = {
            let mut guard = match STAGED_FONTS.lock() {
                Ok(g) => g,
                Err(_) => {
                    avm_log_push("[ruffle_ios] STAGED_FONTS lock poisoned, skipping".into());
                    return std::ptr::null_mut();
                }
            };
            std::mem::take(&mut *guard)
        };
        if !staged.is_empty() {
            let mut p = player.lock().expect("player lock");
            for font in staged {
                let len = font.data.len();
                p.register_device_font(FontDefinition::FontFile {
                    name: font.name.clone(),
                    is_bold: font.is_bold,
                    is_italic: font.is_italic,
                    data: FontFileData::new(font.data),
                    index: 0,
                });
                avm_log_push(format!(
                    "[ruffle_ios] applied staged font '{}' ({} bytes) before preload",
                    font.name, len
                ));
            }
        }

        use ruffle_core::limits::ExecutionLimit;
        let mut limit = ExecutionLimit::none();
        let mut guard = 0;
        loop {
            let done = {
                let mut p = player.lock().expect("player lock");
                p.preload(&mut limit)
            };
            executor.run();
            if done || guard >= 1024 {
                break;
            }
            guard += 1;
        }
        {
            let mut p = player.lock().expect("player lock");
            p.set_is_playing(true);
            // Fire a FocusGained so AS3 code that gates on
            // stage.focus can proceed. Harmless if nothing listens.
            p.handle_event(PlayerEvent::FocusGained);
            eprintln!(
                "[ruffle_ios] preload rounds={} movie={}x{} fps={}",
                guard, p.movie_width(), p.movie_height(), p.frame_rate()
            );
        }
        executor.run();
        avm_log_push("[ruffle_ios] FocusGained sent, executor drained".into());

        // Burn-frames probe: call run_frame() BURN_N times and record what
        // the root clip's frame number looks like after each call, with the
        // executor drained between each attempt. With the navigator wired
        // we expect this to actually advance for AS3 movies now.
        use std::sync::atomic::Ordering::Relaxed;
        let first = {
            let p = player.lock().expect("player lock");
            p.current_frame().map(|n| n as i32).unwrap_or(-1)
        };
        BURN_FIRST_CF.store(first, Relaxed);
        let mut max = first;
        let mut prev = first;
        let mut unique = 1i32;
        for _ in 0..BURN_N {
            let cf = {
                let mut p = player.lock().expect("player lock");
                p.run_frame();
                p.current_frame().map(|n| n as i32).unwrap_or(-1)
            };
            executor.run();
            if cf > max { max = cf; }
            if cf != prev { unique += 1; prev = cf; }
        }
        let final_cf = {
            let p = player.lock().expect("player lock");
            p.current_frame().map(|n| n as i32).unwrap_or(-1)
        };
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
    to_handle(player, executor)
}

// --- Input events from the iOS controller layer ------------------------------

fn map_gamepad_button(code: c_int) -> Option<GamepadButton> {
    Some(match code {
        0 => GamepadButton::South,          // Xbox A
        1 => GamepadButton::East,           // Xbox B
        2 => GamepadButton::North,          // Xbox Y
        3 => GamepadButton::West,           // Xbox X
        4 => GamepadButton::Start,
        5 => GamepadButton::Select,
        6 => GamepadButton::DPadUp,
        7 => GamepadButton::DPadDown,
        8 => GamepadButton::DPadLeft,
        9 => GamepadButton::DPadRight,
        10 => GamepadButton::LeftTrigger,
        11 => GamepadButton::RightTrigger,
        12 => GamepadButton::LeftTrigger2,
        13 => GamepadButton::RightTrigger2,
        _ => return None,
    })
}

/// Forward a controller button press into the Ruffle player as a
/// GamepadButtonDown event. `code` is the iOS-side mapping (see header).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_gamepad_down(raw: *mut PlayerHandle, code: c_int) {
    let Some(handle) = borrow_handle(raw) else { return; };
    let Some(button) = map_gamepad_button(code) else { return; };
    if let Ok(mut p) = handle.player.lock() {
        p.handle_event(PlayerEvent::GamepadButtonDown { button });
    }
    handle.executor.borrow_mut().run();
    avm_log_push(format!("[ruffle_ios] GamepadButtonDown code={code}"));
}

/// Mirror of gamepad_down for button release.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_gamepad_up(raw: *mut PlayerHandle, code: c_int) {
    let Some(handle) = borrow_handle(raw) else { return; };
    let Some(button) = map_gamepad_button(code) else { return; };
    if let Ok(mut p) = handle.player.lock() {
        p.handle_event(PlayerEvent::GamepadButtonUp { button });
    }
    handle.executor.borrow_mut().run();
    avm_log_push(format!("[ruffle_ios] GamepadButtonUp code={code}"));
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

    // Step 1: drain async futures (Loader completions etc.) WITHOUT holding
    // the player lock. The futures lock the player themselves; holding our
    // own lock here would deadlock.
    handle.executor.borrow_mut().run();
    let nth_executor = handle.executor_runs.fetch_add(1, Relaxed) + 1;

    // Every 60 ticks drop a heartbeat marker into AVM_LOG so the overlay
    // proves our avm_log_push path still delivers even when Ruffle itself
    // emits no further tracing events.
    if nth_executor % 60 == 0 {
        avm_log_push(format!(
            "[ruffle_ios] heartbeat tick={} exec={}",
            TICK_COUNT.load(Relaxed), nth_executor
        ));
    }

    // Step 2: take the lock, sample state, tick, render, release.
    // Player::tick is the full per-frame driver (it runs run_frame when
    // the accumulator crosses a frame boundary, and also drives
    // update_timers / stream manager / audio). Calling run_frame here
    // would be a second advance on top and desyncs AS3's timer and stream
    // state. Ruffle's test runner calls one or the other, never both.
    let (cf_pre, cf_post) = {
        let Ok(mut p) = handle.player.lock() else { return; };
        TICK_LOCK_OK.fetch_add(1, Relaxed);
        IS_PLAYING_SAMPLE.store(if p.is_playing() { 1 } else { 2 }, Relaxed);
        use ruffle_common::duration::FloatDuration;
        let dt = FloatDuration::from_secs(dt_seconds as f64);
        let pre = p.current_frame().map(|n| n as i32).unwrap_or(-1);
        p.tick(dt);
        TICK_AFTER_TICK.fetch_add(1, Relaxed);
        TICK_AFTER_RUNFRAME.fetch_add(1, Relaxed);
        let post = p.current_frame().map(|n| n as i32).unwrap_or(-1);
        p.render();
        TICK_AFTER_RENDER.fetch_add(1, Relaxed);
        (pre, post)
    };

    // Step 3: publish samples and run the executor one more time so any
    // futures tick queued start resolving before the next tick. cf_mid is
    // now the same as cf_post (we no longer call run_frame separately);
    // kept as a field in the diag for overlay compatibility.
    LATEST_CF_PRE.store(cf_pre, Relaxed);
    LATEST_CF_MID.store(cf_post, Relaxed);
    LATEST_CF_POST.store(cf_post, Relaxed);
    if cf_post != cf_pre && cf_pre >= 0 && cf_post >= 0 {
        FRAME_ADVANCES.fetch_add(1, Relaxed);
    }
    handle.executor.borrow_mut().run();
}

/// Advance the player one simulated frame *without* rendering to the
/// wgpu surface. Drains async futures, calls Player::tick, but skips
/// Player::render - so the surface keeps showing whatever was last
/// drawn. Used by the iOS host during scene transitions so the new
/// scene can fully construct + run its Init/SetLabel wiring before
/// the first visible frame. Mirrors the console behaviour where
/// initialiseMovie + control init happen atomically before the scene
/// becomes visible.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_tick_headless(raw: *mut PlayerHandle, dt_seconds: f32) {
    let Some(handle) = borrow_handle(raw) else { return; };
    handle.executor.borrow_mut().run();
    {
        let Ok(mut p) = handle.player.lock() else { return; };
        use ruffle_common::duration::FloatDuration;
        let dt = FloatDuration::from_secs(dt_seconds as f64);
        p.tick(dt);
    }
    handle.executor.borrow_mut().run();
}

/// Same as ruffle_ios_player_tick_headless but the panorama, logo, and
/// tooltips (any Bitmap flagged as coming from the 4J XUI import path)
/// get their matrices snapshotted before the tick and restored after.
/// That keeps their visible positions locked across the burst of 30
/// headless ticks used on scene transitions, so the panorama doesn't
/// appear to jump leftward when you enter/exit a sub-menu.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_tick_headless_preserve_xui(
    raw: *mut PlayerHandle,
    dt_seconds: f32,
) {
    let Some(handle) = borrow_handle(raw) else { return; };
    handle.executor.borrow_mut().run();
    {
        let Ok(mut p) = handle.player.lock() else { return; };
        use ruffle_common::duration::FloatDuration;
        let dt = FloatDuration::from_secs(dt_seconds as f64);
        p.tick_preserving_xui(dt);
    }
    handle.executor.borrow_mut().run();
}

/// Stash the current matrix of every XUI-origin Bitmap. Pair with
/// ruffle_ios_player_restore_xui_matrices: call snapshot before a
/// scene transition starts (replace_swf + headless ticks + button
/// init), call restore after, and the panorama/logo/tooltips stay
/// visually locked through the entire sequence. Needed because the
/// 30 headless ticks aren't the only source of scroll drift; the
/// ~15 call_init_on_named_child calls after the burst each advance
/// the executor by a small amount that adds up.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_snapshot_xui_matrices(raw: *mut PlayerHandle) {
    let Some(handle) = borrow_handle(raw) else { return; };
    let Ok(mut p) = handle.player.lock() else { return; };
    p.snapshot_xui_matrices();
}

#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_restore_xui_matrices(raw: *mut PlayerHandle) {
    let Some(handle) = borrow_handle(raw) else { return; };
    let Ok(mut p) = handle.player.lock() else { return; };
    p.restore_xui_matrices();
}

/// Toggle play/stop on every stage child that isn't the root scene
/// clip (depth != 0). Used around scene-transition headless bursts
/// so the panorama, logo, and tooltips freeze their Timelines while
/// the new scene's construction chain runs at full dt. Without this,
/// 30 ticks at 1/60 dt advance the panorama scroll ~22 authored px
/// which reads as a visible leftward jump.
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_set_xui_siblings_playing(
    raw: *mut PlayerHandle,
    playing: c_int,
) {
    let Some(handle) = borrow_handle(raw) else { return; };
    let Ok(mut p) = handle.player.lock() else { return; };
    p.set_xui_siblings_playing(playing != 0);
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
///   out[3] = after_render, out[4] = executor_runs (per-handle).
/// `is_playing` receives 0 (unknown), 1 (true), or 2 (false).
#[no_mangle]
pub unsafe extern "C" fn ruffle_ios_player_diag(out_counters: *mut u64, len: usize,
                                                is_playing: *mut c_int,
                                                handle_ptr: *mut PlayerHandle) {
    use std::sync::atomic::Ordering::Relaxed;
    if !out_counters.is_null() && len >= 4 {
        *out_counters.add(0) = TICK_LOCK_OK.load(Relaxed);
        *out_counters.add(1) = TICK_AFTER_TICK.load(Relaxed);
        *out_counters.add(2) = TICK_AFTER_RUNFRAME.load(Relaxed);
        *out_counters.add(3) = TICK_AFTER_RENDER.load(Relaxed);
    }
    if !out_counters.is_null() && len >= 5 {
        let runs = borrow_handle(handle_ptr)
            .map(|h| h.executor_runs.load(Relaxed))
            .unwrap_or(0);
        *out_counters.add(4) = runs;
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
