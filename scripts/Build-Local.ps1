#requires -Version 7.0
param([string]$ToolchainRoot='C:\CS2\.build\cs2fixes-toolchain', [int]$Jobs=4, [switch]$TestTrainingPolicy)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$setup=Join-Path $ToolchainRoot 'msvc/setup_x64.bat'
$python=Join-Path $ToolchainRoot 'python/python.exe'
if(!(Test-Path -LiteralPath $setup) -or !(Test-Path -LiteralPath $python)) { throw '需要先准备便携MSVC、Windows SDK、Python和AMBuild，详见docs/SDK_UPDATE_20261113.md。' }
foreach($line in Get-Content -LiteralPath $setup) {
    if($line -match '^set ([A-Za-z0-9_]+)=(.*)$') {
        $name=$Matches[1]; $value=$Matches[2]
        $value=$value.Replace('%~dp0',((Split-Path $setup -Parent)+'\')).Replace('%PATH%',$env:PATH)
        [Environment]::SetEnvironmentVariable($name,$value,'Process')
    }
}
$env:PATH=(Join-Path $ToolchainRoot 'python/Scripts')+';'+$env:PATH
$env:HL2SDKCS2=Join-Path $root 'sdk'
$env:MMSOURCE_DEV=Join-Path $ToolchainRoot 'metamod-source'
$env:CC='cl'
$env:CXX='cl'
$temp=Join-Path $ToolchainRoot 'tmp'
New-Item -ItemType Directory -Path $temp -Force | Out-Null
$env:TEMP=$temp
$env:TMP=$temp
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
if ($TestTrainingPolicy) {
    $testDir=Join-Path $root 'build/training-policy-tests'
    New-Item -ItemType Directory -Path $testDir -Force | Out-Null
    Push-Location $testDir
    try {
        & cl /nologo /std:c++20 /EHsc /W4 /Fe:policy-tests.exe (Join-Path $root 'tests/ZrTrainingPolicy.cpp')
        if ($LASTEXITCODE -ne 0) { throw 'Training policy test compilation failed' }
        & ./policy-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Training policy tests failed' }
    } finally { Pop-Location }
    return
}
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
Push-Location $build
try {
    # Embedded Python's isolated path omits the script directory; restore the
    # normal sys.path[0] expected by the upstream configure.py.
    & $python -c 'import runpy,sys; root=sys.argv.pop(1); sys.path.insert(0,root); sys.argv[0]=root+"/configure.py"; runpy.run_path(sys.argv[0],run_name="__main__")' $root --enable-optimize --sdks cs2
    if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath (Join-Path $build '.ambuild2/vars'))) { throw 'AMBuild configure failed' }
    & (Join-Path $ToolchainRoot 'python/Scripts/ambuild.exe') -j $Jobs
    if($LASTEXITCODE -ne 0) { throw 'AMBuild compilation failed' }
} finally { Pop-Location }
