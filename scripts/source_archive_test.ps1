# Tests scripts\source_archive.ps1 on this repository's HEAD: the zip's name and layout, EML's licence and the
# vendored Rust libraries in it, and that cargo resolves emf_content's whole dependency graph from the archive alone
# (an empty CARGO_HOME, offline). Needs cargo and the crates a build fetched. Exit 0 when every check passes.
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$script:failed = 0
function Check([bool]$ok, [string]$what) {
    if ($ok) { "ok   $what" } else { "FAIL $what"; $script:failed++ }
}
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("emf_source_test_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    "== a ref that is not there =="
    $msg = ""
    try { & (Join-Path $PSScriptRoot "source_archive.ps1") -Ref "no-such-ref-here" -Out $tmp | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "no commit 'no-such-ref-here'") "source_archive.ps1 refuses a ref that does not exist ($msg)"

    "== the archive of HEAD =="
    $ver = (Get-Content -LiteralPath (Join-Path $repo "VERSION") -TotalCount 1).Trim()
    $head = (& git -C $repo rev-parse HEAD).Trim()
    & (Join-Path $PSScriptRoot "source_archive.ps1") -Out $tmp | Out-Host
    $zip = Join-Path $tmp "EMF-$ver-source.zip"
    Check (Test-Path -LiteralPath $zip) "it writes EMF-$ver-source.zip (named after VERSION)"
    if (Test-Path -LiteralPath $zip) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $z = [System.IO.Compression.ZipFile]::OpenRead($zip)
        $names = @($z.Entries | ForEach-Object { $_.FullName })
        $z.Dispose()
        $p = "EMF-$ver-source"
        $missing = @(@("SOURCE.txt", "LICENSE", "THIRD_PARTY_NOTICES.md", "README.md", "third_party/eml/LICENSE",
                       "third_party/eml/EMF_PATCHES.md", "src/content/Cargo.lock", "src/content/Cargo.toml",
                       "src/content/emf_content.rc", "src/content/.cargo/config.toml") | Where-Object { $names -notcontains "$p/$_" })
        Check ($missing.Count -eq 0) "it has the repository's files, EML's licence and emf_content's sources (missing: $($missing -join ', '))"
        Check (@($names | Where-Object { $_ -match "^$p/src/content/vendor/serde-1\.[^/]+/Cargo\.toml$" }).Count -eq 1) "the Rust libraries are vendored in src/content/vendor (serde among them)"
        Check (@($names | Where-Object { $_.Contains("\") }).Count -eq 0) "entry names use '/'"
        Check (@($names | Where-Object { $_ -match "/(build|target)/" -and $_ -notmatch "/vendor/" }).Count -eq 0) "no build output is in it"

        $x = Join-Path $tmp "x"
        New-Item -ItemType Directory -Force $x | Out-Null
        & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $x
        $root = Join-Path $x $p
        $note = Get-Content -LiteralPath (Join-Path $root "SOURCE.txt") -Raw
        Check (($note -match [regex]::Escape($head)) -and ($note -match "GPL-3\.0-or-later")) "SOURCE.txt names the commit and the GPL"
        $cfg = Get-Content -LiteralPath (Join-Path $root "src\content\.cargo\config.toml") -Raw
        Check (($cfg -match 'replace-with = "vendored-sources"') -and ($cfg -match 'directory = "vendor"') -and ($cfg -match "crt-static")) "src\content\.cargo\config.toml keeps the static CRT and builds from vendor\"

        # cargo resolves the whole graph from the archive alone: an empty CARGO_HOME has no registry cache
        $emptyHome = Join-Path $tmp "cargo_home"
        New-Item -ItemType Directory -Force $emptyHome | Out-Null
        $savedHome = $env:CARGO_HOME
        $env:CARGO_HOME = $emptyHome
        Push-Location -LiteralPath (Join-Path $root "src\content")
        try {
            $null = & cargo metadata --format-version 1 --locked --offline 2>&1
            $code = $LASTEXITCODE
        } finally {
            Pop-Location
            $env:CARGO_HOME = $savedHome
        }
        Check ($code -eq 0) "cargo resolves emf_content's dependencies from the archive alone (empty CARGO_HOME, offline; exit $code)"
    }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:failed) { "source_archive: $($script:failed) check(s) failed"; exit 1 }
"source_archive: all checks passed"
