# Prebuilt EML content packs (the creature packs for Beastmaster and Necromancer, built outside the repository) as one
# optional download next to EMF's own zips:
#   package_packs.ps1 -Packs <pack folder>[,<pack folder>...] -Version <x.y.z> [-Out build\package]
# Each pack is named by its own folder, with mod.json and src\mod.lua (never a folder of packs: it would ship every test
# pack in it). The zip EMF-creature-packs-<version>.zip holds mods\<pack>\..., the layout of EML's mods folder and the one
# EMF.exe's "Install a downloaded file" installs as EML mods, and README.txt: what is in it and how to install it.
param(
    [Parameter(Mandatory = $true)][string[]]$Packs,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Out = ""
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if (-not $Out) { $Out = Join-Path $repo "build\package" }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "-Version must be three numbers x.y.z (for example 1.0.0), not '$Version'" }

$found = @()
foreach ($pack in @($Packs | ForEach-Object { $_ -split "," })) {  # powershell -File passes a list as one string
    if (-not $pack) { continue }
    if (-not (Test-Path -LiteralPath $pack -PathType Container)) { throw "-Packs: there is no folder $pack" }
    $p = Get-Item -LiteralPath $pack
    if (-not (Test-Path -LiteralPath (Join-Path $p.FullName "mod.json"))) {
        throw "-Packs: $($p.FullName) has no mod.json (not an EML content pack: name the pack's own folder)"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $p.FullName "src\mod.lua"))) {
        throw "-Packs: $($p.Name) has no src\mod.lua (EML and EMF.exe would not see it as a pack)"
    }
    $found += $p
}
if (-not $found) { throw "-Packs: no pack named" }

$name = "EMF-creature-packs-$Version"
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("emf_packs_" + [guid]::NewGuid().ToString("N"))
$root = Join-Path $stage $name
New-Item -ItemType Directory -Force -Path (Join-Path $root "mods") | Out-Null
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$zip = Join-Path $Out "$name.zip"
try {
    $lines = @("EMF creature packs $Version", "", "New creatures for EMF's Beastmaster and Necromancer mods. They are EML content packs:",
               "EMF (0.4.0 and newer) builds them itself at the game's start; EML (Enshrouded Mod Loader) or ShroudForge work too.",
               "Without a pack the mods still work; the creatures it adds are refused with a note.", "", "In this download:")
    foreach ($p in $found) {
        Copy-Item -LiteralPath $p.FullName -Destination (Join-Path $root "mods\$($p.Name)") -Recurse
        $m = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $p.FullName "mod.json") | ConvertFrom-Json
        $title = if ($m.name) { $m.name } else { $p.Name }
        $ver = if ($m.version) { " $($m.version)" } else { "" }
        $lines += "- $title$ver (mods\$($p.Name))"
        if ($m.description) { $lines += "  $($m.description)" }
    }
    $lines += @("", "Install:",
                "- With EMF.exe: Mods tab, ""Install a downloaded file"", pick this zip. It puts each pack in the mods folder",
                "  next to the game.",
                "- By hand: copy the folders inside mods\ into the mods folder next to enshrouded.exe (not into emf\mods,",
                "  which is for EMF mods), and start the game: EMF builds them. On Linux or a Steam Deck this is the way.",
                "Every player in a world that uses these creatures needs the same packs, and the host needs Beastmaster or",
                "Necromancer switched on in EMF.", "",
                "Uninstall: delete the pack folders from the mods folder (or uninstall each one in EMF.exe).", "",
                "Licence and credits:",
                "- The creature models and textures in these packs are by EMF contributors, made with Meshy (meshy.ai),",
                "  and are licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/): you may share and adapt",
                "  them, also in your own mods, with the credit ""EMF contributors, made with Meshy"".",
                "- The packs contain no game files: they reuse the skeletons and animations of your installed game.")
    [System.IO.File]::WriteAllText((Join-Path $root "README.txt"), ($lines -join "`r`n") + "`r`n")
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }

    # '/' in entry names (see package.ps1): written one by one
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stream = [System.IO.File]::Open($zip, [System.IO.FileMode]::CreateNew)
    $archive = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File) {
            $entry = $name + "/" + $f.FullName.Substring($root.Length + 1).Replace("\", "/")
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $entry,
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally {
        $archive.Dispose()
        $stream.Dispose()
    }
    $mb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
    "package_packs: $zip ($($found.Count) pack(s), $mb MB)"
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
