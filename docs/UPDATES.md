# Release updates and source link

Put a single HTTPS URL in each UTF-8 text file beside Setup.exe:

- `release.url`: a GitHub repository's `/releases` or `/releases/latest`
  URL, its GitHub API `/repos/OWNER/REPO/releases/latest` endpoint, or a
  direct HTTPS ZIP URL. The latest-release page/API endpoint follows new
  releases automatically. A fixed ZIP URL always targets that ZIP.
- `source.url`: the project's `https://github.com/OWNER/REPO` URL or
  GitHub organization page, opened by the **GitHub** logo beside the subtitle.

WO4K ships with source.url set to https://github.com/ninjadynamics/wo4k and
release.url set to https://github.com/ninjadynamics/wo4k/releases/latest.
Both remain plain text containing one HTTPS URL, rather than Windows Internet
Shortcut sections. Missing, blank, unreadable, invalid UTF-8,
multiline or malformed files disable their controls without a popup. Plain
HTTP, credentials in URLs, local files and non-GitHub source links are rejected.
Setup rereads configuration when activated and before using a link. Settings
labels retain their explanatory tooltips; buttons and dropdowns do not repeat
them.

Update runs in a background worker, so the window remains responsive. It uses
Windows' built-in WinHTTP with TLS 1.2 or newer, refuses non-HTTPS redirects,
validates certificates through Windows, and limits time and size (4 MB for
metadata, 256 MB for the package). GitHub API downloads select exactly one
`wo4k*` (or legacy `wo-beta-mod*`) ZIP asset. Publish its `.zip.sha256` sidecar alongside the ZIP;
the updater checks it and GitHub's SHA-256 asset digest when supplied.
Every ZIP must include `release-files.json`, listing every payload file's
size and SHA-256 hash. ZIP traversal, duplicate/undeclared files, incomplete
packages and disallowed game/audio payloads are rejected before installation.
For a direct ZIP URL, the embedded inventory provides corruption detection;
the configured HTTPS server is the trusted source of the package.

When the running copy of Setup is Authenticode-signed, every signed WO4K binary
in the update (`Setup.exe`, `Play.exe` and the three runtime binaries) must
carry a valid signature from the same publisher, or the update is refused
before anything is replaced. Unsigned development builds skip this check and
say so in the log.

The latest-release API excludes GitHub prereleases and drafts. For this
configured updater to find a beta, publish it as a full release (a beta
version string is supported) rather than marking it as a prerelease. Otherwise
users can download it manually from https://github.com/ninjadynamics/wo4k/releases.
Attach the distributable ZIP and its .zip.sha256 sidecar; automatic source
archives do not contain the packaged inventory.

Versions use SemVer, including beta numbers. An older release does not replace
the current installation. An equal release restores damaged packaged files and
runs Install / repair again, making Update a repair fail-safe. Changed mod files
and installed runtime files are backed up under `backups/update-*`; the three
most recent update backups are kept. Running Setup and Play executables are
renamed aside (`*.wo4k-old`) so they can be replaced; Setup deletes them when
it next starts. A failed
copy rolls back files already replaced. URL configuration, audio/music, saves, settings and original
game data are retained. Installed HD artwork is regenerated from new artwork
only when HD artwork was enabled. Setup retains source paths and preferences
in `.state/setup.json`. After an update, a fresh worker runs Install / repair
for that saved configuration. Play and Setup also reconcile local runtime,
texture and audio files after manually extracting a release ZIP over the mod.
Unchanged retired mod files are removed using the previous release inventory;
modified files, saves and unrelated local content are preserved.
Close the game before updating. On successful completion, Setup starts the
updated Setup, which waits for the old process to exit before opening its
window. A failed update keeps the existing window open with its explanation.
Temporary Repair mode does not survive the restart.

Releases up to beta.24 used a PowerShell updater that requires `Setup.ps1` in
the package, so it refuses C releases. Players on those versions download the
new ZIP once and extract it over the mod folder; the next Setup or Play launch
repairs the installation and retires the unchanged PowerShell files using the
saved `.state/release-inventory.json`.

Updates keep the supported patched game executable. If a future release
requires a different game executable, the updater leaves the installation
unchanged and retains the downloaded ZIP under `.work/update-*`; extract that
release into a new mod folder and run Install / repair with your source files.
Failed downloads or verification similarly leave installed files unchanged.

The updater never downloads automatically at startup. Both valid URLs can
be configured before building the release ZIP; users may override them later.
