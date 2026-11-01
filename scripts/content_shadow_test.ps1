# Tests content_shadow.ps1 against fake game folders in %TEMP% (never the real game): -Make hard-links the .dat files
# (a change to the game's file shows in the shadow), copies the .kfc and .kfc_resources (a change to the copy leaves the
# game's file alone), records what it made and, last, the game's files it was made from (the loader's staleness check);
# -Remove takes out exactly that, keeps files it did not make and the game's own files; a hard link that cannot be made,
# a read-only container, junctions in the shadow's path, a game folder given through a PowerShell drive; the refusals
# (the game runs, no content files, a shadow already there, a folder it did not make, a tampered record). Folder names
# with brackets check that paths are taken literally. Every folder it makes is removed at the end, also when a step
# fails. Exit code 0 = all passed.
$ErrorActionPreference = "Stop"
$script:fails = 0
$script:made = New-Object System.Collections.ArrayList
$script:junctions = New-Object System.Collections.ArrayList  # removed (as links only) before the folders
if (-not ('EmfShadowTest.Native' -as [type])) {
    Add-Type -Namespace EmfShadowTest -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool CreateHardLink(string link, string existing, IntPtr security);
'@
}
function Check([bool]$cond, [string]$what) {
    if ($cond) { "ok   $what" } else { "FAIL $what"; $script:fails++ }
}
function New-TempDir([string]$name) {
    $d = Join-Path ([System.IO.Path]::GetTempPath()) ("emf test " + $name + " " + [guid]::NewGuid().ToString("N").Substring(0, 8))
    New-Item -ItemType Directory -Force $d | Out-Null
    [void]$script:made.Add($d)
    return $d
}
function Write-File([string]$path, [string]$text) {
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($path)) | Out-Null
    [System.IO.File]::WriteAllText($path, $text)
}
function Read-File([string]$path) { [System.IO.File]::ReadAllText($path) }
function Exists([string]$path) { Test-Path -LiteralPath $path }
# a fake game folder: the content files, the exe, an EMF install
function New-FakeGame([string]$tag) {
    $g = New-TempDir "shadow game [$tag]"
    Write-File (Join-Path $g "enshrouded.exe") "fake exe"
    Write-File (Join-Path $g "enshrouded.kfc") "kfc vanilla"
    Write-File (Join-Path $g "enshrouded.kfc_resources") "resources vanilla"
    Write-File (Join-Path $g "enshrouded_000.dat") "container 0"
    Write-File (Join-Path $g "enshrouded_001.dat") "container 1"
    Write-File (Join-Path $g "steam_api64.dll") "steam"
    Write-File (Join-Path $g "emf\emf_core.ini") "[content]`r`nredirect_test=0`r`n"
    return $g
}
$shadowScript = Join-Path $PSScriptRoot "content_shadow.ps1"
$noGame = "emf-test-no-such-process"
$savedGameDir = $env:EMF_GAME_DIR
try {
    "== content_shadow.ps1 -Make / -Remove (fake game) =="
    $g = New-FakeGame "a"
    $shadow = Join-Path $g "emf\content\client"
    $out = (& $shadowScript -Make -Game $g -ProcessName $noGame) -join "`n"
    Check ((Exists (Join-Path $shadow "enshrouded.kfc")) -and (Exists (Join-Path $shadow "enshrouded.kfc_resources")) -and
        (Exists (Join-Path $shadow "enshrouded_000.dat")) -and (Exists (Join-Path $shadow "enshrouded_001.dat"))) "-Make puts the four content files into emf\content\client"
    Check (-not (Exists (Join-Path $shadow "enshrouded.exe")) -and -not (Exists (Join-Path $shadow "steam_api64.dll"))) "-Make takes only the content files"
    Check ($out -match "2 hard links" -and $out -match "2 copies") "-Make says what it made ($($out -replace "`n", ' | '))"
    # a hard link is the game's own file under a second name: a change to one shows in the other
    [System.IO.File]::AppendAllText((Join-Path $g "enshrouded_000.dat"), " grown")
    Check ((Read-File (Join-Path $shadow "enshrouded_000.dat")) -eq "container 0 grown") "the shadow's .dat is a hard link to the game's"
    # a copy is separate: a patcher may change it, the game's file stays
    Write-File (Join-Path $shadow "enshrouded.kfc") "kfc patched"
    Check ((Read-File (Join-Path $g "enshrouded.kfc")) -eq "kfc vanilla") "the shadow's .kfc is a copy: changing it leaves the game's alone"
    $record = Read-File (Join-Path $shadow "shadow.txt")
    Check ($record -match "(?m)^link enshrouded_000\.dat\r?$" -and $record -match "(?m)^copy enshrouded\.kfc\r?$") "shadow.txt records each file and how it was made"
    # the game's files as they were (the loader compares them with the game's files at each start)
    $stamps = @()
    foreach ($n in "enshrouded.exe", "enshrouded.kfc", "enshrouded.kfc_resources") {
        $f = Get-Item -LiteralPath (Join-Path $g $n)
        $want = "source $n $($f.Length) $($f.LastWriteTimeUtc.ToFileTimeUtc())"
        if ($record -notmatch ("(?m)^" + [regex]::Escape($want) + "\r?$")) { $stamps += "missing: $want" }
    }
    Check ($stamps.Count -eq 0) "shadow.txt records the game's enshrouded.exe, .kfc and .kfc_resources: size and last write time, UTC ($($stamps -join '; '))"
    Check ($record -match "(?m)^dat_count 2\r?$") "shadow.txt records how many enshrouded_NNN.dat files the game has"
    $recLines = @($record -split "`r?`n")
    $lastFile = -1
    $firstSource = -1
    for ($i = 0; $i -lt $recLines.Count; $i++) {
        if ($recLines[$i] -match '^(link|copy) ') { $lastFile = $i }
        if ($recLines[$i] -match '^(source|dat_count) ' -and $firstSource -lt 0) { $firstSource = $i }
    }
    Check ($firstSource -gt $lastFile -and $lastFile -ge 0) "the game's files are recorded last, after every file -Make made (a -Make that stops half-way leaves no stamps)"

    $msg = ""
    try { & $shadowScript -Make -Game $g -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "-Remove" -and (Read-File (Join-Path $shadow "enshrouded.kfc")) -eq "kfc patched") "a second -Make refuses and says to run -Remove first ($msg)"

    # a file the shadow got from somewhere else (a patcher's new container) stays
    Write-File (Join-Path $shadow "enshrouded_055.dat") "from a patcher"
    $out = (& $shadowScript -Remove -Game $g -ProcessName $noGame) -join "`n"
    Check (-not (Exists (Join-Path $shadow "enshrouded.kfc")) -and -not (Exists (Join-Path $shadow "enshrouded_000.dat")) -and
        -not (Exists (Join-Path $shadow "shadow.txt"))) "-Remove takes out the files it made and its record"
    Check ((Exists (Join-Path $shadow "enshrouded_055.dat")) -and $out -match "enshrouded_055\.dat") "-Remove keeps a file it did not make and names it ($($out -replace "`n", ' | '))"
    Check ((Read-File (Join-Path $g "enshrouded_000.dat")) -eq "container 0 grown" -and (Read-File (Join-Path $g "enshrouded.kfc")) -eq "kfc vanilla" -and
        (Read-File (Join-Path $g "enshrouded_001.dat")) -eq "container 1" -and (Read-File (Join-Path $g "enshrouded.kfc_resources")) -eq "resources vanilla") "the game's own files are all still there, unchanged"
    Remove-Item -LiteralPath (Join-Path $shadow "enshrouded_055.dat")
    Remove-Item -LiteralPath $shadow
    Remove-Item -LiteralPath (Join-Path $g "emf\content")

    # made and removed again: the folders it made go too, EMF's own folder stays
    & $shadowScript -Make -Game $g -ProcessName $noGame | Out-Null
    $out = (& $shadowScript -Remove -Game $g -ProcessName $noGame) -join "`n"
    Check (-not (Exists (Join-Path $g "emf\content")) -and (Exists (Join-Path $g "emf\emf_core.ini"))) "-Remove takes out the folders -Make made and leaves EMF's folder"
    $out = (& $shadowScript -Remove -Game $g -ProcessName $noGame) -join "`n"
    Check ($out -match "nothing to remove") "-Remove with no shadow says there is nothing to remove ($out)"

    "== -Copy, and a game folder without EMF =="
    $c = New-FakeGame "copy"
    Remove-Item -LiteralPath (Join-Path $c "emf") -Recurse
    $out = (& $shadowScript -Make -Copy -Game $c -ProcessName $noGame) -join "`n"
    $cs = Join-Path $c "emf\content\client"
    [System.IO.File]::AppendAllText((Join-Path $c "enshrouded_001.dat"), " grown")
    Check ((Read-File (Join-Path $cs "enshrouded_001.dat")) -eq "container 1" -and $out -match "4 copies") "-Copy copies the .dat files instead of linking them ($($out -replace "`n", ' | '))"
    & $shadowScript -Remove -Game $c -ProcessName $noGame | Out-Null
    Check (-not (Exists (Join-Path $c "emf")) -and (Exists (Join-Path $c "enshrouded_001.dat"))) "-Remove also takes out the emf folder when -Make made it"

    "== refusals =="
    $r = New-FakeGame "refuse"
    $rs = Join-Path $r "emf\content\client"
    $msg = ""
    try { & $shadowScript -Make -Game $r -ProcessName ([System.Diagnostics.Process]::GetCurrentProcess().ProcessName) | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "running" -and -not (Exists $rs)) "-Make refuses while the game runs ($msg)"
    $msg = ""
    try { & $shadowScript -Make -Game $r -ProcessName $noGame | Out-Null; & $shadowScript -Remove -Game $r -ProcessName ([System.Diagnostics.Process]::GetCurrentProcess().ProcessName) | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "running" -and (Exists (Join-Path $rs "shadow.txt"))) "-Remove refuses while the game runs ($msg)"
    & $shadowScript -Remove -Game $r -ProcessName $noGame | Out-Null

    $empty = New-TempDir "shadow empty"
    Write-File (Join-Path $empty "enshrouded.exe") "fake exe"
    $msg = ""
    try { & $shadowScript -Make -Game $empty -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "enshrouded\.kfc" -and -not (Exists (Join-Path $empty "emf"))) "-Make refuses a folder without the content files and makes nothing ($msg)"

    Write-File (Join-Path $rs "enshrouded.kfc") "someone else's"
    $msg = ""
    try { & $shadowScript -Remove -Game $r -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "shadow\.txt" -and (Exists (Join-Path $rs "enshrouded.kfc"))) "-Remove leaves a shadow folder it did not make alone ($msg)"
    Remove-Item -LiteralPath (Join-Path $r "emf\content") -Recurse

    # a record changed by hand cannot make -Remove delete anything outside the shadow folder
    & $shadowScript -Make -Game $r -ProcessName $noGame | Out-Null
    Add-Content -LiteralPath (Join-Path $rs "shadow.txt") "link ..\..\..\enshrouded.kfc"
    Add-Content -LiteralPath (Join-Path $rs "shadow.txt") "copy C:\Windows\win.ini"
    $out = (& $shadowScript -Remove -Game $r -ProcessName $noGame) -join "`n"
    Check ((Exists (Join-Path $r "enshrouded.kfc")) -and $out -match "skipped") "-Remove skips a recorded name that is not a content file name ($($out -replace "`n", ' | '))"

    $msg = ""
    try { & $shadowScript -Game $r -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "-Make" -and $msg -match "-Remove") "without -Make or -Remove it says how to use it ($msg)"

    "== a hard link that cannot be made =="
    $h = New-FakeGame "link fails"
    # NTFS gives a file at most 1024 names: give enshrouded_001.dat all of them, so -Make's hard link for it fails
    $extra = Join-Path $h "more names"
    New-Item -ItemType Directory $extra | Out-Null
    $names = 0
    while ($names -lt 1100 -and [EmfShadowTest.Native]::CreateHardLink((Join-Path $extra "n$names"), (Join-Path $h "enshrouded_001.dat"), [IntPtr]::Zero)) { $names++ }
    $hs = Join-Path $h "emf\content\client"
    $msg = ""
    try { & $shadowScript -Make -Game $h -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($names -ge 1000 -and $msg -match "hard link for enshrouded_001\.dat" -and $msg -match "-Remove" -and $msg -match "-Copy") "-Make stops when a hard link cannot be made and says what to do ($names names; $msg)"
    $rec = Read-File (Join-Path $hs "shadow.txt")
    Check ($rec -match "(?m)^link enshrouded_001\.dat\r?$" -and $rec -notmatch "(?m)^source ") "each file is recorded before it is made, and the unfinished record has no stamps of the game's files"
    $out = (& $shadowScript -Remove -Game $h -ProcessName $noGame) -join "`n"
    Check (-not (Exists (Join-Path $h "emf\content")) -and (Read-File (Join-Path $h "enshrouded_001.dat")) -eq "container 1" -and
        (Read-File (Join-Path $h "enshrouded_000.dat")) -eq "container 0") "-Remove cleans up after the stopped -Make; the game's files stay ($($out -replace "`n", ' | '))"

    "== a read-only container =="
    $ro = New-FakeGame "read-only"
    $ros = Join-Path $ro "emf\content\client"
    & $shadowScript -Make -Game $ro -ProcessName $noGame | Out-Null
    # a hard link and the game's file are one file: the read-only mark is on both
    (Get-Item -LiteralPath (Join-Path $ro "enshrouded_000.dat")).IsReadOnly = $true
    $msg = ""
    try { & $shadowScript -Remove -Game $ro -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "enshrouded_000\.dat" -and $msg -match "read-only" -and $msg -match "game's file too") "-Remove says a read-only hard link cannot go without changing the game's file ($msg)"
    Check ((Get-Item -LiteralPath (Join-Path $ro "enshrouded_000.dat")).IsReadOnly -and (Exists (Join-Path $ros "enshrouded_000.dat")) -and
        (Exists (Join-Path $ros "shadow.txt")) -and -not (Exists (Join-Path $ros "enshrouded.kfc"))) "it leaves the game's file read-only, removes the rest and keeps shadow.txt for another -Remove"
    (Get-Item -LiteralPath (Join-Path $ro "enshrouded_000.dat")).IsReadOnly = $false
    & $shadowScript -Remove -Game $ro -ProcessName $noGame | Out-Null
    Check (-not (Exists (Join-Path $ro "emf\content")) -and (Exists (Join-Path $ro "enshrouded_000.dat"))) "with the mark taken off, -Remove finishes"

    "== junctions in the shadow's path =="
    $j = New-FakeGame "junction"
    $elsewhere = New-TempDir "shadow elsewhere"
    $jc = Join-Path $j "emf\content"
    New-Item -ItemType Junction -Path $jc -Target $elsewhere | Out-Null
    [void]$script:junctions.Add($jc)
    $msg = ""
    try { & $shadowScript -Make -Game $j -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "emf\\content" -and $msg -match "link" -and @(Get-ChildItem -LiteralPath $elsewhere -Force).Count -eq 0) "-Make refuses when emf\content is a junction and writes nothing through it ($msg)"
    Write-File (Join-Path $elsewhere "client\enshrouded.kfc") "someone else's"
    Write-File (Join-Path $elsewhere "client\shadow.txt") "copy enshrouded.kfc`r`n"
    $msg = ""
    try { & $shadowScript -Remove -Game $j -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "link" -and (Exists (Join-Path $elsewhere "client\enshrouded.kfc"))) "-Remove refuses too and deletes nothing through it ($msg)"
    [System.IO.Directory]::Delete($jc)  # the junction only
    $script:junctions.Remove($jc)
    $je = Join-Path $j "emf"
    Remove-Item -LiteralPath $je -Recurse -Force
    New-Item -ItemType Junction -Path $je -Target $elsewhere | Out-Null
    [void]$script:junctions.Add($je)
    $msg = ""
    try { & $shadowScript -Make -Game $j -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
    Check ($msg -match "link" -and -not (Exists (Join-Path $elsewhere "content"))) "-Make refuses when emf itself is a junction ($msg)"
    [System.IO.Directory]::Delete($je)
    $script:junctions.Remove($je)

    "== a game folder given through a PowerShell drive =="
    $p = New-FakeGame "psdrive"
    New-PSDrive -Name EmfShadowTest -PSProvider FileSystem -Root $p | Out-Null
    try {
        $msg = ""
        try { & $shadowScript -Make -Game "EmfShadowTest:\" -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
        Check (Exists (Join-Path $p "emf\content\client\enshrouded.kfc")) "-Make works with -Game on a PowerShell drive: the folder's real path is used ($msg)"
        $msg = ""
        try { & $shadowScript -Remove -Game "EmfShadowTest:\" -ProcessName $noGame | Out-Null } catch { $msg = $_.Exception.Message }
        Check (-not (Exists (Join-Path $p "emf\content"))) "-Remove works there too ($msg)"
    } finally {
        Remove-PSDrive -Name EmfShadowTest
    }

    "== the game folder without -Game: EMF_GAME_DIR =="
    $e = New-FakeGame "env"
    $env:EMF_GAME_DIR = $e
    & $shadowScript -Make -ProcessName $noGame | Out-Null
    Check (Exists (Join-Path $e "emf\content\client\enshrouded.kfc")) "-Make without -Game uses EMF_GAME_DIR"
    & $shadowScript -Remove -ProcessName $noGame | Out-Null
    Check (-not (Exists (Join-Path $e "emf\content"))) "-Remove without -Game uses EMF_GAME_DIR"
} catch {
    "FAIL a step threw: $($_.Exception.Message) ($($_.InvocationInfo.PositionMessage))"
    $script:fails++
} finally {
    $env:EMF_GAME_DIR = $savedGameDir
    # a junction goes as a link first: Windows PowerShell's Remove-Item -Recurse could follow it into its target
    foreach ($jn in $script:junctions) { try { [System.IO.Directory]::Delete($jn) } catch { } }
    foreach ($d in $script:made) { Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue }
}
if ($script:fails) { "$($script:fails) check(s) failed"; exit 1 }
"all passed"
exit 0
