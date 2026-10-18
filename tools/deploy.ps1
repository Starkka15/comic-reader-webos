# Package, install and relaunch the app on the connected TouchPad.
# Needs the Palm SDK and a Java runtime; set the two paths below for your machine.
$env:JAVA_HOME = "G:\Java\jdk17"
$env:Path = "G:\Java\jdk17\bin;G:\webOS\PalmSDK\Current\bin;" + $env:Path
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root "build"
New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $build
# An old package must never be installed in place of one that failed to build.
Remove-Item "org.webos.comicreader_*_all.ipk" -ErrorAction SilentlyContinue
# App, service and package description go into one .ipk.
cmd /c "palm-package.bat `"$root\app`" `"$root\service`" `"$root\package`" 2>&1" | Select-Object -Last 1
$ipk = Get-ChildItem "org.webos.comicreader_*_all.ipk" | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $ipk) {
    Pop-Location
    Write-Output "RESULT: FAILED (no package was built)"
    exit 1
}
cmd /c "palm-install.bat -d usb $($ipk.Name) 2>&1" | Select-Object -Last 1
cmd /c "palm-launch.bat -d usb -c org.webos.comicreader 2>&1" | Out-Null
# Installing does not restart a service that is running; stop it so the new code is used.
. (Join-Path $PSScriptRoot "tp-tools.ps1")
Invoke-Tp 'for p in $(ps aux | grep "org.webos.comicreader.service.js" | grep -v grep | awk "{print \$2}"); do kill $p; done; sync' | Out-Null
cmd /c "palm-launch.bat -d usb org.webos.comicreader 2>&1" | Select-Object -Last 1
Pop-Location
