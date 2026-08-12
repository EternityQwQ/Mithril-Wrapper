param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [switch]$RequireTargetArtifact
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$directories = @("metal", "platform/apple", "egl")
$files = foreach ($directory in $directories) {
    $path = Join-Path $root ("Mithril-Wrapper-cpp/src/" + $directory)
    if (Test-Path -LiteralPath $path) {
        Get-ChildItem -LiteralPath $path -Recurse -File |
            Where-Object { @(".cpp", ".h", ".mm") -contains $_.Extension.ToLowerInvariant() }
    }
}
$unsafeClassMutation = 'object' + '_setClass'
$terms = @('Vk[A-Za-z]', 'vulkan', 'MoltenVK', 'DirectVulkan', $unsafeClassMutation)
$violations = foreach ($file in $files) {
    Select-String -LiteralPath $file.FullName -Pattern $terms -CaseSensitive
}
if ($violations) {
    $violations | ForEach-Object { Write-Error ("{0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim()) }
    exit 1
}

$boundary = Join-Path $BuildDirectory "mithril_direct.boundary.json"
if (Test-Path -LiteralPath $boundary) {
    if ((Get-Content -Raw -LiteralPath $boundary) -match '(?i)(vulkan|moltenvk|directvulkan)') {
        throw "mithril_direct boundary artifact contains a forbidden dependency"
    }
} elseif ($RequireTargetArtifact) {
    throw "Missing mithril_direct boundary artifact: $boundary"
}
Write-Output "mithril_direct source scan passed ($($files.Count) source files)"
