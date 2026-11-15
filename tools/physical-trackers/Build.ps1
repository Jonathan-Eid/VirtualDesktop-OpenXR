param([Parameter(Mandatory=$true)][string]$BuildRoot)
$ErrorActionPreference = 'Stop'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$buildPath = (Resolve-Path -LiteralPath $BuildRoot).Path
$surviveSource = Join-Path $buildPath 'libsurvive'
$surviveBuild = Join-Path $buildPath 'libsurvive-build'
$providerBuild = Join-Path $buildPath 'provider-build'
$revision = '44b1e2f0cb95e5697bc0b86f58fd8520a813b713'
if (!(Test-Path -LiteralPath $surviveSource)) {
    Invoke-Checked git @('clone','https://github.com/collabora/libsurvive.git',$surviveSource)
}
Invoke-Checked git @('-C',$surviveSource,'checkout','--detach',$revision)
Invoke-Checked cmake @('-S',$surviveSource,'-B',$surviveBuild,'-G','Visual Studio 17 2022','-A','x64','-DUSE_SINGLE_PRECISION=OFF','-DENABLE_TESTS=OFF')
Invoke-Checked cmake @('--build',$surviveBuild,'--config','Release','--target','survive-cli','--parallel','4')
Push-Location $repo
try {
    Invoke-Checked git @('submodule','update','--init','external/OpenXR-SDK','external/OpenXR-MixedReality','external/LibOVR','external/Vulkan-SDK','external/fmt','external/openvr','external/FidelityFX-FSR','external/FidelityFX-CAS','external/cJSON')
    Invoke-Checked "$surviveBuild/nuget.exe" @('restore','virtualdesktop-openxr/packages.config','-PackagesDirectory',"$repo/packages",'-NonInteractive')
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
    if (!$msbuild) { throw 'Visual Studio 2022 with C++ desktop tools is required' }
    Invoke-Checked $msbuild @('virtualdesktop-openxr/virtualdesktop-openxr.vcxproj','/p:Configuration=Release','/p:Platform=x64',"/p:SolutionDir=$repo\",'/m','/v:minimal','/nologo')
} finally { Pop-Location }
Invoke-Checked cmake @('-S',$PSScriptRoot,'-B',$providerBuild,'-G','Visual Studio 17 2022','-A','x64',"-DSURVIVE_SOURCE=$surviveSource", "-DSURVIVE_LIBRARY=$surviveBuild/src/Release/survive.lib", "-DVDXR_RUNTIME_DLL=$repo/bin/x64/Release/virtualdesktop-openxr.dll")
Invoke-Checked cmake @('--build',$providerBuild,'--config','Release')
Invoke-Checked ctest @('--test-dir',$providerBuild,'-C','Release','--output-on-failure')
# libsurvive loads its plugins relative to its library. Keep the directory intact.
Copy-Item -LiteralPath "$providerBuild/Release/vdxr-survive.exe" -Destination "$surviveBuild/Release/vdxr-survive.exe"
Write-Output "Provider: $surviveBuild/Release/vdxr-survive.exe"
Write-Output "Runtime: $repo/bin/x64/Release/virtualdesktop-openxr.json"
