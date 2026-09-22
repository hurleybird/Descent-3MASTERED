param(
    [ValidatePattern('^saveg00[0-7]$')][string]$Save = 'saveg003',
    [string]$Runtime = 'C:\tmp\D3RendererVerification\scene-test',
    [switch]$Capture
)
$ErrorActionPreference = 'Stop'
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
if ($runtimePath -ne 'C:\tmp\D3RendererVerification\scene-test') {
    throw 'This launcher only operates on the isolated scene-test installation.'
}
$seeds = 'C:\tmp\D3RendererVerification\seeds'
$savePath = Join-Path $runtimePath "savegame\$Save"
Copy-Item -LiteralPath (Join-Path $seeds 'Jeff.plt') -Destination (Join-Path $runtimePath 'Jeff.plt')
Copy-Item -LiteralPath (Join-Path $seeds "savegame\$Save") -Destination $savePath
Copy-Item -LiteralPath (Join-Path $seeds "savegame\$Save.cnt") -Destination "$savePath.cnt"
$arguments = @('-windowed','-setdir',('"{0}"' -f $runtimePath),'-pilot','Jeff',
    '-loadgame',('"{0}"' -f $savePath),'-scene-inspection','-render-resolution','1280','720',
    '-perpixellighting','-noao','-nobloom','-nomotionblur','-msaa','0','-ssaa','1','-nosound','-nomusic')
if ($Capture) {
    $png = Join-Path $runtimePath ('inspection-{0:yyyyMMdd-HHmmss}.png' -f (Get-Date))
    $arguments += @('-background','-capture-frame','60','-capture-dt','0.016666667',
        '-capture-seed','12345','-capture-headlight','-capture-output',('"{0}"' -f $png))
}
Write-Host 'ROOM GEOMETRY TEST ONLY. Objects/terrain/effects/lighting migration is unfinished.'
Write-Host 'Normal flight controls; Escape for menus. Do not use this build for gameplay.'
Write-Host 'Only copied save and pilot files are used. Candidate is not modified.'
$start = @{FilePath=(Join-Path $runtimePath 'Descent 3MASTERED.exe'); ArgumentList=$arguments; WorkingDirectory=$runtimePath; PassThru=$true}
if ($Capture) { $start.WindowStyle = 'Hidden' }
# Without -Capture, the explicitly requested test is interactive and visible.
$process = Start-Process @start
Write-Host "Started inspection process $($process.Id) from $Save."
