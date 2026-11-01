# Tests content_shadow.ps1 against fake game folders in %TEMP% (never the real game): -Make hard-links the .dat files
# (a change to the game's file shows in the shadow), copies the .kfc and .kfc_resources (a change to the copy leaves the
# game's file alone), records what it made; -Remove takes out exactly that, keeps files it did not make and the game's
# own files; the refusals (the game runs, no content files, a shadow already there, a folder it did not make, a tampered
# record). Folder names with brackets check that paths are taken literally. Every folder it makes is removed at the end,
# also when a step fails. Exit code 0 = all passed.
$ErrorActionPreference = "Stop"
$script:fails = 0
$script:made = New-Object System.Collections.ArrayList
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
    foreach ($d in $script:made) { Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue }
}
if ($script:fails) { "$($script:fails) check(s) failed"; exit 1 }
"all passed"
exit 0
