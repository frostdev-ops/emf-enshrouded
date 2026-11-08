// A zipped EML mod (a .zip in the mods folder) as EML's registry opens it: its files listed, found and read by path.
// Covers EMF patch P5 (zip 9 decodes each file name, and the decoding can fail).
use std::io::{Read, Write};

use mod_loader_base::FileSystem;

fn make_zip(path: &std::path::Path) {
    let file = std::fs::File::create(path).unwrap();
    let mut zip = zip::ZipWriter::new(file);
    let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    zip.start_file("mod.json", options).unwrap();
    zip.write_all(br#"{"id":"zipped","name":"Zipped","version":"1.0.0"}"#).unwrap();
    zip.add_directory("src/", options).unwrap();
    zip.start_file("src/mod.lua", options).unwrap();
    zip.write_all(b"-- a zipped mod\n").unwrap();
    zip.finish().unwrap();
}

#[test]
fn a_zipped_mod_lists_finds_and_reads_its_files() {
    let dir = std::env::temp_dir().join(format!("emf_zip_mod_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("zipped.zip");
    make_zip(&path);
    {
        let mut fs = FileSystem::new_zip(path.to_str().unwrap()).unwrap();
        let mut names: Vec<String> = fs.files().into_iter().map(|name| name.unwrap().to_string()).collect();
        names.sort();
        assert_eq!(names, vec!["mod.json", "src/", "src/mod.lua"]);
        assert!(fs.is_file("mod.json"));
        assert!(fs.exists("src/mod.lua"));
        assert!(!fs.exists("src/other.lua"));
        let mut text = String::new();
        fs.read_file("src/mod.lua").unwrap().read_to_string(&mut text).unwrap();
        assert_eq!(text, "-- a zipped mod\n");
    }
    std::fs::remove_dir_all(&dir).unwrap();
}
