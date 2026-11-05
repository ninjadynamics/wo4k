[CmdletBinding()]
param(
    [string]$CompilerDirectory = 'C:/mingw64/bin',
    [string]$DependencyDirectory,
    [string]$SevenZip = 'C:/Program Files/7-Zip/7z.exe'
)
$ErrorActionPreference = 'Stop'
$compiler = (Join-Path $CompilerDirectory 'gcc.exe').Replace('\', '/')
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw "C compiler not found: $compiler" }
$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$version = (& $compiler -v 2>&1 | Out-String)
$ErrorActionPreference = $savedPreference
if ($version -notmatch 'ucrt') { throw 'This dependency lock requires a Windows x86_64 UCRT MinGW compiler.' }
$target = & $compiler -dumpmachine
if ($target -ne 'x86_64-w64-mingw32') { throw "Expected x86_64 compiler; got $target" }
$tar = (Get-Command tar.exe -ErrorAction Stop).Source
if (-not $DependencyDirectory) { $DependencyDirectory = Join-Path $PSScriptRoot '../../.work/deband-build' }
$cache = [IO.Path]::GetFullPath($DependencyDirectory)
[IO.Directory]::CreateDirectory($cache) | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
$packages = $lock.packages
$includeFlags = @()
$libraryFlags = @()
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
foreach ($package in $packages) {
    $filename = [IO.Path]::GetFileName([Uri]$package.url)
    $archive = Join-Path $cache $filename
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        Write-Host "Downloading $($package.name) $($package.version)..."
        Invoke-WebRequest -UseBasicParsing -Uri $package.url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.sha256) {
        throw "Dependency checksum mismatch: $archive"
    }
    $unpacked = Join-Path $cache "$($package.name)-$($package.version)"
    $tarArchive = $archive
    # Windows' bundled tar on older systems cannot decompress zstd itself.
    if (Test-Path -LiteralPath $SevenZip -PathType Leaf) {
        & $SevenZip x -y "-o$cache" $archive | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cannot decompress dependency: $archive" }
        $tarArchive = $archive -replace '\.zst$', ''
    }
    $entries = @(& $tar -tf $tarArchive)
    if ($LASTEXITCODE -ne 0) { throw "Cannot list dependency: $archive" }
    foreach ($entry in $entries) {
        if ($entry -match '(^[/\\]|^[A-Za-z]:|(^|[/\\])\.\.([/\\]|$))') {
            throw "Unsafe dependency archive path: $entry"
        }
    }
    [IO.Directory]::CreateDirectory($unpacked) | Out-Null
    & $tar -xf $tarArchive -C $unpacked
    if ($LASTEXITCODE -ne 0) { throw "Cannot extract dependency: $archive" }
    $includeFlags += '-I' + (Join-Path $unpacked 'ucrt64/include')
    $libraryFlags += '-L' + (Join-Path $unpacked 'ucrt64/lib')
}
$pngSource = $lock.png_source
$pngArchive = Join-Path $cache "libpng-$($pngSource.version).tar.gz"
if (-not (Test-Path -LiteralPath $pngArchive -PathType Leaf)) {
    Write-Host "Downloading libpng $($pngSource.version) source..."
    Invoke-WebRequest -UseBasicParsing -Uri $pngSource.url -OutFile $pngArchive
}
if ((Get-FileHash -LiteralPath $pngArchive -Algorithm SHA256).Hash -ne $pngSource.sha256) {
    throw "Dependency checksum mismatch: $pngArchive"
}
$pngEntries = @(& $tar -tf $pngArchive)
if ($LASTEXITCODE -ne 0) { throw 'Cannot list libpng source archive.' }
foreach ($entry in $pngEntries) {
    if ($entry -match '(^[/\\]|^[A-Za-z]:|(^|[/\\])\.\.([/\\]|$))') { throw "Unsafe libpng source path: $entry" }
}
& $tar -xf $pngArchive -C $cache
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract libpng source.' }
$pngRoot = Join-Path $cache "libpng-$($pngSource.version)"
$pngBuild = Join-Path $cache 'libpng-static'
$zlibPackage = $packages | Where-Object { $_.name -eq 'zlib' }
$zlibRoot = Join-Path $cache "zlib-$($zlibPackage.version)/ucrt64"
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ninja = (Join-Path $CompilerDirectory 'ninja.exe').Replace('\', '/')
$oldPath = $env:PATH
try {
    $env:PATH = $CompilerDirectory + ';' + $oldPath
    & $cmake -S $pngRoot -B $pngBuild -G Ninja "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$ninja" `
        -DCMAKE_BUILD_TYPE=Release -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF `
        "-DZLIB_LIBRARY=$zlibRoot/lib/libz.a" "-DZLIB_INCLUDE_DIR=$zlibRoot/include"
    if ($LASTEXITCODE -ne 0) { throw 'libpng configuration failed.' }
    & $cmake --build $pngBuild --target png_static --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'libpng compilation failed.' }
} finally { $env:PATH = $oldPath }
$output = Join-Path $PSScriptRoot 'deband.exe'
$flags = @('-O3', '-std=c99', '-Wall', '-Wextra', '-D__USE_MINGW_ANSI_STDIO=1', '-DPNG_STATIC', '-static', '-static-libgcc', '-Wl,--no-insert-timestamp')
$compilerArgs = $flags + @(('-I' + $pngRoot), ('-I' + $pngBuild)) + $includeFlags + $libraryFlags + @('-o', $output, (Join-Path $PSScriptRoot 'deband.c'), (Join-Path $pngBuild 'libpng16.a'), '-ljpeg', '-lz', '-lm')
& $compiler @compilerArgs
if ($LASTEXITCODE -ne 0) { throw "Deband compilation failed (exit $LASTEXITCODE)." }
$info = [ordered]@{
    format = 1
    compiler = $compiler
    compiler_version = $version.Trim()
    arguments = $compilerArgs
    source_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'deband.c') -Algorithm SHA256).Hash.ToLowerInvariant()
    executable_sha256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
    dependencies = $packages
    libpng_source = $pngSource
}
$info | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $cache 'build-info.json') -Encoding UTF8
Write-Host "Built $output"
Write-Host "Dependency licenses and build record: $cache"
