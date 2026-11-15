param(
    [Parameter(Mandatory=$true)][string]$Application,
    [string[]]$ApplicationArguments = @(),
    [string]$RuntimeJson = "$PSScriptRoot/../../bin/x64/Release/virtualdesktop-openxr.json"
)
$ErrorActionPreference = 'Stop'
$appPath = (Resolve-Path -LiteralPath $Application).Path
$runtimePath = (Resolve-Path -LiteralPath $RuntimeJson).Path
$previousRuntime = $env:XR_RUNTIME_JSON
$previousSource = $env:VDXR_TRACKER_SOURCE
try {
    $env:XR_RUNTIME_JSON = $runtimePath
    $env:VDXR_TRACKER_SOURCE = 'libsurvive'
    & $appPath @ApplicationArguments
} finally {
    $env:XR_RUNTIME_JSON = $previousRuntime
    $env:VDXR_TRACKER_SOURCE = $previousSource
}
