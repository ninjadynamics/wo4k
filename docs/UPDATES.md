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

Update runs curl in a background worker, so the window remains responsive.
It uses HTTPS for downloads and redirects, with time/size limits and curl's
certificate validation. GitHub API downloads select exactly one `wo-beta-mod*`
or `wo4k*` ZIP asset. Publish its `.zip.sha256` sidecar alongside the ZIP;
the updater checks it and GitHub's SHA-256 asset digest when supplied.
Every ZIP must include `release-files.json`, listing every payload file's
size and SHA-256 hash. ZIP traversal, duplicate/undeclared files, incomplete
packages and disallowed game/audio payloads are rejected before installation.
For a direct ZIP URL, the embedded inventory provides corruption detection;
the configured HTTPS server is the trusted source of the package.

The latest-release API excludes GitHub prereleases and drafts. For this
configured updater to find a beta, publish it as a full release (a beta
version string is supported) rather than marking it as a prerelease. Otherwise
users can download it manually from https://github.com/ninjadynamics/wo4k/releases.
Attach the distributable ZIP and its .zip.sha256 sidecar; automatic source
archives do not contain the packaged inventory.

Versions use SemVer, including beta numbers. An older release does not replace
the current installation. An equal release restores damaged packaged files and
runs Install / repair again, making Update a repair fail-safe. Changed mod files
and installed runtime files are backed up under `backups/update-*`. A failed
copy rolls back files already replaced. URL configuration, audio/music, saves, settings and original
game data are retained. Installed HD artwork is regenerated from new artwork
only when HD artwork was enabled. Setup retains source paths and preferences
in `.state/setup.json`. After an update, a fresh worker runs Install / repair
for that saved configuration. Play and Setup also reconcile local runtime,
texture and audio files after manually extracting a release ZIP over the mod.
Unchanged retired mod files are removed using the previous release inventory;
modified files, saves and unrelated local content are preserved.
Close the game before updating. On successful completion, Setup closes and a
hidden helper waits for its old process to exit before opening the updated app.
A failed update keeps the existing window open with its explanation. Interfaces
from beta.19 or earlier need one manual close/reopen after their first upgrade;
subsequent updates restart automatically. Temporary Repair mode does not survive
the restart.

Updates keep the supported patched game executable. If a future release
requires a different game executable, the updater leaves the installation
unchanged and retains the downloaded ZIP under `.work/update-*`; extract that
release into a new mod folder and run Install / repair with your source files.
Failed downloads or verification similarly leave installed files unchanged.

The updater never downloads automatically at startup. Both valid URLs can
be configured before building the release ZIP; users may override them later.
