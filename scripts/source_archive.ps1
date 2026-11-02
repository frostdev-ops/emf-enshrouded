# Builds EMF-<version>-source.zip for a GitHub release: the repository at one commit (git archive: every tracked file,
# third_party\eml with EML's licence among them) plus the sources of every Rust library emf_content.dll is built from
# (cargo vendor, into src\content\vendor), with src\content\.cargo\config.toml set to build from them without the
# network. emf_content.dll is GPL-3.0-or-later (THIRD_PARTY_NOTICES.md), and the GPL asks for its complete source.
#   source_archive.ps1 [-Ref v0.4.0] [-Out build\package]
# -Ref: the commit, tag or branch (default HEAD; at a release, the release's tag). The version comes from that commit's
# VERSION file. cargo vendor works offline from the crates a build already fetched (cargo build first).
# Exit: 0 written; a failure throws.
param(
    [string]$Ref = "HEAD",
    [string]$Out = (Join-Path $PSScriptRoot "..\build\package")
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

function Invoke-Native([string]$What, [scriptblock]$Run) {
    & $Run
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
}

# --quiet: no message on stderr, which Windows PowerShell would turn into its own error before this one
$prev = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$commit = (& git -C $repo rev-parse --verify --quiet "$Ref^{commit}")
$code = $LASTEXITCODE
$ErrorActionPreference = $prev
if ($code -ne 0 -or -not $commit) { throw "-Ref: no commit '$Ref' in $repo" }
$commit = "$commit".Trim()
$version = "$(& git -C $repo show "${commit}:VERSION" | Select-Object -First 1)".Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "VERSION at $Ref must be three numbers x.y.z, not '$version'" }

New-Item -ItemType Directory -Force $Out | Out-Null
$outDir = (Resolve-Path -LiteralPath $Out).Path
$name = "EMF-$version-source"
$zip = Join-Path $outDir "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("emf_source_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    # 1. the tracked files at the commit
    $tar = Join-Path $work "repo.tar"
    Invoke-Native "git archive" { & git -C $repo archive --format=tar "--prefix=$name/" -o $tar $commit }
    Invoke-Native "tar" { & "$env:SystemRoot\System32\tar.exe" -xf $tar -C $work }
    Remove-Item -LiteralPath $tar -Force
    $root = Join-Path $work $name
    $content = Join-Path $root "src\content"
    if (-not (Test-Path -LiteralPath (Join-Path $content "Cargo.lock"))) { throw "$Ref has no src\content\Cargo.lock (no emf_content.dll at that commit)" }

    # 2. the Rust libraries, from that commit's Cargo.lock (cargo runs in the archive's src\content, so its
    # rust-toolchain.toml picks the toolchain)
    # cargo reports each crate on stderr: kept quiet, shown only when it fails (Continue: Windows PowerShell would stop
    # at the first stderr line otherwise)
    Push-Location -LiteralPath $content
    $ErrorActionPreference = "Continue"
    try {
        $said = & cargo vendor --locked --offline --versioned-dirs vendor 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = "Stop"
        Pop-Location
    }
    if ($code -ne 0) { $said | ForEach-Object { "$_" }; throw "cargo vendor failed (exit $code; it needs the crates a build fetched)" }
    $config = Join-Path $content ".cargo\config.toml"
    $vendored = "`n# The source archive: build from the vendored sources in vendor\ (cargo vendor), without the network.`n" +
                "[source.crates-io]`nreplace-with = `"vendored-sources`"`n`n[source.vendored-sources]`ndirectory = `"vendor`"`n"
    [System.IO.File]::AppendAllText($config, $vendored)

    # 3. what this is
    $note = @(
        "EMF $version, source archive",
        "",
        "The EMF repository (https://github.com/matenm/emf-enshrouded) at commit $commit, with the sources of every Rust",
        "library that emf_content.dll is built from in src\content\vendor\ (cargo vendor). src\content\.cargo\config.toml",
        "points cargo at them, so the DLL builds without the network.",
        "",
        "emf_content.dll is built from the Enshrouded Mod Loader (third_party\eml, GPL-3.0-or-later: third_party\eml\LICENSE)",
        "and EMF's shim (src\content); as a whole it is under the GPL-3.0-or-later. The rest of EMF is MIT (LICENSE).",
        "Third-party code and its licences: THIRD_PARTY_NOTICES.md.",
        "",
        "Build: see README.md (Building). emf_content.dll alone: cargo build --release --locked, in src\content."
    ) -join "`r`n"
    [System.IO.File]::WriteAllText((Join-Path $root "SOURCE.txt"), $note + "`r`n")

    # 4. the zip, entry by entry with '/' (Windows PowerShell's own zip writers put '\' in the names)
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stream = [System.IO.File]::Open($zip, [System.IO.FileMode]::CreateNew)
    $archive = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    $count = 0
    try {
        foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File -Force) {
            $entry = $name + "/" + $f.FullName.Substring($root.Length + 1).Replace("\", "/")
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $entry,
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
            $count++
        }
    } finally {
        $archive.Dispose()
        $stream.Dispose()
    }
    $mb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
    "source archive: $zip ($count files, $mb MB; commit $commit)"
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
