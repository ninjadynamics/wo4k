param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$catalog=Get-Content -LiteralPath (Join-Path $root 'manifests/hd-textures.json') -Encoding UTF8 -Raw|ConvertFrom-Json
$lines=New-Object Collections.Generic.List[string]
$lines.Add('/* Generated from manifests/hd-textures.json. */')
$lines.Add('typedef struct HdAlias {const WCHAR *png;const char *native;UINT page;} HdAlias;')
$lines.Add('static const HdAlias hd_aliases[]={')
foreach($entry in $catalog|Where-Object kind -eq atlas){
    if($entry.file -notmatch '^[a-z0-9-]+\.png$' -or $entry.native -notmatch '^[a-z0-9]+\.tga$' -or $entry.page -lt 0 -or $entry.page -gt 255){throw 'Invalid HD texture catalog.'}
    $lines.Add(('    {L"'+$entry.file+'","'+$entry.native+'",'+$entry.page+'},'))
}
$lines.Add('};')
$build=Join-Path $root 'src/runtime/build';[IO.Directory]::CreateDirectory($build)|Out-Null
[IO.File]::WriteAllText((Join-Path $build 'hd_catalog.h'),($lines -join "`n"),[Text.Encoding]::ASCII)