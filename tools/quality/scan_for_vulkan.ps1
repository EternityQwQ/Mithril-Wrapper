param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [switch]$RequireTargetArtifact
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$allowed = @(".cpp", ".h")
$neutralDirectories = @("core", "abi", "gl", "ir", "backend", "shader")
$files = foreach ($directory in $neutralDirectories) {
    $path = Join-Path $root ("Mithril-Wrapper-cpp/src/" + $directory)
    if (Test-Path -LiteralPath $path) {
        Get-ChildItem -LiteralPath $path -Recurse -File | Where-Object { $allowed -contains $_.Extension.ToLowerInvariant() }
    }
}
$terms = @('Vk[A-Za-z]', 'vulkan', 'MoltenVK', 'Objective-C', '\bid<MTL')
$violations = foreach ($file in $files) {
    $matches = Select-String -LiteralPath $file.FullName -Pattern $terms -CaseSensitive
    if ($matches) { $matches }
}
if ($violations) {
    $violations | ForEach-Object { Write-Error ("{0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim()) }
    exit 1
}

$compileDb = Join-Path $BuildDirectory "compile_commands.json"
if (Test-Path -LiteralPath $compileDb) {
    $compileText = Get-Content -Raw -LiteralPath $compileDb
    if ($compileText -match '(?i)(vulkan|moltenvk|directvulkan)') { throw "Pure core compile commands contain forbidden backend dependency" }
}
$boundary = Join-Path $BuildDirectory "mithril_core.boundary.json"
if (Test-Path -LiteralPath $boundary) {
    $boundaryText = Get-Content -Raw -LiteralPath $boundary
    if ($boundaryText -match '(?i)(vulkan|moltenvk|directvulkan|metal|\.mm)') {
        throw "mithril_core target boundary artifact contains forbidden dependency"
    }
} elseif ($RequireTargetArtifact) {
    throw "Missing mithril_core boundary artifact: $boundary"
}
Write-Output "mithril_core boundary scan passed ($($files.Count) source files)"
