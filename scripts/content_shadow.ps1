# Builds and removes the shadow copy of the game's content files for the loader's redirect test ([content]
# redirect_test=1 in emf\emf_core.ini; docs/engine_parts/file_redirect_spike.md).
#   content_shadow.ps1 -Make [-Game <folder>] [-Copy]   makes <game>\emf\content\client\ with a hard link to every
#                                                        enshrouded_NNN.dat (no extra space: the same file under a
#                                                        second name; -Copy copies them instead, about 55 GB) and real
#                                                        copies of enshrouded.kfc and enshrouded.kfc_resources (so a
#                                                        patcher can change them later). shadow.txt there records each
#                                                        file and folder it made.
#   content_shadow.ps1 -Remove [-Game <folder>]          takes out exactly what shadow.txt records; a file it did not
#                                                        make stays (and is named).
# Without -Game: EMF_GAME_DIR, else the Steam library that has the game (scripts\find_game.ps1). The game must be closed.
# It never changes, moves or deletes the game's own files: a hard link is removed as a link, the game's file stays.
param(
    [switch]$Make,
    [switch]$Remove,
    [switch]$Copy,
    [string]$Game = "",
    [string]$ProcessName = "enshrouded"  # the game's process (tests pass another name)
)
$ErrorActionPreference = "Stop"
if ($Make -eq $Remove) { throw "use content_shadow.ps1 -Make [-Game <folder>] [-Copy] or content_shadow.ps1 -Remove [-Game <folder>]" }
. (Join-Path $PSScriptRoot "find_game.ps1")
$Game = (Resolve-Path -LiteralPath (Resolve-GameDir $Game)).Path.TrimEnd("\")
if (Get-Process $ProcessName -ErrorAction SilentlyContinue) { throw "the game is running; close it first" }
$shadow = Join-Path $Game "emf\content\client"
$record = Join-Path $shadow "shadow.txt"
$contentName = '^enshrouded(\.kfc|\.kfc_resources|_\d+\.dat)$'

if (-not ('EmfShadow.Native' -as [type])) {
    Add-Type -Namespace EmfShadow -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool CreateHardLink(string link, string existing, IntPtr security);
'@
}

if ($Make) {
    foreach ($f in "enshrouded.kfc", "enshrouded.kfc_resources") {
        if (-not (Test-Path -LiteralPath (Join-Path $Game $f) -PathType Leaf)) { throw "no $f in $Game (is it the game folder?)" }
    }
    $dats = @(Get-ChildItem -LiteralPath $Game -File | Where-Object { $_.Name -match '^enshrouded_\d+\.dat$' } | Sort-Object Name)
    if ($dats.Count -eq 0) { throw "no enshrouded_NNN.dat in $Game (is it the game folder?)" }
    if (Test-Path -LiteralPath $shadow) { throw "$shadow is already there; run content_shadow.ps1 -Remove first" }
    $copies = @("enshrouded.kfc", "enshrouded.kfc_resources") | ForEach-Object { Get-Item -LiteralPath (Join-Path $Game $_) }
    if ($Copy) { $copies += $dats }
    $need = ($copies | Measure-Object -Property Length -Sum).Sum
    $free = [long]::MaxValue
    try { $free = (New-Object System.IO.DriveInfo ([System.IO.Path]::GetPathRoot($Game))).AvailableFreeSpace } catch { }  # a network share: not checked
    if ($need + 1GB -gt $free) {
        throw ("the copies need {0:N1} GB and the drive has {1:N1} GB free" -f ($need / 1GB), ($free / 1GB))
    }
    if (Test-Path -LiteralPath (Join-Path $Game "enshrouded.kfc.bak")) {
        "note: enshrouded.kfc.bak is there, so an EML patch may be applied: the shadow gets the patched files"
    }
    # the folders it makes, outermost first (recorded so that -Remove takes out only those)
    $newFolders = @()
    foreach ($rel in "emf", "emf\content", "emf\content\client") {
        if (-not (Test-Path -LiteralPath (Join-Path $Game $rel))) { $newFolders += $rel }
    }
    [System.IO.Directory]::CreateDirectory($shadow) | Out-Null
    $stamp = [datetime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ", [System.Globalization.CultureInfo]::InvariantCulture)
    $lines = @("# made by content_shadow.ps1 at $stamp; content_shadow.ps1 -Remove takes out exactly what is listed here")
    $lines += $newFolders | ForEach-Object { "folder $_" }
    [System.IO.File]::WriteAllLines($record, [string[]]$lines)
    # each file is recorded as soon as it is made: a stop half-way leaves a record -Remove can clean up
    $linked = 0
    $copied = 0
    foreach ($f in $copies) {
        [System.IO.File]::Copy($f.FullName, (Join-Path $shadow $f.Name))
        [System.IO.File]::AppendAllText($record, "copy $($f.Name)`r`n")
        $copied++
    }
    if (-not $Copy) {
        foreach ($f in $dats) {
            if (-not [EmfShadow.Native]::CreateHardLink((Join-Path $shadow $f.Name), $f.FullName, [IntPtr]::Zero)) {
                $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw ("could not make a hard link for $($f.Name) (Windows error $err): hard links need an NTFS drive. " +
                    "content_shadow.ps1 -Remove takes out what was made; -Make -Copy copies the files instead (about " +
                    ("{0:N0} GB)" -f ((($dats | Measure-Object -Property Length -Sum).Sum) / 1GB)))
            }
            [System.IO.File]::AppendAllText($record, "link $($f.Name)`r`n")
            $linked++
        }
    }
    $mb = [math]::Round($need / 1MB)
    "made ${shadow}: $linked hard links, $copied copies ($mb MB copied)"
    "next: set redirect_test=1 under [content] in $Game\emf\emf_core.ini; take it out again with content_shadow.ps1 -Remove"
}

if ($Remove) {
    if (-not (Test-Path -LiteralPath $shadow)) { "nothing to remove: $shadow is not there"; return }
    if (-not (Test-Path -LiteralPath $record -PathType Leaf)) {
        throw "$shadow has no shadow.txt, so content_shadow.ps1 did not make it; left alone"
    }
    $folders = @()
    $removed = 0
    foreach ($line in [System.IO.File]::ReadAllLines($record)) {
        if ($line -match '^(link|copy) (.+)$') {
            $name = $Matches[2].Trim()
            if ($name -notmatch $contentName) { "skipped `"$name`" in shadow.txt: not a content file name"; continue }
            $path = Join-Path $shadow $name
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                [System.IO.File]::Delete($path)  # a hard link: only this name goes, the game's file stays
                $removed++
            }
        } elseif ($line -match '^folder (emf|emf\\content|emf\\content\\client)$') {
            $folders += $Matches[1]
        }
    }
    [System.IO.File]::Delete($record)
    $left = @(Get-ChildItem -LiteralPath $shadow -Force | ForEach-Object { $_.Name })
    if ($left.Count -gt 0) {
        "removed $removed files; kept what content_shadow.ps1 did not make: $($left -join ', ') (in $shadow)"
    } else {
        "removed $removed files and $shadow"
    }
    # the folders -Make made, innermost first, when they are empty
    foreach ($rel in "emf\content\client", "emf\content", "emf") {
        $dir = Join-Path $Game $rel
        if ($rel -eq "emf\content\client" -or $folders -contains $rel) {
            if ((Test-Path -LiteralPath $dir) -and @(Get-ChildItem -LiteralPath $dir -Force).Count -eq 0) {
                [System.IO.Directory]::Delete($dir)
            }
        }
    }
    "next: set redirect_test=0 (or remove it) under [content] in $Game\emf\emf_core.ini"
}
