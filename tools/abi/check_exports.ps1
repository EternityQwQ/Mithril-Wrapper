param(
    [Parameter(Mandatory = $true)][string]$Binary,
    [string]$Manifest = "abi/manifest/gl-egl-manifest.json",
    [switch]$Release
)

$ErrorActionPreference = "Stop"
$manifestData = Get-Content -Raw -LiteralPath $Manifest | ConvertFrom-Json
if ($Release -and ($manifestData.symbols | Where-Object status -eq 'provisional')) {
    throw "Release ABI check rejects provisional symbols"
}

$tool = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($tool) {
    $exports = (& $tool.Source /nologo /exports $Binary) -join "`n"
} else {
    $nm = Get-Command nm -ErrorAction Stop
    $exports = (& $nm.Source -g $Binary) -join "`n"
}

$missing = @()
foreach ($symbol in $manifestData.symbols | Where-Object status -ne 'unsupported') {
    if ($exports -notmatch "(?m)(^|\s)_?$([regex]::Escape($symbol.name))(\s|$)") {
        $missing += $symbol.name
    }
}
if ($missing.Count) { throw "Missing ABI exports: $($missing -join ', ')" }
Write-Output "ABI exports match $($manifestData.symbols.Count) manifest entries"
