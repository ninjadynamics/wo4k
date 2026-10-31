# Local link configuration. Invalid or absent files are deliberately silent.
function Get-ModConfiguredUrl([string]$Path,[ValidateSet('Release','Source')][string]$Kind) {
    try {
        if(-not [IO.File]::Exists($Path) -or (Get-Item -LiteralPath $Path).Length -gt 4096){return $null}
        $encoding=New-Object Text.UTF8Encoding($false,$true)
        $value=[IO.File]::ReadAllText($Path,$encoding).Trim().TrimStart([char]0xFEFF).Trim()
        if(-not $value -or $value.Length -gt 2048 -or $value -match '[\s\x00-\x1f\x7f"<>\\]'){return $null}
        $uri=$null
        if(-not [Uri]::TryCreate($value,[UriKind]::Absolute,[ref]$uri)){return $null}
        if($uri.Scheme -ne 'https' -or -not $uri.Host -or $uri.UserInfo -or $uri.Fragment -or $uri.Port -ne 443){return $null}
        if($Kind -eq 'Source'){
            if($uri.Host -ine 'github.com' -or $uri.AbsolutePath -notmatch '^/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)?/?$' -or $uri.Query){return $null}
        } elseif(-not (Get-ModReleaseEndpoint $uri.AbsoluteUri)){return $null}
        return $uri.AbsoluteUri
    } catch {return $null}
}
function Get-ModReleaseEndpoint([string]$Url) {
    $uri=$null
    if(-not [Uri]::TryCreate($Url,[UriKind]::Absolute,[ref]$uri) -or $uri.Scheme -ne 'https' -or $uri.UserInfo -or $uri.Fragment){return $null}
    if($uri.Host -ieq 'github.com' -and $uri.AbsolutePath -match '^/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/releases(?:/latest)?/?$'){
        return 'https://api.github.com/repos/'+$Matches[1]+'/'+$Matches[2]+'/releases/latest'
    }
    if($uri.Host -ieq 'api.github.com' -and $uri.AbsolutePath -match '^/repos/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/releases/latest/?$' -and -not $uri.Query){return $uri.AbsoluteUri}
    if($uri.AbsolutePath -match '(?i)\.zip$'){return $uri.AbsoluteUri}
    return $null
}
