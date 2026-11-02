// Links emf_content.dll's version resource (emf_content.rc, compiled by CMake with EMF's version) into the DLL. CMake
// passes the compiled .res in EMF_CONTENT_RES; a plain `cargo build` or `cargo test` has none and builds without it.
fn main() {
    println!("cargo:rerun-if-env-changed=EMF_CONTENT_RES");
    if let Ok(res) = std::env::var("EMF_CONTENT_RES") {
        if !res.is_empty() {
            println!("cargo:rerun-if-changed={res}");
            println!("cargo:rustc-link-arg-cdylib={res}");
        }
    }
}
