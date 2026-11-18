param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('flight','inspection','menu','options','reload','mission','level2','overscan','overscan-reference','bloom','bloom-reference')]
    [string[]]$Cases=@('menu','mission','flight','options','reload')
)
# Use a private, seeded runtime. Screenshot automation supplies inputs but
# does not opt into scene inspection, except for the explicit image control.
$ErrorActionPreference='Stop'
$Runtime=(Resolve-Path -LiteralPath $Runtime).Path
$exe=Join-Path $Runtime 'Descent 3MASTERED.exe'
foreach($file in @($exe,(Join-Path $Runtime 'Jeff.plt'),(Join-Path $Runtime 'savegame/saveg003'),(Join-Path $Runtime 'savegame/saveg007'))) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) {throw "Missing private runtime input: $file"}
}
if(Test-Path -LiteralPath $Output) {throw 'Choose a new evidence directory'}
$Output=(New-Item -ItemType Directory -Path $Output).FullName
$previous=$env:DESCENT3MASTERED_CAPTURE_LOG
try {
    foreach($case in $Cases) {
        $shot=Join-Path $Output "$case.png"
        $log=Join-Path $Output "$case.log"
        $env:DESCENT3MASTERED_CAPTURE_LOG=$log
        $flags=@('-background','-windowed','-nosound','-nomusic','-setdir',$Runtime,'-pilot','Jeff','-limitframe','0')
        if ($case -eq 'menu') { $flags+=@('-capture-main-menu-output',$shot) }
        else {
            if ($case -in @('mission','level2')) {
                $flags+=@('-loadmission','d3.mn3')
                if ($case -eq 'level2') {$flags+=@('-missionlevel','2')}
            }
            else {$flags+=@('-loadgame',(Join-Path $Runtime 'savegame/saveg003'))}
            $flags+=@('-capture-frame','120','-capture-dt','0.016666667','-capture-seed','12345','-capture-headlight',
                '-capture-heading','-capture-heading-until','12','-capture-fire-primary','-capture-fire-until','18','-capture-output',$shot,'-perpixellighting')
            if ($case -like 'overscan*' -or $case -like 'bloom*') {
                $flags+=@('-render-resolution','2560','1440','-ao','-ssaa','2','-msaa','4','-nomotionblur')
                if ($case -like 'bloom*') {$flags+='-bloom'} else {$flags+='-nobloom'}
                if ($case -like '*reference') {$flags+='-scene-overscan-color-reference'}
            } else {$flags+=@('-render-resolution','1280','720','-noao','-nobloom','-nomotionblur','-msaa','0','-ssaa','1')}
            if ($case -eq 'inspection') {$flags+='-scene-inspection'}
            if ($case -eq 'options') {$flags+=@('-capture-options-after','20','-capture-options-output',(Join-Path $Output 'options-ui.png'))}
            if ($case -eq 'reload') {$flags+=@('-capture-reload-frame','40','-capture-reloadgame',(Join-Path $Runtime 'savegame/saveg007'))}
        }
        if ($flags | Where-Object {$_.Length -ge 99 -or $_ -like '*"*'}) {throw 'Use short paths without embedded quotes for game arguments'}
        $p=Start-Process -FilePath $exe -ArgumentList ($flags | ForEach-Object {'"'+$_+'"'}) -WorkingDirectory $Runtime -WindowStyle Hidden -PassThru
        try {
            if (!$p.WaitForExit(90000)) {$p.Kill(); $p.WaitForExit(); throw "$case timed out; see $log"}
            $messages=Get-Content -LiteralPath $log -Raw
            if($p.ExitCode -ne 1 -or !(Test-Path -LiteralPath $shot) -or !$messages.Contains('winmain application run returned')) {throw "$case failed (exit $($p.ExitCode)); see $log"}
            if($case -eq 'options' -and !(Test-Path -LiteralPath (Join-Path $Output 'options-ui.png'))) {throw 'Options did not render'}
            if($case -eq 'reload' -and !$messages.Contains('scheduled reload frame=40')) {throw 'Reload did not run'}
            Write-Output "$case passed: $shot"
        } finally {$p.Dispose()}
    }
} finally {$env:DESCENT3MASTERED_CAPTURE_LOG=$previous}
