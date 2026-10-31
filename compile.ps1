param([ValidateSet('all','test','package','release','clean','help')][string]$Target='all',[string]$Version='',[string]$ToolsDirectory=(Join-Path $PSScriptRoot '.tools'))
$ErrorActionPreference='Stop'
$tools=[IO.Path]::GetFullPath($ToolsDirectory)
[IO.Directory]::CreateDirectory($tools)|Out-Null
function Find-LocalToolchain {
    foreach($gcc in @(Get-ChildItem -LiteralPath $tools -Recurse -File -Filter gcc.exe)){
        $bin=$gcc.DirectoryName
        $required=@('clang.exe','llvm-dlltool.exe','lld-link.exe','windres.exe','mingw32-make.exe')
        if(@($required|Where-Object {-not [IO.File]::Exists((Join-Path $bin $_))}).Count -eq 0){return $bin}
    }
    return $null
}
$bin=Find-LocalToolchain
if(-not $bin){
    if(-not (Get-Command winget.exe -ErrorAction SilentlyContinue)){throw 'WinGet is required by this optional bootstrap. Install App Installer, or use your existing compiler and Make directly; see HOW-TO-COMPILE.md.'}
    $downloads=Join-Path $tools 'downloads';[IO.Directory]::CreateDirectory($downloads)|Out-Null
    # This portable WinLibs package includes GCC, LLVM and GNU Make. WinGet
    # verifies its package hash; unpack it locally rather than installing globally.
    & winget.exe download --id BrechtSanders.WinLibs.POSIX.MSVCRT.LLVM --exact --source winget --architecture x64 --download-directory $downloads --skip-dependencies --skip-license --accept-source-agreements --accept-package-agreements --disable-interactivity
    if($LASTEXITCODE){throw "WinGet compiler/Make download failed: $LASTEXITCODE"}
    $archives=@(Get-ChildItem -LiteralPath $downloads -File -Filter '*.zip'|Sort-Object LastWriteTime -Descending)
    if(-not $archives.Count){throw "WinGet did not provide the expected portable ZIP. Check $downloads; no global installer was run."}
    Expand-Archive -LiteralPath $archives[0].FullName -DestinationPath (Join-Path $tools 'toolchain') -Force
    $bin=Find-LocalToolchain
    if(-not $bin){throw 'The downloaded toolchain lacks GCC/LLVM/Make. See HOW-TO-COMPILE.md for the manual build.'}
}
$savedPath=$env:PATH
try{
    $env:PATH=$bin+';'+$env:PATH
    & (Join-Path $bin 'mingw32-make.exe') -C $PSScriptRoot $Target ('TOOLCHAIN='+$bin.Replace('\','/')) ('VERSION='+$Version)
    if($LASTEXITCODE){throw "Make failed: $LASTEXITCODE"}
}finally{$env:PATH=$savedPath}
