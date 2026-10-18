# Helpers for working on the TouchPad over novacom. Dot-source this file:
#     . .\tools\tp-tools.ps1
$script:Nov = "C:\Program Files\Palm, Inc\novacom.exe"

# Run a shell script on the TouchPad. Going through a file avoids PowerShell's quoting problems.
function Invoke-Tp([string]$Script) {
    $f = Join-Path $env:TEMP "tp-cmd.sh"
    [IO.File]::WriteAllText($f, ($Script -replace "`r`n", "`n") + "`n", (New-Object Text.UTF8Encoding $false))
    cmd /c "`"$script:Nov`" -d usb put file:///tmp/tp-cmd.sh < `"$f`"" | Out-Null
    & $script:Nov -d usb run file:///bin/sh /tmp/tp-cmd.sh 2>&1
}

# Copy a file from the TouchPad to this PC, or from this PC to the TouchPad.
function Get-TpFile([string]$Remote, [string]$Local) {
    cmd /c "`"$script:Nov`" -d usb get file://$Remote > `"$Local`""
}
function Send-TpFile([string]$Local, [string]$Remote) {
    cmd /c "`"$script:Nov`" -d usb put file://$Remote < `"$Local`""
}

# Take a screenshot of the TouchPad. The image comes out in the panel's own orientation.
function Get-TpShot([string]$Out) {
    Invoke-Tp "luna-send -n 1 palm://com.palm.systemmanager/takeScreenShot '{`"file`":`"/media/internal/.cr-shot.png`"}' >/dev/null 2>&1" | Out-Null
    Get-TpFile "/media/internal/.cr-shot.png" $Out
}

# Close the app, start it once with launch parameters, wait, print what the app and
# its service logged, and take a screenshot. One cold start: never send a second
# launch into an app that is still starting.
#     Show-Cr shot.png @{target = "file:///media/internal/comics/a.cbz"}
#     Show-Cr shot.png @{side = "server"}
function Show-Cr([string]$Shot, [hashtable]$Params = @{}, [int]$Seconds = 8) {
    $launch = @{ id = "org.webos.comicreader"; params = $Params } | ConvertTo-Json -Compress -Depth 4
    # The parameters travel in a file, so nothing in them meets a shell.
    $f = Join-Path $env:TEMP "cr-launch.json"
    [IO.File]::WriteAllText($f, $launch, (New-Object Text.UTF8Encoding $false))
    Send-TpFile $f "/tmp/cr-launch.json"
    $env:JAVA_HOME = "G:\Java\jdk17"
    $env:Path = "G:\Java\jdk17\bin;G:\webOS\PalmSDK\Current\bin;" + $env:Path
    cmd /c "palm-launch.bat -d usb -c org.webos.comicreader 2>&1" | Out-Null
    $lines = @(
        'sleep 3',
        'wc -l < /var/log/messages > /tmp/cr-mark',
        'luna-send -n 1 palm://com.palm.applicationManager/launch "$(cat /tmp/cr-launch.json)" </dev/null >/dev/null 2>&1',
        'rm -f /tmp/cr-launch.json',
        "sleep $Seconds",
        'tail -n +$(cat /tmp/cr-mark) /var/log/messages > /tmp/cr-new.log',
        'grep -E "\[comics\]|\[crsvc\]|Uncaught|comicreader.*[Ee]rror|kgsl" /tmp/cr-new.log | sed -e "s/^[^ ]* \[[0-9]*\] [^ ]* //" | cut -c1-300 | tail -n 20'
    )
    Invoke-Tp ($lines -join "`n")
    if ($Shot) { Get-TpShot $Shot }
}
