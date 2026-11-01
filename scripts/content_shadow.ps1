# Builds and removes the shadow copy of the game's content files for the loader's redirect test ([content]
# redirect_test=1 in emf\emf_core.ini; docs/engine_parts/file_redirect_spike.md).
#   content_shadow.ps1 -Make [-Game <folder>] [-Copy]   makes <game>\emf\content\client\ with a hard link to every
#                                                        enshrouded_NNN.dat (no extra space: the same file under a
#                                                        second name; -Copy copies them instead, about 55 GB) and real
#                                                        copies of enshrouded.kfc and enshrouded.kfc_resources (so a
#                                                        patcher can change them later). shadow.txt there records each
#                                                        file and folder it made, and last the game's files it was made
#                                                        from (the loader uses the copy only while they still match).
#   content_shadow.ps1 -Remove [-Game <folder>]          takes out exactly what shadow.txt records; a file it did not
#                                                        make stays (and is named).
# Without -Game: EMF_GAME_DIR, else the Steam library that has the game (scripts\find_game.ps1). The game must be closed.
# It never changes, moves or deletes the game's own files: a hard link is removed as a link, the game's file stays, and
# no file's attributes are ever changed (a hard link shares them with the game's file).
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
# the file system's own path (not a PowerShell drive's): the .NET calls below need it
$Game = (Resolve-Path -LiteralPath (Resolve-GameDir $Game)).ProviderPath.TrimEnd("\")
if (Get-Process $ProcessName -ErrorAction SilentlyContinue) { throw "the game is running; close it first" }
$shadow = Join-Path $Game "emf\content\client"
$record = Join-Path $shadow "shadow.txt"
$contentName = '^enshrouded(\.kfc|\.kfc_resources|_[0-9]+\.dat)$'
# the game's files the record stamps (the loader compares them at each start: src/loader/file_redirect.h)
$stamped = @("enshrouded.exe", "enshrouded.kfc", "enshrouded.kfc_resources")

if (-not ('EmfShadow.Native' -as [type])) {
    Add-Type -Namespace EmfShadow -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool CreateHardLink(string link, string existing, IntPtr security);
'@
}

# A junction or symbolic link at emf, emf\content or emf\content\client would make -Make write and -Remove delete in
# another place (the game folder itself, say): both refuse then.
foreach ($rel in "emf", "emf\content", "emf\content\client") {
    $p = Join-Path $Game $rel
    if ((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "$p is a link to another place (a junction or symbolic link); content_shadow.ps1 does not work through one, so it changes nothing"
    }
}

if ($Make) {
    foreach ($f in $stamped) {
        if (-not (Test-Path -LiteralPath (Join-Path $Game $f) -PathType Leaf)) { throw "no $f in $Game (is it the game folder?)" }
    }
    $dats = @(Get-ChildItem -LiteralPath $Game -File | Where-Object { $_.Name -match '^enshrouded_[0-9]+\.dat$' } | Sort-Object Name)
    if ($dats.Count -eq 0) { throw "no enshrouded_NNN.dat in $Game (is it the game folder?)" }
    if (Test-Path -LiteralPath $shadow) { throw "$shadow is already there; run content_shadow.ps1 -Remove first" }
    # the game's files as they are now, before anything is copied (written into the record last); a file that changes
    # while the copy is made then no longer matches, and the loader does not use the copy
    $stampLines = @()
    foreach ($n in $stamped) {
        $item = Get-Item -LiteralPath (Join-Path $Game $n) -Force
        $item.Refresh()
        $stampLines += "source $n $($item.Length) $($item.LastWriteTimeUtc.ToFileTimeUtc())"
    }
    $stampLines += "dat_count $($dats.Count)"
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
    # each file is recorded before it is made: a stop half-way (even inside a copy) leaves a record -Remove cleans up
    # with (it removes only the listed files that are there)
    $linked = 0
    $copied = 0
    foreach ($f in $copies) {
        [System.IO.File]::AppendAllText($record, "copy $($f.Name)`r`n")
        [System.IO.File]::Copy($f.FullName, (Join-Path $shadow $f.Name))
        $copied++
    }
    if (-not $Copy) {
        foreach ($f in $dats) {
            [System.IO.File]::AppendAllText($record, "link $($f.Name)`r`n")
            if (-not [EmfShadow.Native]::CreateHardLink((Join-Path $shadow $f.Name), $f.FullName, [IntPtr]::Zero)) {
                $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
                $text = (New-Object System.ComponentModel.Win32Exception $err).Message
                throw ("could not make a hard link for $($f.Name) (Windows error ${err}: $text). Hard links need an NTFS " +
                    "drive. content_shadow.ps1 -Remove takes out what was made; -Make -Copy copies the files instead (about " +
                    ("{0:N0} GB)" -f ((($dats | Measure-Object -Property Length -Sum).Sum) / 1GB)))
            }
            $linked++
        }
    }
    # last: the game's files it was made from. Without these lines (a -Make that stopped) the loader does not use it.
    [System.IO.File]::AppendAllText($record, ("# the game's files when this copy was made: name, size in bytes, last write " +
        "time (UTC, a Windows file time); the loader uses the copy only while they match`r`n" + ($stampLines -join "`r`n") + "`r`n"))
    $mb = [math]::Round($need / 1MB)
    "made ${shadow}: $linked hard links, $copied copies ($mb MB copied)"
    "next: set redirect_test=1 under [content] in $Game\emf\emf_core.ini; take it out again with content_shadow.ps1 -Remove"
    "after a game update: content_shadow.ps1 -Remove, then -Make again (until then the loader does not use the copy)"
}

if ($Remove) {
    if (-not (Test-Path -LiteralPath $shadow)) { "nothing to remove: $shadow is not there"; return }
    if (-not (Test-Path -LiteralPath $record -PathType Leaf)) {
        throw "$shadow has no shadow.txt, so content_shadow.ps1 did not make it; left alone"
    }
    $folders = @()
    $removed = 0
    $stuck = @()
    foreach ($line in [System.IO.File]::ReadAllLines($record)) {
        if ($line -match '^(link|copy) (.+)$') {
            $kind = $Matches[1]
            $name = $Matches[2].Trim()
            if ($name -notmatch $contentName) { "skipped `"$name`" in shadow.txt: not a content file name"; continue }
            $path = Join-Path $shadow $name
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                try {
                    [System.IO.File]::Delete($path)  # a hard link: only this name goes, the game's file stays
                    $removed++
                } catch {
                    $why = $_.Exception.Message
                    if (-not (Get-Item -LiteralPath $path -Force).IsReadOnly) {
                        $stuck += "could not remove $name ($why)"
                    } elseif ($kind -eq "link") {
                        $stuck += ("$name is read-only. A hard link is the game's own file under a second name, so the mark " +
                            "is on the game's $name as well, and clearing it here would change the game's file too: " +
                            "content_shadow.ps1 does not do that. If that is fine with you, clear the mark on the game's " +
                            "file, then run content_shadow.ps1 -Remove again")
                    } else {
                        $stuck += "$name is read-only (a copy, not the game's file): clear the mark on it, then run content_shadow.ps1 -Remove again"
                    }
                }
            }
        } elseif ($line -match '^folder (emf|emf\\content|emf\\content\\client)$') {
            $folders += $Matches[1]
        }
    }
    if ($stuck.Count -gt 0) {
        # the record stays, so that another -Remove can finish
        throw ("removed $removed files, but not all of them from ${shadow}: " + ($stuck -join "; ") + ". shadow.txt is kept for that.")
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
