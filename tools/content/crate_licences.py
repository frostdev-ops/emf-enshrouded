#!/usr/bin/env python3
r"""The Rust libraries inside emf_content.dll and their licences, as one HTML page (licenses\rust-crates.html in the
release zip, named in THIRD_PARTY_NOTICES.md).

    python -I tools\content\crate_licences.py --manifest src\content\Cargo.toml --out <page.html>

It asks cargo for the locked dependency graph of the Windows target (`cargo metadata --locked --offline
--filter-platform x86_64-pc-windows-msvc`, so it needs the sources a build already fetched, not the network) and lists
every crates.io package the DLL links through normal dependencies: name, version, licence expression, repository and
the licence files the package ships (LICENSE*, LICENCE*, COPYING*, NOTICE*, UNLICENSE*, or the file its manifest
names). Path packages (EML's crates in third_party/eml, under EML's GPL text, and EMF's own shim) and dev or build
dependencies (never in the DLL) are left out.

Python 3, standard library only. Exit codes: 0 written, 1 cargo failed, 2 a wrong command line.
"""

import argparse
import html
import json
import os
import subprocess
import sys

TARGET = "x86_64-pc-windows-msvc"
LICENCE_PREFIXES = ("license", "licence", "copying", "notice", "unlicense")


def licence_files(folder, named=None):
    """The licence files in a package's folder, sorted, as (file name, text)."""
    names = set()
    if named:
        names.add(named)
    try:
        for entry in os.listdir(folder):
            if entry.lower().startswith(LICENCE_PREFIXES) and os.path.isfile(os.path.join(folder, entry)):
                names.add(entry)
    except OSError:
        return []
    out = []
    for name in sorted(names):
        try:
            with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as f:
                out.append((name.replace("\\", "/"), f.read()))
        except OSError:
            pass
    return out


def collect(metadata):
    """The crates.io packages the root links through normal dependencies, sorted by name and version. A build
    dependency named <name>-src is one too: such a crate compiles a C library into the crate that uses it (lua-src:
    EML's Lua 5.4)."""
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    seen, todo = set(), [metadata["resolve"]["root"]]
    while todo:
        pid = todo.pop()
        if pid in seen:
            continue
        seen.add(pid)
        for d in nodes.get(pid, {}).get("deps", []):
            kinds = [k.get("kind") for k in d.get("dep_kinds", [])]
            normal = None in kinds  # linked (not dev, not a build tool)
            c_source = "build" in kinds and packages.get(d["pkg"], {}).get("name", "").endswith("-src")
            if normal or c_source:
                todo.append(d["pkg"])
    out = []
    for pid in seen:
        p = packages[pid]
        if p.get("source") is None:  # a path package: EML's crates, EMF's shim
            continue
        folder = os.path.dirname(p["manifest_path"])
        named = p.get("license_file")
        out.append({
            "name": p["name"], "version": p["version"],
            "license": p.get("license") or (f"see {named}" if named else "not stated"),
            "repository": p.get("repository") or "",
            "files": licence_files(folder, named),
        })
    return sorted(out, key=lambda p: (p["name"], p["version"]))


def render(packages):
    e = html.escape
    rows = []
    for p in packages:
        repo = f' &middot; <a href="{e(p["repository"])}">{e(p["repository"])}</a>' if p["repository"] else ""
        body = [f'<h2 id="{e(p["name"])}-{e(p["version"])}">{e(p["name"])} {e(p["version"])}</h2>',
                f"<p>Licence: <b>{e(p['license'])}</b>{repo}</p>"]
        if p["files"]:
            for fname, text in p["files"]:
                body.append(f"<details><summary>{e(fname)}</summary><pre>{e(text)}</pre></details>")
        else:
            body.append("<p>The crate ships no licence file; its licence expression above applies.</p>")
        rows.append("\n".join(body))
    return ("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<title>Rust libraries in emf_content.dll</title>"
            "<style>body{font-family:sans-serif;max-width:60em;margin:2em auto;padding:0 1em}"
            "pre{white-space:pre-wrap;background:#f4f4f4;padding:.5em}</style></head><body>\n"
            "<h1>Rust libraries in emf_content.dll</h1>\n"
            "<p>emf_content.dll is built from the Enshrouded Mod Loader (EML, GPL-3.0-or-later: EML-LICENSE.txt) and "
            f"links these {len(packages)} libraries from crates.io, each under its own licence.</p>\n"
            + "\n".join(rows) + "\n</body></html>\n")


def cargo_metadata(manifest):
    folder = os.path.dirname(os.path.abspath(manifest))  # rust-toolchain.toml there picks the pinned toolchain
    r = subprocess.run(["cargo", "metadata", "--format-version", "1", "--locked", "--offline",
                        "--filter-platform", TARGET, "--manifest-path", os.path.abspath(manifest)],
                       cwd=folder, capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        return None
    return json.loads(r.stdout)


def main(argv=None):
    ap = argparse.ArgumentParser(description="the Rust libraries inside emf_content.dll and their licences")
    ap.add_argument("--manifest", required=True, help="src/content/Cargo.toml")
    ap.add_argument("--out", required=True, help="the HTML page to write")
    args = ap.parse_args(argv)
    meta = cargo_metadata(args.manifest)
    if meta is None:
        print("crate_licences: cargo metadata failed", file=sys.stderr)
        return 1
    packages = collect(meta)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(render(packages))
    print(f"crate_licences: {len(packages)} libraries -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
