param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath($Output)
if (!(Test-Path -LiteralPath $directory -PathType Container)) { throw 'Fixture directory must already exist' }
for ($item=Get-Item -LiteralPath $directory; $item; $item=$item.Parent) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Fixture directory cannot traverse a link' }
}
# Native vclip v1 contains ordinary bitmap streams; the bitmap loader accepts
# uncompressed 24-bit TGA streams as well as the compressed OGF encoding.
function Write-LogoBitmap([IO.BinaryWriter]$writer,[int]$phase) {
    $header=New-Object byte[] 18
    $header[2]=2; $header[12]=64; $header[14]=64; $header[16]=24; $header[17]=32
    $writer.Write($header)
    for ($y=0;$y -lt 64;$y++) { for ($x=0;$x -lt 64;$x++) {
        $edge=$x -lt 4 -or $x -ge 60 -or $y -lt 4 -or $y -ge 60 -or [Math]::Abs($x-$y) -lt 3
        $rgb=if ($edge) { @(255,255,255) } elseif ($phase -eq 0) { @(255,48,0) } else { @(0,160,255) }
        $writer.Write([byte]$rgb[2]); $writer.Write([byte]$rgb[1]); $writer.Write([byte]$rgb[0])
    } }
}
foreach ($animated in @($false,$true)) {
    $name=if ($animated) {'renderer-logo-animated.oaf'} else {'renderer-logo-static.ogf'}
    $path=Join-Path $directory $name
    $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    $writer=[IO.BinaryWriter]::new($stream)
    try {
        if ($animated) { $writer.Write([byte]127); $writer.Write([byte]1); $writer.Write([byte]2); $writer.Write([single]0.125) }
        Write-LogoBitmap $writer 0
        if ($animated) { Write-LogoBitmap $writer 1 }
    } finally { $writer.Dispose() }
    Get-FileHash -LiteralPath $path | Select-Object Path,Hash
}
