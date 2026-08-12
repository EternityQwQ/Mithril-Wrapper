param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$OutputPath = "abi/manifest/gl-egl-manifest.json"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$rulesPath = Join-Path $root "abi/manifest/status-rules.json"
$rules = Get-Content -Raw -LiteralPath $rulesPath | ConvertFrom-Json
$validStatuses = @("implemented", "provisional", "unsupported")
if ($validStatuses -notcontains $rules.default_status) {
    throw "Unknown default ABI status: $($rules.default_status)"
}

$headers = @(
    "Mithril-Wrapper-cpp/include/GL/glcorearb.h",
    "Mithril-Wrapper-cpp/include/GL/gl.h",
    "Mithril-Wrapper-cpp/include/EGL/egl.h"
)

function Normalize-Signature([string]$Value) {
    $value = $Value -replace '/\*[\s\S]*?\*/', ' '
    $value = $value -replace '//[^\r\n]*', ' '
    $value = $value -replace '\b(GLAPI|EGLAPI|GLAPIENTRY|EGLAPIENTRY)\b', ' '
    $value = $value -replace '\s+', ' '
    $value = $value -replace '\s*([(),*])\s*', '$1'
    return $value.Trim()
}

function Get-Sha256([string]$Value) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = $sha.ComputeHash($bytes) } finally { $sha.Dispose() }
    return (($hash | ForEach-Object { $_.ToString('x2') }) -join '')
}

$stubText = Get-Content -Raw -LiteralPath (Join-Path $root $rules.unsupported_source)
$stubNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
[regex]::Matches($stubText, '(?m)^\s*(?!if\b|for\b|while\b|switch\b)[\w:*&<>\s]+\s+(gl[A-Z]\w*)\s*\([^;{}]*\)\s*\{') |
    ForEach-Object { [void]$stubNames.Add($_.Groups[1].Value) }

$byName = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
foreach ($relativePath in $headers) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
    $text = $text -replace '/\*[\s\S]*?\*/', ' '
    $text = $text -replace '(?m)//.*$', ' '
    $pattern = '(?ms)(?<!typedef\s)(?<decl>(?:GLAPI\s+|EGLAPI\s+)?[A-Za-z_][\w\s*]*?\s+(?<name>(?:gl|egl)[A-Z]\w*)\s*\([^;{}]*?\))\s*;'
    foreach ($match in [regex]::Matches($text, $pattern)) {
        $name = $match.Groups['name'].Value
        $signature = Normalize-Signature $match.Groups['decl'].Value
        if ($byName.ContainsKey($name)) {
            if ($byName[$name].signature -ne $signature) {
                throw "Conflicting declarations for $name"
            }
            continue
        }
        $status = if ($stubNames.Contains($name)) { "unsupported" } else { $rules.default_status }
        if ($validStatuses -notcontains $status) { throw "Missing status for $name" }
        $isEgl = $name.StartsWith('egl', [StringComparison]::Ordinal)
        $byName.Add($name, [ordered]@{
            name = $name
            signature = $signature
            signature_hash = Get-Sha256 $signature
            api = if ($isEgl) { "EGL" } else { "GL" }
            since = if ($isEgl) { "EGL 1.5 compatibility surface" } else { "OpenGL 3.3 Core compatibility surface" }
            status = $status
            error_behavior = if ($status -eq "unsupported") { $rules.unsupported_error_behavior } else { $rules.provisional_error_behavior }
            evidence = if ($status -eq "unsupported") { $rules.unsupported_source } else { $rules.provisional_reason }
        })
    }
}

foreach ($entry in @(
    @{ name = 'glXGetProcAddress'; signature = 'void* glXGetProcAddress(const char*)' },
    @{ name = 'glXGetProcAddressARB'; signature = 'void* glXGetProcAddressARB(const char*)' }
)) {
    if (!$byName.ContainsKey($entry.name)) {
        $byName.Add($entry.name, [ordered]@{
            name = $entry.name
            signature = $entry.signature
            signature_hash = Get-Sha256 $entry.signature
            api = "GLX"
            since = "Host lookup compatibility"
            status = "provisional"
            error_behavior = $rules.provisional_error_behavior
            evidence = "Mithril-Wrapper-cpp/MG_Impl/lookup.cpp; host trace pending"
        })
    }
}

$symbols = @($byName.Values | Sort-Object { $_.name })
if ($symbols.Count -eq 0) { throw "No public ABI symbols found" }
$manifest = [ordered]@{
    format_version = "1.0"
    library = "mithril"
    generated_from = $headers
    symbols = $symbols
}
$absoluteOutput = Join-Path $root $OutputPath
$parent = Split-Path -Parent $absoluteOutput
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$json = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($absoluteOutput, $json + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "Generated $($symbols.Count) ABI symbols at $absoluteOutput"
