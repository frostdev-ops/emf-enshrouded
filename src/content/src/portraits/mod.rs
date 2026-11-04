//! Portraits: frames of a game creature's own model turning, drawn on the player's PC from the player's own game
//! files (nothing of the game's art is shipped). `emf_content_portraits` (lib.rs) runs one request:
//!
//! ```json
//! {"game_dir": "...", "file_name": "enshrouded", "cache_dir": "<game>\\emf\\cache\\portraits",
//!  "items": [{"key": "wolf", "template": "<guid>", "frames": 24, "size": 256}]}
//! ```
//!
//! Each item names a render model (`model`) or a template whose `keen::ecs::ModelResource` names one (`template`).
//! Its frames go to `<cache_dir>\<key>\00.png, 01.png, ...` with an `index.json` beside them (the frame count, the
//! size and the inputs: the render model, the content hashes of its render data and albedo textures, and a hash of
//! its records). An item whose index already matches is left as it is; a game update that changes the model, or a
//! request with other frames or size, draws it again. Stale frames past the new count are removed.
//!
//! Part of emf_content.dll, which as a whole is GPL-3.0-or-later (README.md).

pub mod game;
pub mod model;
pub mod raster;

use std::path::Path;

use serde::{Deserialize, Serialize};
use serde_json::Value;

/// Frames when an item does not say.
pub const DEFAULT_FRAMES: u32 = 24;
/// Frame size when an item does not say.
pub const DEFAULT_SIZE: u32 = 256;
/// The longest key.
pub const MAX_KEY: usize = 64;
/// The albedo mip a portrait is drawn with: the largest at most this many pixels.
pub const TEXTURE_SIDE: u32 = 256;
/// How frames are drawn: a change here draws every portrait again.
pub const RENDERER: u32 = 1;
/// The index file beside the frames.
pub const INDEX: &str = "index.json";

/// What an item asks to draw.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Target {
    /// A `keen::RenderModel` by GUID.
    Model(String),
    /// A `keen::ecs::TemplateResource` by GUID, drawn with the render model its ModelResource names.
    Template(String),
}

/// What a portrait is drawn from. Equal inputs draw equal frames.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Inputs {
    /// The render model's GUID.
    pub model: String,
    /// The render model's debugName.
    pub name: String,
    /// The content hash of its render data.
    pub render_data: String,
    /// Per material slot, the content hash of the albedo texture drawn (null: none).
    pub albedo: Vec<Option<String>>,
    /// A hash of the records' bytes: the render model's and its materials'.
    pub records: String,
}

/// Where models come from: the game's archive, or a fake in tests.
pub trait Source {
    /// Resolves the target and reads what drawing it depends on, without reading the render data or textures.
    fn inputs(&mut self, target: &Target) -> Result<Inputs, String>;
    /// The model to draw, and notes on what was left out (a mesh in another format, a texture not decoded).
    fn model(&mut self, inputs: &Inputs) -> Result<(raster::Model, Vec<String>), String>;
}

/// The request (`emf_content_portraits`'s JSON).
#[derive(Deserialize)]
pub struct Request {
    #[serde(default)]
    pub game_dir: String,
    #[serde(default)]
    pub file_name: Option<String>,
    #[serde(default)]
    pub cache_dir: String,
    /// Read one by one, so a bad item fails alone.
    #[serde(default)]
    pub items: Vec<Value>,
    /// For tests only: the call panics with this message.
    #[serde(default)]
    pub test_panic: Option<String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Status {
    /// Drawn at this call.
    Drawn,
    /// Its index matched: left as it was.
    Current,
    /// Not drawn: see `error`. Frames from an earlier call stay.
    Failed,
}

/// One item's result, in the order of the request.
#[derive(Clone, Debug, Serialize)]
pub struct ItemReply {
    /// The key as given (null when it is missing or not text).
    pub key: Option<String>,
    pub status: Status,
    /// The render model drawn (resolved from the template when the item names one).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    /// The render model's debugName.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub frames: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub size: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    /// What was left out of the drawing.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub notes: Vec<String>,
}

/// The reply (`emf_content_portraits`'s JSON).
#[derive(Serialize, Deserialize, Default)]
pub struct Reply {
    pub code: i32,
    pub message: String,
    /// Only in a reply cut to fit the caller's buffer.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub truncated: bool,
    #[serde(skip_deserializing)]
    pub items: Vec<ItemReply>,
    pub ms: u64,
}

impl Reply {
    pub fn failed(code: i32, message: impl Into<String>) -> Self {
        Reply { code, message: message.into(), ..Default::default() }
    }

    pub fn to_json(&self) -> String {
        serde_json::to_string(self).unwrap_or_else(|_| format!("{{\"code\":{}}}", self.code))
    }
}

/// A key names a folder in the cache and the overlay's `portrait:<key>`: 1 to 64 of A-Z a-z 0-9 _ . - (the
/// overlay's rule), and, because Windows would read it as another folder or a device, not ending in a dot and not
/// a device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, also with an extension).
pub fn check_key(key: &str) -> Result<(), String> {
    if key.is_empty() || key.len() > MAX_KEY {
        return Err(format!("the key must be 1 to {MAX_KEY} characters long, not {}", key.chars().count()));
    }
    if let Some(c) = key.chars().find(|c| !(c.is_ascii_alphanumeric() || matches!(c, '_' | '.' | '-'))) {
        return Err(format!("the key may hold only A-Z a-z 0-9 _ . - (not {c:?})"));
    }
    if key.ends_with('.') {
        return Err("the key may not end in a dot (Windows drops it from a folder's name)".into());
    }
    let stem = key.split('.').next().unwrap_or_default().to_ascii_uppercase();
    let device = matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || (stem.len() == 4
            && (stem.starts_with("COM") || stem.starts_with("LPT"))
            && stem.as_bytes()[3].is_ascii_digit());
    if device {
        return Err(format!("the key {key:?} is a Windows device name"));
    }
    Ok(())
}

/// One item of the request, checked.
struct Item {
    key: String,
    target: Target,
    frames: u32,
    size: u32,
}

fn guid_text(item: &Value, name: &str) -> Result<Option<String>, String> {
    match item.get(name) {
        None | Some(Value::Null) => Ok(None),
        Some(Value::String(text)) if kfc::guid::Guid::parse(text).is_some() => Ok(Some(text.clone())),
        Some(other) => Err(format!("{name} must be a GUID as text (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx), not {other}")),
    }
}

fn whole(item: &Value, name: &str, default: u32, range: std::ops::RangeInclusive<u32>) -> Result<u32, String> {
    let value = match item.get(name) {
        None | Some(Value::Null) => return Ok(default),
        Some(v) => v,
    };
    match value.as_u64() {
        Some(n) if n >= *range.start() as u64 && n <= *range.end() as u64 => Ok(n as u32),
        _ => Err(format!("{name} must be a whole number from {} to {}, not {value}", range.start(), range.end())),
    }
}

/// Checks one item; `earlier` holds the keys of the good items before it (lower case: Windows folder names ignore
/// case).
fn check_item(item: &Value, earlier: &[(String, String)]) -> Result<Item, String> {
    if !item.is_object() {
        return Err(format!("an item must be an object, not {item}"));
    }
    let key = match item.get("key") {
        Some(Value::String(key)) => key.clone(),
        None | Some(Value::Null) => return Err("the item has no key".into()),
        Some(other) => return Err(format!("the key must be text, not {other}")),
    };
    check_key(&key)?;
    if let Some((_, first)) = earlier.iter().find(|(lower, _)| *lower == key.to_ascii_lowercase()) {
        return Err(format!("the key {key:?} is the same folder as the earlier item's key {first:?}"));
    }
    let target = match (guid_text(item, "model")?, guid_text(item, "template")?) {
        (Some(model), None) => Target::Model(model),
        (None, Some(template)) => Target::Template(template),
        (Some(_), Some(_)) => return Err("the item names both a model and a template: give one".into()),
        (None, None) => return Err("the item names no model and no template".into()),
    };
    Ok(Item {
        key,
        target,
        frames: whole(item, "frames", DEFAULT_FRAMES, 1..=raster::MAX_FRAMES)?,
        size: whole(item, "size", DEFAULT_SIZE, raster::MIN_SIZE..=raster::MAX_SIZE)?,
    })
}

/// What `index.json` holds.
#[derive(Debug, PartialEq, Serialize, Deserialize)]
struct Index {
    renderer: u32,
    frames: u32,
    size: u32,
    /// The template the item named, if it named one.
    template: Option<String>,
    inputs: Inputs,
}

fn frame_name(i: usize) -> String {
    format!("{i:02}.png")
}

/// The index matches and every frame it names is there.
fn is_current(folder: &Path, index: &Index) -> bool {
    let Ok(bytes) = std::fs::read(folder.join(INDEX)) else { return false };
    let Ok(found) = serde_json::from_slice::<Index>(&bytes) else { return false };
    found == *index && (0..index.frames as usize).all(|i| folder.join(frame_name(i)).is_file())
}

/// Writes `bytes` to `path` through a `.tmp` beside it, so a file is either whole or the old one.
fn write_file(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let mut tmp = path.as_os_str().to_owned();
    tmp.push(".tmp");
    let tmp = std::path::PathBuf::from(tmp);
    std::fs::write(&tmp, bytes).map_err(|e| format!("could not write {}: {e}", tmp.display()))?;
    std::fs::rename(&tmp, path).map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        format!("could not write {}: {e}", path.display())
    })
}

/// Removes the frame files other than 00.png .. (frames-1).png: files named by a number alone, and `.tmp` files.
fn remove_stale_frames(folder: &Path, frames: usize) -> Result<(), String> {
    let entries = std::fs::read_dir(folder).map_err(|e| format!("could not list {}: {e}", folder.display()))?;
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let stale = match name.strip_suffix(".png") {
            Some(stem) if !stem.is_empty() && stem.bytes().all(|b| b.is_ascii_digit()) => {
                stem.parse::<usize>().map_or(true, |n| n >= frames || stem != format!("{n:02}"))
            }
            _ => name.ends_with(".tmp"),
        };
        if stale && entry.path().is_file() {
            std::fs::remove_file(entry.path()).map_err(|e| format!("could not remove {name}: {e}"))?;
        }
    }
    Ok(())
}

/// Writes the frames and then the index (removed first, so a write cut short is drawn again next time), and
/// removes stale frames.
fn write_portrait(folder: &Path, pngs: &[Vec<u8>], index: &Index) -> Result<(), String> {
    std::fs::create_dir_all(folder).map_err(|e| format!("could not make {}: {e}", folder.display()))?;
    match std::fs::remove_file(folder.join(INDEX)) {
        Err(e) if e.kind() != std::io::ErrorKind::NotFound => {
            return Err(format!("could not remove the old {INDEX}: {e}"));
        }
        _ => {}
    }
    for (i, png) in pngs.iter().enumerate() {
        write_file(&folder.join(frame_name(i)), png)?;
    }
    remove_stale_frames(folder, pngs.len())?;
    let json = serde_json::to_vec_pretty(index).map_err(|e| format!("{INDEX}: {e}"))?;
    write_file(&folder.join(INDEX), &json)
}

/// Draws one item unless it is current.
fn draw_item<S: Source>(source: &mut S, item: &Item, cache_dir: &Path) -> Result<(Status, Inputs, Vec<String>), String> {
    let inputs = source.inputs(&item.target)?;
    let template = match &item.target {
        Target::Template(t) => Some(t.clone()),
        Target::Model(_) => None,
    };
    let index = Index { renderer: RENDERER, frames: item.frames, size: item.size, template, inputs };
    let folder = cache_dir.join(&item.key);
    if is_current(&folder, &index) {
        return Ok((Status::Current, index.inputs, Vec::new()));
    }
    let (model, notes) = source.model(&index.inputs)?;
    let frames = raster::render_turntable(&model, &raster::View::new(item.frames, item.size))?;
    let pngs = frames.iter().map(raster::encode_png).collect::<Result<Vec<_>, _>>()?;
    write_portrait(&folder, &pngs, &index)?;
    Ok((Status::Drawn, index.inputs, notes))
}

fn failed(key: Option<String>, error: String) -> ItemReply {
    ItemReply { key, status: Status::Failed, model: None, name: None, frames: None, size: None, error: Some(error),
        notes: Vec::new() }
}

/// Runs the request: checks it, opens the game's archive with `open(game_dir, file_name)` once when an item needs
/// it, and draws every item that is not current.
pub fn run<S: Source>(request: Request, file_name: &str, open: impl FnOnce(&Path, &str) -> Result<S, String>) -> Reply {
    if let Some(message) = request.test_panic {
        panic!("{message}");
    }
    if request.game_dir.is_empty() {
        return Reply::failed(crate::BAD_REQUEST, "The request's game_dir is empty");
    }
    let cache_dir = Path::new(&request.cache_dir);
    if request.cache_dir.is_empty() || !cache_dir.is_absolute() {
        let message = format!("The request's cache_dir must be a full path, not {:?}", request.cache_dir);
        return Reply::failed(crate::BAD_REQUEST, message);
    }

    let mut keys: Vec<(String, String)> = Vec::new();
    let checked: Vec<(Option<String>, Result<Item, String>)> = request
        .items
        .iter()
        .map(|value| {
            let given = value.get("key").and_then(Value::as_str).map(str::to_string);
            let item = check_item(value, &keys);
            if let Ok(item) = &item {
                keys.push((item.key.to_ascii_lowercase(), item.key.clone()));
            }
            (given, item)
        })
        .collect();

    let mut source = None;
    let mut open_error = None;
    if checked.iter().any(|(_, item)| item.is_ok()) {
        match open(Path::new(&request.game_dir), file_name) {
            Ok(s) => source = Some(s),
            Err(e) => {
                tracing::error!(target: "emf::portraits", "The game's archive could not be read: {e}");
                open_error = Some(e);
            }
        }
    }

    let mut items = Vec::with_capacity(checked.len());
    for (given, item) in checked {
        let item = match item {
            Err(error) => {
                tracing::warn!(target: "emf::portraits", "Portrait {}: {error}", given.as_deref().unwrap_or("(no key)"));
                items.push(failed(given, error));
                continue;
            }
            Ok(item) => item,
        };
        let Some(source) = source.as_mut() else {
            let error = format!("the game's archive could not be read: {}", open_error.as_deref().unwrap_or_default());
            items.push(failed(given, error));
            continue;
        };
        let started = std::time::Instant::now();
        match draw_item(source, &item, cache_dir) {
            Ok((status, inputs, notes)) => {
                if status == Status::Drawn {
                    tracing::info!(target: "emf::portraits", "Portrait {}: drew {} frames of {} px of {} ({}) in {} ms",
                        item.key, item.frames, item.size, inputs.name, inputs.model, started.elapsed().as_millis());
                } else {
                    tracing::debug!(target: "emf::portraits", "Portrait {}: current", item.key);
                }
                for note in &notes {
                    tracing::warn!(target: "emf::portraits", "Portrait {}: {note}", item.key);
                }
                items.push(ItemReply {
                    key: Some(item.key),
                    status,
                    model: Some(inputs.model),
                    name: Some(inputs.name),
                    frames: Some(item.frames),
                    size: Some(item.size),
                    error: None,
                    notes,
                });
            }
            Err(error) => {
                tracing::warn!(target: "emf::portraits", "Portrait {}: {error}", item.key);
                items.push(failed(Some(item.key), error));
            }
        }
    }

    let count = |s: Status| items.iter().filter(|i| i.status == s).count();
    let (drawn, current, failed) = (count(Status::Drawn), count(Status::Current), count(Status::Failed));
    let mut message = format!("{} portraits: {drawn} drawn, {current} current, {failed} failed", items.len());
    let code = if let Some(e) = &open_error {
        message = format!("The game's archive in {} could not be read: {e}", request.game_dir);
        crate::RUN_FAILED
    } else if failed > 0 {
        crate::ITEMS_FAILED
    } else {
        crate::OK
    };
    Reply { code, message, items, ..Default::default() }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{BAD_REQUEST, ITEMS_FAILED, OK, RUN_FAILED};
    use raster::tests::{cube, solid};
    use serde_json::json;
    use std::cell::Cell;
    use std::path::PathBuf;
    use std::rc::Rc;

    const WOLF: &str = "91e2d4cc-01c9-4203-a92e-b08769364522";
    const BOAR: &str = "0bee6531-ef51-4925-9cd3-a40d11561165";
    const WOLF_TEMPLATE: &str = "478381c5-585f-4264-b373-dbb1ea596eca";
    const MISSING: &str = "00000000-1111-2222-3333-444444444444";

    struct TempDir(PathBuf);

    impl TempDir {
        fn new(name: &str) -> Self {
            let unique = format!(
                "emf_portraits_{name}_{}_{}",
                std::process::id(),
                std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
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

    /// Counts what the run asked of the fake game.
    #[derive(Default)]
    struct Calls {
        opened: Cell<u32>,
        inputs: Cell<u32>,
        models: Cell<u32>,
    }

    /// Two models (a wolf and a boar, cubes of their own colours) and a template that runs the wolf. `edition`
    /// stands for the game's files: another edition gives other inputs, as a game update would.
    struct Fake {
        calls: Rc<Calls>,
        edition: u32,
        broken: Option<String>,
    }

    impl Source for Fake {
        fn inputs(&mut self, target: &Target) -> Result<Inputs, String> {
            self.calls.inputs.set(self.calls.inputs.get() + 1);
            let guid = match target {
                Target::Model(g) => g.clone(),
                Target::Template(g) if g == WOLF_TEMPLATE => WOLF.to_string(),
                Target::Template(g) => return Err(format!("no template {g}")),
            };
            if self.broken.as_deref() == Some(guid.as_str()) || (guid != WOLF && guid != BOAR) {
                return Err(format!("no render model {guid}"));
            }
            Ok(Inputs {
                model: guid.clone(),
                name: if guid == WOLF { "wolf".into() } else { "boar".into() },
                render_data: format!("render-{guid}-{}", self.edition),
                albedo: vec![Some(format!("albedo-{guid}-{}", self.edition))],
                records: format!("records-{}", self.edition),
            })
        }

        fn model(&mut self, inputs: &Inputs) -> Result<(raster::Model, Vec<String>), String> {
            self.calls.models.set(self.calls.models.get() + 1);
            let colour = if inputs.model == WOLF { [150, 150, 160, 255] } else { [120, 80, 40, 255] };
            let materials = vec![raster::Material { texture: Some(solid(2, 2, colour)), ..Default::default() }];
            Ok((raster::Model { meshes: vec![cube(1.0, 0)], materials }, vec!["a note".into()]))
        }
    }

    struct Run {
        calls: Rc<Calls>,
        reply: Reply,
    }

    fn run_fake(cache: &TempDir, items: Value, edition: u32, broken: Option<&str>) -> Run {
        let calls = Rc::new(Calls::default());
        let request: Request = serde_json::from_value(json!({
            "game_dir": "C:\\Game", "cache_dir": cache.utf8(), "items": items
        }))
        .unwrap();
        let c = calls.clone();
        let reply = run(request, "enshrouded", move |dir: &Path, name: &str| {
            assert_eq!(dir, Path::new("C:\\Game"));
            assert_eq!(name, "enshrouded");
            c.opened.set(c.opened.get() + 1);
            Ok(Fake { calls: c.clone(), edition, broken: broken.map(str::to_string) })
        });
        Run { calls, reply }
    }

    fn frame_files(folder: &Path) -> Vec<String> {
        let mut names: Vec<String> = std::fs::read_dir(folder)
            .unwrap()
            .flatten()
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .filter(|n| n != INDEX)
            .collect();
        names.sort();
        names
    }

    fn index_of(folder: &Path) -> Value {
        serde_json::from_slice(&std::fs::read(folder.join(INDEX)).unwrap()).unwrap()
    }

    fn png_size(path: &Path) -> (u32, u32) {
        let decoder = png::Decoder::new(std::io::BufReader::new(std::fs::File::open(path).unwrap()));
        let reader = decoder.read_info().unwrap();
        (reader.info().width, reader.info().height)
    }

    #[test]
    fn keys_follow_the_overlays_rule() {
        let longest = "k".repeat(MAX_KEY);
        for good in ["wolf", "a", "Beast_01.v2-final", ".hidden", &longest, "con1", "nullable", "com10"] {
            assert_eq!(check_key(good), Ok(()), "{good}");
        }
        let long = "k".repeat(MAX_KEY + 1);
        for bad in ["", &long, "a b", "a/b", "a\\b", "wolf:1", "w\u{e4}lf", "x*", "..", ".", "wolf.", "CON", "nul",
            "Aux.txt", "COM1", "lpt9.png", "prn"] {
            let error = check_key(bad).unwrap_err();
            assert!(error.contains("key"), "{bad:?}: {error}");
        }
    }

    #[test]
    fn nothing_to_draw_is_ok_and_opens_nothing() {
        let cache = TempDir::new("empty");
        let run = run_fake(&cache, json!([]), 1, None);
        assert_eq!(run.reply.code, OK, "{}", run.reply.message);
        assert!(run.reply.items.is_empty());
        assert_eq!(run.calls.opened.get(), 0);
    }

    #[test]
    fn items_are_drawn_into_their_folders_with_an_index() {
        let cache = TempDir::new("draw");
        let items = json!([{"key": "wolf", "model": WOLF, "frames": 6, "size": 48}, {"key": "boar", "model": BOAR}]);
        let run = run_fake(&cache, items, 1, None);
        assert_eq!(run.reply.code, OK, "{}", run.reply.message);
        assert_eq!(run.calls.opened.get(), 1, "the archive is opened once");
        let wolf = &run.reply.items[0];
        assert_eq!((wolf.key.as_deref(), wolf.status), (Some("wolf"), Status::Drawn));
        assert_eq!((wolf.frames, wolf.size), (Some(6), Some(48)));
        assert_eq!(wolf.model.as_deref(), Some(WOLF));
        assert_eq!(wolf.name.as_deref(), Some("wolf"));
        assert_eq!(wolf.notes, vec!["a note".to_string()]);
        let folder = cache.0.join("wolf");
        assert_eq!(frame_files(&folder), ["00.png", "01.png", "02.png", "03.png", "04.png", "05.png"]);
        assert_eq!(png_size(&folder.join("03.png")), (48, 48));
        let index = index_of(&folder);
        assert_eq!(index["frames"], 6);
        assert_eq!(index["size"], 48);
        assert_eq!(index["renderer"], RENDERER);
        assert_eq!(index["inputs"]["model"], WOLF);
        assert_eq!(index["inputs"]["render_data"], format!("render-{WOLF}-1"));
        // The defaults: 24 frames of 256 px
        let boar = &run.reply.items[1];
        assert_eq!((boar.status, boar.frames, boar.size), (Status::Drawn, Some(DEFAULT_FRAMES), Some(DEFAULT_SIZE)));
        assert_eq!(frame_files(&cache.0.join("boar")).len(), DEFAULT_FRAMES as usize);
        assert_eq!(png_size(&cache.0.join("boar").join("23.png")), (DEFAULT_SIZE, DEFAULT_SIZE));
    }

    #[test]
    fn a_current_item_is_not_drawn_again() {
        let cache = TempDir::new("current");
        let items = json!([{"key": "wolf", "model": WOLF, "frames": 4, "size": 32}]);
        run_fake(&cache, items.clone(), 1, None);
        std::fs::write(cache.0.join("wolf").join("02.png"), b"marker").unwrap();
        let run = run_fake(&cache, items, 1, None);
        assert_eq!(run.reply.code, OK);
        assert_eq!(run.reply.items[0].status, Status::Current);
        assert_eq!((run.reply.items[0].frames, run.reply.items[0].size), (Some(4), Some(32)));
        assert_eq!(run.reply.items[0].model.as_deref(), Some(WOLF));
        assert_eq!(run.calls.inputs.get(), 1, "the inputs are read to compare");
        assert_eq!(run.calls.models.get(), 0, "the model is not read");
        assert_eq!(std::fs::read(cache.0.join("wolf").join("02.png")).unwrap(), b"marker", "not written");
    }

    #[test]
    fn changed_inputs_draw_again() {
        let cache = TempDir::new("update");
        let items = json!([{"key": "wolf", "model": WOLF, "frames": 4, "size": 32}]);
        run_fake(&cache, items.clone(), 1, None);
        let run = run_fake(&cache, items, 2, None);
        assert_eq!(run.reply.items[0].status, Status::Drawn, "a game update changed the model");
        assert_eq!(index_of(&cache.0.join("wolf"))["inputs"]["render_data"], format!("render-{WOLF}-2"));
    }

    #[test]
    fn other_size_or_frames_or_a_missing_frame_draw_again() {
        let cache = TempDir::new("redraw");
        run_fake(&cache, json!([{"key": "wolf", "model": WOLF, "frames": 4, "size": 32}]), 1, None);
        let run = run_fake(&cache, json!([{"key": "wolf", "model": WOLF, "frames": 4, "size": 40}]), 1, None);
        assert_eq!(run.reply.items[0].status, Status::Drawn);
        assert_eq!(png_size(&cache.0.join("wolf").join("00.png")), (40, 40));
        std::fs::remove_file(cache.0.join("wolf").join("03.png")).unwrap();
        let run = run_fake(&cache, json!([{"key": "wolf", "model": WOLF, "frames": 4, "size": 40}]), 1, None);
        assert_eq!(run.reply.items[0].status, Status::Drawn);
        assert!(cache.0.join("wolf").join("03.png").is_file());
    }

    #[test]
    fn fewer_frames_remove_the_stale_ones() {
        let cache = TempDir::new("fewer");
        run_fake(&cache, json!([{"key": "wolf", "model": WOLF, "frames": 8, "size": 32}]), 1, None);
        let folder = cache.0.join("wolf");
        assert_eq!(frame_files(&folder).len(), 8);
        std::fs::write(folder.join("12.png"), b"older").unwrap();
        std::fs::write(folder.join("5.png"), b"not one of ours by name").unwrap();
        std::fs::write(folder.join("notes.txt"), b"not a frame").unwrap();
        let run = run_fake(&cache, json!([{"key": "wolf", "model": WOLF, "frames": 3, "size": 32}]), 1, None);
        assert_eq!(run.reply.items[0].status, Status::Drawn);
        assert_eq!(frame_files(&folder), ["00.png", "01.png", "02.png", "notes.txt"]);
        assert_eq!(index_of(&folder)["frames"], 3, "the index's count matches the frames written");
        let run = run_fake(&cache, json!([{"key": "wolf", "model": WOLF, "frames": 3, "size": 32}]), 1, None);
        assert_eq!(run.reply.items[0].status, Status::Current);
    }

    #[test]
    fn a_template_is_drawn_with_its_model() {
        let cache = TempDir::new("template");
        let run = run_fake(&cache, json!([{"key": "wolf", "template": WOLF_TEMPLATE, "frames": 2, "size": 16}]), 1, None);
        assert_eq!(run.reply.items[0].status, Status::Drawn, "{:?}", run.reply.items[0].error);
        assert_eq!(run.reply.items[0].model.as_deref(), Some(WOLF));
        assert_eq!(index_of(&cache.0.join("wolf"))["template"], WOLF_TEMPLATE);
    }

    #[test]
    fn bad_items_fail_alone_with_their_reason() {
        let cache = TempDir::new("bad");
        let items = json!([
            {"key": "good", "model": WOLF, "frames": 2, "size": 16},
            {"key": "a b", "model": WOLF},
            {"model": WOLF},
            {"key": 7, "model": WOLF},
            {"key": "con", "model": WOLF},
            {"key": "Good", "model": BOAR},
            {"key": "both", "model": WOLF, "template": WOLF_TEMPLATE},
            {"key": "neither"},
            {"key": "notaguid", "model": "wolf"},
            {"key": "zero", "model": WOLF, "frames": 0},
            {"key": "many", "model": WOLF, "frames": 65},
            {"key": "tiny", "model": WOLF, "size": 7},
            {"key": "huge", "model": WOLF, "size": 513},
            {"key": "text", "model": WOLF, "frames": "24"},
            {"key": "missing", "model": MISSING},
            "not an object"
        ]);
        let run = run_fake(&cache, items, 1, None);
        assert_eq!(run.reply.code, ITEMS_FAILED, "{}", run.reply.message);
        let items = &run.reply.items;
        assert_eq!(items.len(), 16);
        assert_eq!(items[0].status, Status::Drawn);
        let expect = [
            (1, "key"), (2, "key"), (3, "key"), (4, "key"), (5, "key"), (6, "model"), (7, "model"),
            (8, "GUID"), (9, "frames"), (10, "frames"), (11, "size"), (12, "size"), (13, "frames"),
            (14, MISSING), (15, "object"),
        ];
        for (i, word) in expect {
            assert_eq!(items[i].status, Status::Failed, "item {i}");
            let error = items[i].error.as_deref().unwrap_or_default();
            assert!(error.contains(word), "item {i}: {error}");
        }
        assert_eq!(items[1].key.as_deref(), Some("a b"), "the key as given");
        assert_eq!(items[2].key, None);
        assert!(items[5].error.as_deref().unwrap().contains("good"), "the duplicate names the earlier key");
        let folders: Vec<String> = std::fs::read_dir(&cache.0).unwrap().flatten()
            .map(|e| e.file_name().to_string_lossy().into_owned()).collect();
        assert_eq!(folders, ["good"], "nothing is written for a bad item");
    }

    #[test]
    fn a_failed_item_keeps_its_earlier_frames() {
        let cache = TempDir::new("keep");
        let items = json!([{"key": "wolf", "model": WOLF, "frames": 2, "size": 16},
                           {"key": "boar", "model": BOAR, "frames": 2, "size": 16}]);
        run_fake(&cache, items.clone(), 1, None);
        let run = run_fake(&cache, items, 2, Some(WOLF));
        assert_eq!(run.reply.code, ITEMS_FAILED);
        assert_eq!(run.reply.items[0].status, Status::Failed);
        assert_eq!(run.reply.items[1].status, Status::Drawn);
        assert_eq!(frame_files(&cache.0.join("wolf")), ["00.png", "01.png"]);
    }

    #[test]
    fn an_archive_that_cannot_be_opened_fails_every_item() {
        let cache = TempDir::new("noarchive");
        let request: Request = serde_json::from_value(json!({
            "game_dir": "C:\\Nowhere", "cache_dir": cache.utf8(),
            "items": [{"key": "wolf", "model": WOLF}, {"key": "bad key", "model": WOLF}]
        }))
        .unwrap();
        let reply = run(request, "enshrouded", |_: &Path, _: &str| -> Result<Fake, String> {
            Err("No enshrouded.kfc in C:\\Nowhere".into())
        });
        assert_eq!(reply.code, RUN_FAILED);
        assert!(reply.message.contains("C:\\Nowhere"), "{}", reply.message);
        assert!(reply.items[0].error.as_deref().unwrap().contains("enshrouded.kfc"));
        assert!(reply.items[1].error.as_deref().unwrap().contains("key"));
    }

    #[test]
    fn the_request_needs_a_game_and_an_absolute_cache_folder() {
        for (request, word) in [
            (json!({"cache_dir": "C:\\cache", "items": []}), "game_dir"),
            (json!({"game_dir": "C:\\Game", "items": []}), "cache_dir"),
            (json!({"game_dir": "C:\\Game", "cache_dir": "emf\\cache", "items": []}), "cache_dir"),
        ] {
            let request: Request = serde_json::from_value(request).unwrap();
            let reply = run(request, "enshrouded", |_: &Path, _: &str| -> Result<Fake, String> { unreachable!() });
            assert_eq!(reply.code, BAD_REQUEST);
            assert!(reply.message.contains(word), "{}", reply.message);
        }
    }

    #[test]
    fn the_reply_is_json_with_the_items() {
        let cache = TempDir::new("json");
        let run = run_fake(&cache, json!([{"key": "wolf", "model": WOLF, "frames": 2, "size": 16}, {"key": "?"}]), 1, None);
        let reply: Value = serde_json::from_str(&run.reply.to_json()).unwrap();
        assert_eq!(reply["code"], ITEMS_FAILED);
        assert_eq!(reply["items"][0]["status"], "drawn");
        assert_eq!(reply["items"][0]["frames"], 2);
        assert_eq!(reply["items"][1]["status"], "failed");
        assert!(reply["items"][0].get("error").is_none());
    }
}
