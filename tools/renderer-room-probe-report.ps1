param(
    [Parameter(Mandatory=$true)][string]$Tag,
    [string]$Root='C:\tmp\D3RendererVerification',
    [string[]]$Saves=@('saveg003','saveg007'),
    [int[]]$Scales=@(1,2)
)
$ErrorActionPreference='Stop'
# Run under Windows PowerShell: the corpus pixel comparer targets .NET Framework.
if ($PSVersionTable.PSEdition -eq 'Core') { throw 'Run this report with Windows PowerShell 5.1.' }
Add-Type -AssemblyName System.Drawing
$source=Get-Content (Join-Path $PSScriptRoot 'renderer-compare-corpus.ps1') -Raw
$match=[regex]::Match($source,"(?s)Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'\r?\n(.*?)\r?\n'@")
if (!$match.Success) { throw 'Corpus image comparison helper not found.' }
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition $match.Groups[1].Value
function Median($rows,[string]$field) {
    $values=@($rows | ForEach-Object { [double]$_.$field } | Sort-Object)
    ($values[399]+$values[400])/2
}
foreach ($save in $Saves) { foreach ($scale in $Scales) {
    $paths=@('ref','probe' | ForEach-Object { Join-Path $Root "$Tag-$save-ss$scale-$_" })
    $profiles=@($paths | ForEach-Object { Get-Content (Join-Path $_ 'profile.json') -Raw | ConvertFrom-Json })
    foreach ($field in @('executable','save','width','height','ssaa','msaa','ao','bloom','renderThreadCpu')) {
        if ($profiles[0].$field -ne $profiles[1].$field) { throw "Pair differs in $field" }
    }
    $samples=@($paths | ForEach-Object {
        $rows=@(Import-Csv -Delimiter "`t" (Join-Path $_ 'frames.tsv') |
            Where-Object { [int]$_.gameplay_frame -ge 400 -and [int]$_.gameplay_frame -lt 1200 })
        if ($rows.Count -ne 800) { throw "Incomplete timing window: $_" }
        [pscustomobject]@{CPU=(Median $rows 'render_ms');GPU=(Median $rows 'gpu_frame_ms')}
    })
    $pixels=[RendererImageComparison]::Measure((Join-Path $paths[0] 'saveg003.png'),
        (Join-Path $paths[1] 'saveg003.png'),@())
    [pscustomobject]@{Save=$save;SSAA=$scale;ReferenceCPU=$samples[0].CPU;ProbeCPU=$samples[1].CPU;
        ReferenceGPU=$samples[0].GPU;ProbeGPU=$samples[1].GPU;
        MeanPixelError=$pixels[2];ChangedPercent=$pixels[3];AboveTwoPercent=$pixels[4];MaxPixelError=$pixels[5]}
}}
