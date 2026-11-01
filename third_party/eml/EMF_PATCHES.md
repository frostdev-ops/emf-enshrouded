# EML in EMF: the pinned copy and its patches

This folder is a copy of part of EML, the Enshrouded Mod Loader.

- Upstream: https://github.com/Brabb3l/kfc-parser
- Commit: `16708594d686b030d909f2d835b199d879ea9ad8` (short `1670859`), "eml: bump version to 0.2.0", 2026-10-16
  (EML v0.2.0).
- Licence: EML's own, GPL-3.0-or-later, in `LICENSE` here. EML's `README.md` and Cargo metadata are kept as they are.

## Why EMF carries it

EMF builds `emf_content.dll` from these crates and its own small shim in `src/content/`. The DLL builds the game
content of EML mods (the same work `emm.exe run --patch` does), so EML mods work without a separate EML install.

## What was copied, and what was left out

Copied, unchanged except for the patches below:

- `crates/kfc`, `crates/kfc-base`, `crates/kfc-content`, `crates/kfc-resource`
- `crates/mod-loader`, `crates/mod-loader-base`, `crates/mod-loader-lua`, `crates/mod-loader-runtime`
- `Cargo.toml`, `Cargo.lock`, `LICENSE`, `README.md`

Left out: the programs and the rest of the repository (`crates/mod-manager-cli`, `crates/mod-manager-gui`,
`crates/dinput8-proxy`, `crates/dbghelp-proxy`, `crates/cli-legacy`, `docs/`, `examples/`, `.github/`).
`Cargo.lock` is upstream's and still names the crates that were left out; EMF builds with its own lock file in
`src/content/`.

## Patches

Each patch is its own commit, and each changes as little as it can.

1. **P1** `Cargo.toml`: the workspace `members` list names only the eight copied crates.
2. **P2** `crates/mod-loader-base/Cargo.toml`: `zip` without its `bzip2` feature (the other default features stay).
   bzip2-sys defines `bz_internal_error` as an exported function, and a Rust DLL exports every such function of its
   dependencies, so `emf_content.dll` had a third export. Cost: a zipped mod whose files are compressed with bzip2
   cannot be read (zip tools use deflate by default; folders and deflate zips are not affected).
3. **P3** `Cargo.toml`: the workspace's `image` without its `rayon` and `exr` features (the other default formats
   stay). Decoding a compressed EXR builds a rayon thread pool, and rayon starts worker threads for AVIF encoding;
   those threads outlive the call, and unloading the DLL would pull the code out from under them. Cost: a mod's
   `image.decode`/`image.encode` with `"openexr"` fails, and AVIF encoding runs on one thread.

## How to update

1. Clone upstream into an empty folder outside the repository and check out the new commit.
2. Replace the copied paths above with the new commit's files (delete the old ones first, so removed files go).
3. Re-apply the patches, one commit each, and update the commit and date at the top of this file.
4. Start `src/content/Cargo.lock` again from the new `Cargo.lock`, so the crates EMF shares with EML keep upstream's
   versions; then build and run the tests.
