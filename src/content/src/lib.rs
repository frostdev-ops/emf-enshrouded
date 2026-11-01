//! emf_content: EMF's C ABI around EML's mod loader.
//!
//! One call builds the content of a game folder's EML mods, the same two library calls `emm.exe run --patch` makes:
//! `ModEnvironment::load` and `mod_loader::lua::run`. The request and the reply are UTF-8 JSON; log lines go to a
//! callback. The ABI is in `emf_content.h`; README.md has the rules for loading the DLL.
//!
//! Nothing outlives a call: no panic leaves an export, no thread is started, and the log subscriber is set only for
//! the length of the call (never a global one). The reply goes into the caller's buffer, so no memory crosses.

use std::ffi::{CStr, c_char, c_int, c_void};
use std::io;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::time::Instant;

use mod_loader::ModEnvironment;
use mod_loader::lua::{RunArgs, RunOptions};
use serde::{Deserialize, Serialize};

/// The version of this ABI (`emf_content_abi()`).
pub const ABI: u32 = 1;

pub const OK: i32 = 0;
pub const MODS_FAILED: i32 = 1;
pub const RUN_FAILED: i32 = 2;
pub const BAD_REQUEST: i32 = 3;
pub const REPLY_TOO_SMALL: i32 = 4;
pub const PANIC: i32 = 5;

/// `void (*)(void* ctx, int level, const char* line)`: level 0 error, 1 warning, 2 info, 3 debug; one UTF-8 line
/// without a newline.
pub type LogFn = Option<unsafe extern "C" fn(ctx: *mut c_void, level: c_int, line: *const c_char)>;

/// The caller's log callback and its context.
#[derive(Clone, Copy)]
pub struct Logger {
    callback: LogFn,
    ctx: *mut c_void,
}

// The callback is only ever called on the thread that called emf_content_build, while that call runs; tracing's
// writer traits want Send + Sync all the same.
unsafe impl Send for Logger {}
unsafe impl Sync for Logger {}

impl Logger {
    pub fn new(callback: LogFn, ctx: *mut c_void) -> Self {
        Logger { callback, ctx }
    }

    pub fn none() -> Self {
        Logger { callback: None, ctx: std::ptr::null_mut() }
    }

    fn send(&self, level: c_int, text: &str) {
        let Some(callback) = self.callback else { return };
        for line in text.split('\n') {
            let line = line.trim_end_matches('\r');
            if line.is_empty() {
                continue;
            }
            let mut bytes = line.replace('\0', " ").into_bytes();
            bytes.push(0);
            // SAFETY: the caller promises the callback and its context are valid for the whole call.
            unsafe { callback(self.ctx, level, bytes.as_ptr() as *const c_char) };
        }
    }
}

fn level_number(level: &tracing::Level) -> c_int {
    match *level {
        tracing::Level::ERROR => 0,
        tracing::Level::WARN => 1,
        tracing::Level::INFO => 2,
        _ => 3,
    }
}

/// tracing's fmt layer makes one writer per event; it collects the formatted event and hands it to the callback,
/// line by line, when it is dropped.
struct LineWriter {
    logger: Logger,
    level: c_int,
    buf: Vec<u8>,
}

impl io::Write for LineWriter {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.buf.extend_from_slice(data);
        Ok(data.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl Drop for LineWriter {
    fn drop(&mut self) {
        let text = String::from_utf8_lossy(&self.buf);
        self.logger.send(self.level, &text);
    }
}

struct CallbackWriter(Logger);

impl<'a> tracing_subscriber::fmt::MakeWriter<'a> for CallbackWriter {
    type Writer = LineWriter;

    fn make_writer(&'a self) -> Self::Writer {
        LineWriter { logger: self.0, level: 2, buf: Vec::new() }
    }

    fn make_writer_for(&'a self, meta: &tracing::Metadata<'_>) -> Self::Writer {
        LineWriter { logger: self.0, level: level_number(meta.level()), buf: Vec::new() }
    }
}

#[derive(Deserialize)]
struct Request {
    game_dir: String,
    #[serde(default)]
    file_name: Option<String>,
    #[serde(default)]
    force: bool,
    #[serde(default)]
    server: Option<bool>,
}

#[derive(Serialize)]
struct ModInfo {
    id: String,
    version: String,
}

#[derive(Serialize)]
struct ModErrorInfo {
    id: Option<String>,
    path: String,
    error: String,
}

#[derive(Serialize, Default)]
struct Reply {
    code: i32,
    message: String,
    mods: Vec<ModInfo>,
    mod_errors: Vec<ModErrorInfo>,
    dlls: Vec<String>,
    ms: u64,
}

impl Reply {
    fn failed(code: i32, message: impl Into<String>) -> Self {
        Reply { code, message: message.into(), ..Default::default() }
    }

    fn to_json(&self) -> String {
        // Plain strings and numbers only: serialising cannot fail.
        serde_json::to_string(self).unwrap_or_else(|_| format!("{{\"code\":{}}}", self.code))
    }
}

/// Which game files to build: the request's `file_name`, else `enshrouded` or `enshrouded_server` when its .kfc or
/// .exe is there, else the first .kfc or .exe in the folder, else `enshrouded`.
fn resolve_file_name(game_dir: &Path, requested: Option<String>) -> String {
    if let Some(name) = requested.filter(|n| !n.is_empty()) {
        return name;
    }
    let has = |stem: &str| ["kfc", "exe"].iter().any(|ext| game_dir.join(format!("{stem}.{ext}")).is_file());
    if let Some(stem) = ["enshrouded", "enshrouded_server"].into_iter().find(|s| has(s)) {
        return stem.to_string();
    }
    let first = std::fs::read_dir(game_dir).ok().and_then(|entries| {
        entries.flatten().map(|e| e.path()).find_map(|p| {
            let ext = p.extension()?.to_str()?.to_ascii_lowercase();
            if ext == "kfc" || ext == "exe" { p.file_stem()?.to_str().map(str::to_string) } else { None }
        })
    });
    first.unwrap_or_else(|| "enshrouded".to_string())
}

fn sorted_mods(env: &ModEnvironment) -> Vec<ModInfo> {
    let mut mods: Vec<ModInfo> = env
        .mod_registry()
        .values()
        .map(|m| ModInfo { id: m.info().id.clone(), version: m.info().version.to_string() })
        .collect();
    mods.sort_by(|a, b| a.id.cmp(&b.id));
    mods
}

/// The build itself, inside the call's log subscriber.
fn build(request: Request) -> Reply {
    let game_dir = Path::new(&request.game_dir);
    tracing::info!(target: "emf::content", "Building the content of the EML mods in {}", request.game_dir);

    if !game_dir.is_dir() {
        let message = format!("The game folder does not exist or is not a folder: {}", request.game_dir);
        tracing::error!(target: "emf::content", "{message}");
        return Reply::failed(RUN_FAILED, message);
    }
    let file_name = resolve_file_name(game_dir, request.file_name);
    if !game_dir.join(format!("{file_name}.kfc")).is_file() && !game_dir.join(format!("{file_name}.exe")).is_file() {
        let message = format!("No {file_name}.kfc or {file_name}.exe in {}", request.game_dir);
        tracing::error!(target: "emf::content", "{message}");
        return Reply::failed(RUN_FAILED, message);
    }

    let env = match ModEnvironment::load(request.game_dir.as_str()) {
        Ok(env) => env,
        Err(report) => {
            if let Some(error) = report.error {
                let message = format!("Could not read the mods: {error}");
                tracing::error!(target: "emf::content", "{message}");
                return Reply::failed(RUN_FAILED, message);
            }
            let mut reply = Reply::failed(MODS_FAILED, "Some EML mods could not be loaded");
            for e in report.mods {
                tracing::error!(target: "emf::content", "In mod {} at {}: {}",
                    e.id.as_deref().unwrap_or("(no id)"), e.path, e.error);
                reply.mod_errors.push(ModErrorInfo { id: e.id, path: e.path.to_string(), error: e.error.to_string() });
            }
            return reply;
        }
    };

    let mods = sorted_mods(&env);
    let args = RunArgs {
        file_name,
        options: RunOptions {
            skip_cache: request.force,
            is_server: request.server,
            patch: true,
            export: false,
            runtime: false,
            export_dir: None,
        },
    };
    match mod_loader::lua::run(&env, args) {
        Ok(result) => Reply {
            code: OK,
            message: format!("The content of {} EML mods is up to date", mods.len()),
            mods,
            dlls: result.dlls.iter().map(|p| p.to_string_lossy().into_owned()).collect(),
            ..Default::default()
        },
        Err(e) => {
            let message = format!("{e:#}");
            tracing::error!(target: "emf::content", "The build failed: {message}");
            Reply { code: RUN_FAILED, message, mods, ..Default::default() }
        }
    }
}

/// Parses the request, runs the build with the caller's log callback, and gives the code and the reply JSON.
pub fn build_json(request_json: &str, logger: Logger) -> (i32, String) {
    let started = Instant::now();
    let mut reply = match serde_json::from_str::<Request>(request_json) {
        Err(e) => Reply::failed(BAD_REQUEST, format!("The request is not valid JSON for emf_content: {e}")),
        Ok(request) if request.game_dir.is_empty() => Reply::failed(BAD_REQUEST, "The request's game_dir is empty"),
        Ok(request) => {
            if logger.callback.is_some() {
                let subscriber = tracing_subscriber::fmt()
                    .with_ansi(false)
                    .without_time()
                    .with_level(false)
                    .with_target(true)
                    .with_max_level(tracing::Level::DEBUG)
                    .with_writer(CallbackWriter(logger))
                    .finish();
                tracing::subscriber::with_default(subscriber, || build(request))
            } else {
                build(request)
            }
        }
    };
    reply.ms = started.elapsed().as_millis() as u64;
    (reply.code, reply.to_json())
}

/// Runs `f` and turns a panic into a PANIC reply that carries the panic's message.
pub fn guarded<F: FnOnce() -> (i32, String)>(f: F) -> (i32, String) {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(result) => result,
        Err(payload) => {
            let text = payload
                .downcast_ref::<&str>()
                .map(|s| s.to_string())
                .or_else(|| payload.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "(no message)".to_string());
            (PANIC, Reply::failed(PANIC, format!("emf_content panicked: {text}")).to_json())
        }
    }
}

/// Copies `json` and a NUL into the caller's buffer. `*reply_len` always gets the length with the NUL; a buffer that
/// is too small (or NULL) gets nothing and the result is REPLY_TOO_SMALL.
///
/// # Safety
/// `reply` is NULL or points to `reply_cap` writable bytes; `reply_len` is NULL or points to a writable size_t.
pub unsafe fn write_reply(json: &str, reply: *mut c_char, reply_cap: usize, reply_len: *mut usize) -> Result<(), i32> {
    let needed = json.len() + 1;
    if !reply_len.is_null() {
        unsafe { *reply_len = needed };
    }
    if reply.is_null() || reply_cap < needed {
        return Err(REPLY_TOO_SMALL);
    }
    unsafe {
        std::ptr::copy_nonoverlapping(json.as_ptr(), reply as *mut u8, json.len());
        *reply.add(json.len()) = 0;
    }
    Ok(())
}

/// The ABI version: 1.
#[unsafe(no_mangle)]
pub extern "C" fn emf_content_abi() -> u32 {
    ABI
}

/// Builds the content of a game folder's EML mods. See emf_content.h.
///
/// # Safety
/// `request_json` is NULL or a NUL-terminated string; `log` is NULL or a function that may be called with `log_ctx`
/// during the call; `reply` is NULL or points to `reply_cap` writable bytes; `reply_len` points to a size_t.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn emf_content_build(
    request_json: *const c_char,
    log: LogFn,
    log_ctx: *mut c_void,
    reply: *mut c_char,
    reply_cap: usize,
    reply_len: *mut usize,
) -> i32 {
    if reply_len.is_null() {
        return BAD_REQUEST;
    }
    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let (code, json) = guarded(|| {
            if request_json.is_null() {
                return (BAD_REQUEST, Reply::failed(BAD_REQUEST, "The request is NULL").to_json());
            }
            match unsafe { CStr::from_ptr(request_json) }.to_str() {
                Ok(text) => build_json(text, Logger::new(log, log_ctx)),
                Err(e) => (BAD_REQUEST, Reply::failed(BAD_REQUEST, format!("The request is not UTF-8: {e}")).to_json()),
            }
        });
        match unsafe { write_reply(&json, reply, reply_cap, reply_len) } {
            Ok(()) => code,
            Err(too_small) => too_small,
        }
    }));
    outcome.unwrap_or(PANIC)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::{CStr, c_char, c_int, c_void};
    use std::path::PathBuf;

    /// A folder in %TEMP% that is removed again when the test ends, also when it fails.
    struct TempDir(PathBuf);

    impl TempDir {
        fn new(name: &str) -> Self {
            let unique = format!(
                "emf_content_{name}_{}_{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            );
            let path = std::env::temp_dir().join(unique);
            std::fs::create_dir_all(&path).unwrap();
            TempDir(path)
        }

        fn utf8(&self) -> &str {
            self.0.to_str().unwrap()
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn request_for(dir: &str) -> String {
        serde_json::json!({ "game_dir": dir }).to_string()
    }

    fn parse(reply: &str) -> serde_json::Value {
        serde_json::from_str(reply).expect("the reply is JSON")
    }

    unsafe extern "C" fn collect(ctx: *mut c_void, level: c_int, line: *const c_char) {
        let lines = unsafe { &mut *(ctx as *mut Vec<(i32, String)>) };
        let line = unsafe { CStr::from_ptr(line) }.to_string_lossy().into_owned();
        lines.push((level, line));
    }

    /// Calls the exported function the way C does, with a reply buffer of `cap` bytes.
    fn call_export(request: &str, cap: usize, fill: u8) -> (i32, Vec<u8>, usize) {
        let request = std::ffi::CString::new(request).unwrap();
        let mut buf = vec![fill; cap];
        let mut len = 0usize;
        let ptr = if cap == 0 { std::ptr::null_mut() } else { buf.as_mut_ptr() as *mut c_char };
        let code = unsafe {
            emf_content_build(request.as_ptr(), None, std::ptr::null_mut(), ptr, cap, &mut len)
        };
        (code, buf, len)
    }

    #[test]
    fn bad_json_is_bad_request() {
        let (code, reply) = build_json("{", Logger::none());
        assert_eq!(code, BAD_REQUEST);
        let reply = parse(&reply);
        assert_eq!(reply["code"], BAD_REQUEST);
        let message = reply["message"].as_str().unwrap();
        assert!(message.contains("JSON"), "{message}");
        assert!(message.contains("EOF"), "the message names serde's error: {message}");
    }

    #[test]
    fn missing_game_dir_is_bad_request() {
        let (code, reply) = build_json("{}", Logger::none());
        assert_eq!(code, BAD_REQUEST);
        assert!(parse(&reply)["message"].as_str().unwrap().contains("game_dir"));
    }

    #[test]
    fn folder_without_kfc_is_run_failed() {
        let dir = TempDir::new("empty");
        let (code, reply) = build_json(&request_for(dir.utf8()), Logger::none());
        assert_ne!(code, OK);
        assert!(code == MODS_FAILED || code == RUN_FAILED, "code {code}");
        let reply = parse(&reply);
        assert_eq!(reply["code"], code);
        let message = reply["message"].as_str().unwrap();
        assert!(message.contains(dir.utf8()), "the message names the folder: {message}");
        assert_eq!(reply["mods"].as_array().unwrap().len(), 0);
        assert!(reply["mod_errors"].as_array().unwrap().is_empty());
        assert!(reply["dlls"].as_array().unwrap().is_empty());
        assert!(reply["ms"].is_u64());
    }

    #[test]
    fn reply_too_small_reports_needed_length() {
        let dir = TempDir::new("small");
        let (code, buf, len) = call_export(&request_for(dir.utf8()), 4, b'#');
        assert_eq!(code, REPLY_TOO_SMALL);
        assert!(len > 4, "needed length {len}");
        assert_eq!(buf, b"####".to_vec(), "nothing is written into too small a buffer");
    }

    #[test]
    fn retry_with_needed_length_succeeds() {
        let dir = TempDir::new("retry");
        let request = request_for(dir.utf8());
        let (big_code, _, _) = call_export(&request, 64 * 1024, 0);
        let (code, _, needed) = call_export(&request, 0, 0);
        assert_eq!(code, REPLY_TOO_SMALL);
        let (code, buf, len) = call_export(&request, needed, b'#');
        assert_eq!(code, big_code);
        assert_eq!(len, needed);
        assert_eq!(buf[needed - 1], 0, "the reply ends with a NUL");
        let text = std::str::from_utf8(&buf[..needed - 1]).unwrap();
        assert_eq!(parse(text)["code"], big_code);
    }

    #[test]
    fn panic_is_caught() {
        let (code, reply) = guarded(|| panic!("x marks the panic"));
        assert_eq!(code, PANIC);
        let reply = parse(&reply);
        assert_eq!(reply["code"], PANIC);
        assert!(reply["message"].as_str().unwrap().contains("x marks the panic"));
        let (code, _) = build_json("{}", Logger::none());
        assert_eq!(code, BAD_REQUEST, "the next call works");
    }

    #[test]
    fn log_lines_reach_the_callback() {
        let dir = TempDir::new("log");
        let mut lines: Vec<(i32, String)> = Vec::new();
        let logger = Logger::new(Some(collect), &mut lines as *mut _ as *mut c_void);
        let _ = build_json(&request_for(dir.utf8()), logger);
        assert!(!lines.is_empty());
        let info = lines.iter().find(|(level, _)| *level == 2).expect("an info line");
        assert!(info.1.contains(dir.utf8()), "{}", info.1);
        for (_, line) in &lines {
            assert!(!line.contains('\n') && !line.contains('\r'), "{line:?}");
        }
    }

    #[test]
    fn eml_runs_and_its_errors_reach_the_callback() {
        // Not a game: an exe EML cannot read its types from. EML's own code runs (it makes mods\ and .cache\) and fails.
        let dir = TempDir::new("fake_game");
        std::fs::write(dir.0.join("enshrouded.exe"), b"MZ but not a game").unwrap();
        let mut lines: Vec<(i32, String)> = Vec::new();
        let logger = Logger::new(Some(collect), &mut lines as *mut _ as *mut c_void);
        let (code, reply) = build_json(&request_for(dir.utf8()), logger);
        assert_eq!(code, RUN_FAILED);
        assert!(dir.0.join("mods").is_dir(), "EML's ModEnvironment::load ran");
        let reply = parse(&reply);
        assert!(!reply["message"].as_str().unwrap().is_empty());
        assert!(reply["mods"].as_array().unwrap().is_empty());
        let eml_error = lines.iter().find(|(level, line)| *level == 0 && line.starts_with("eml::"));
        assert!(eml_error.is_some(), "an error line from EML itself: {lines:?}");
        assert!(eml_error.unwrap().1.contains("enshrouded.exe"), "{lines:?}");
    }

    #[test]
    fn abi_is_one() {
        assert_eq!(emf_content_abi(), 1);
    }
}
