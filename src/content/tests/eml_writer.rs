//! EML's container writer (third_party/eml, kfc-base) as emf_content.dll uses it: it must never write through a hard
//! link. EMF's shadow folder links the game's own .dat files; a .dat the writer makes new at a linked name has to
//! become a file of its own (patch P4), so the game's file keeps its bytes.

use std::path::{Path, PathBuf};

use kfc_base::container::{KFCFile, KFCWriteOptions, KFCWriter};
use kfc_base::guid::ContentHash;
use kfc_base::reflection::TypeRegistry;

struct TempDir(PathBuf);

impl TempDir {
    fn new(name: &str) -> Self {
        let unique = format!(
            "emf_content_{name}_{}_{}",
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
        );
        let path = std::env::temp_dir().join(unique);
        std::fs::create_dir_all(&path).unwrap();
        TempDir(path)
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn content(writer: &mut KFCWriter<KFCFile, TypeRegistry>, data: &[u8]) {
    writer.write_content(&ContentHash::from_data(data), data).unwrap();
}

/// A game file in `game`, hard-linked into the shadow as `shadow\name`.
fn linked_game_file(game: &Path, shadow: &Path, name: &str) -> PathBuf {
    let original = game.join(name);
    std::fs::write(&original, b"GAME DATA").unwrap();
    std::fs::hard_link(&original, shadow.join(name)).unwrap();
    original
}

#[test]
fn new_containers_at_linked_names_leave_the_linked_file_alone() {
    let root = TempDir::new("writer_links");
    let (game, shadow) = (root.0.join("game"), root.0.join("shadow"));
    std::fs::create_dir_all(&game).unwrap();
    std::fs::create_dir_all(&shadow).unwrap();

    // A base .kfc with one container (t_000.dat), written from nothing.
    std::fs::write(shadow.join("t.kfc"), b"").unwrap();
    std::fs::write(shadow.join("t.kfc_resources"), b"").unwrap();
    let mut base = KFCWriter::<KFCFile, TypeRegistry>::new(&shadow, "t", TypeRegistry::default(), "test").unwrap();
    content(&mut base, b"base content");
    base.finalize().unwrap();
    let reference = KFCFile::from_path(shadow.join("t.kfc"), false).unwrap();
    assert_eq!(reference.containers().len(), 1);

    // The names the incremental writer makes next: t_001.dat and t_002.dat (one content each), and t_003.dat
    // (the padding to four containers). Each is a link to a game file.
    let linked: Vec<PathBuf> =
        ["t_001.dat", "t_002.dat", "t_003.dat"].iter().map(|n| linked_game_file(&game, &shadow, n)).collect();

    let options = KFCWriteOptions {
        overwrite_containers: true, // as EML's mod loader opens it (mod-loader-lua load.rs create_writer)
        truncate_containers: true,  // so the padding container is made again too
        max_container_size: 1,      // one content per container
        ..Default::default()
    };
    let mut writer =
        KFCWriter::new_incremental_with_options(&shadow, "t", reference, TypeRegistry::default(), options).unwrap();
    content(&mut writer, b"first new content");
    content(&mut writer, b"second new content");
    writer.finalize().unwrap();

    for original in &linked {
        assert_eq!(std::fs::read(original).unwrap(), b"GAME DATA", "{} was written through a link", original.display());
    }
    assert!(std::fs::read(shadow.join("t_001.dat")).unwrap().starts_with(b"first new content"));
    assert!(std::fs::read(shadow.join("t_002.dat")).unwrap().starts_with(b"second new content"));
    assert_eq!(std::fs::metadata(shadow.join("t_003.dat")).unwrap().len(), 0);
}
