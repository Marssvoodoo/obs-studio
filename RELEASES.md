# Voodoo OBS Studio Releases

Voodoo OBS Studio releases mirror the platform and debug-symbol choices offered
by upstream OBS while keeping community-fork packages clearly distinguishable.

## Release tag format

Use a tag without a leading `v`:

```text
<base-version>-voodoo<revision>
```

Examples:

```text
32.2.2-voodoo1
32.2.0-rc2-voodoo1
```

Accepted base versions are `MAJOR.MINOR.PATCH`,
`MAJOR.MINOR.PATCH-betaN`, and `MAJOR.MINOR.PATCH-rcN`. The Voodoo revision is a
positive numeric release iteration for that base.

## What a tag does

Pushing a valid Voodoo tag runs the existing multi-platform OBS build and then:

1. builds macOS Apple silicon and Intel packages;
2. builds Ubuntu 24.04 and Ubuntu 26.04 packages;
3. builds Windows x64 and arm64 packages with the pinned VST3 SDK;
4. packages Windows portable ZIPs, an x64 installer, and PDB archives through
   the pinned OBS `bouf` toolchain with code signing disabled;
5. renames every artifact to the public release convention;
6. calculates a SHA-256 checksum for every download;
7. creates a draft GitHub release for final inspection.

The official OBS signing workflow remains unchanged when the repository owner is
`obsproject`. The fork path runs only for non-OBS owners and only for tags that
contain the `-voodooN` suffix.

## Download matrix

A complete release contains 14 assets:

| # | Platform | Asset |
| ---: | --- | --- |
| 1 | macOS Apple silicon | `OBS-Studio-<version>-macOS-Apple.dmg` |
| 2 | macOS Apple silicon symbols | `OBS-Studio-<version>-macOS-Apple-dSYMs.tar.xz` |
| 3 | macOS Intel | `OBS-Studio-<version>-macOS-Intel.dmg` |
| 4 | macOS Intel symbols | `OBS-Studio-<version>-macOS-Intel-dSYMs.tar.xz` |
| 5 | Ubuntu 24.04 x86_64 | `OBS-Studio-<version>-Ubuntu-24.04-x86_64.deb` |
| 6 | Ubuntu 24.04 debug symbols | `OBS-Studio-<version>-Ubuntu-24.04-x86_64-dbsym.ddeb` |
| 7 | Ubuntu 26.04 x86_64 | `OBS-Studio-<version>-Ubuntu-26.04-x86_64.deb` |
| 8 | Ubuntu 26.04 debug symbols | `OBS-Studio-<version>-Ubuntu-26.04-x86_64-dbsym.ddeb` |
| 9 | Source archive | `OBS-Studio-<version>-Sources.tar.gz` |
| 10 | Windows x64 portable | `OBS-Studio-<version>-Windows-x64.zip` |
| 11 | Windows x64 installer | `OBS-Studio-<version>-Windows-x64-Installer.exe` |
| 12 | Windows x64 symbols | `OBS-Studio-<version>-Windows-x64-PDBs.zip` |
| 13 | Windows arm64 portable | `OBS-Studio-<version>-Windows-arm64.zip` |
| 14 | Windows arm64 symbols | `OBS-Studio-<version>-Windows-arm64-PDBs.zip` |

The release body states that the downloads are community-fork builds and that
Windows binaries are unsigned. macOS signing and notarization depend on secrets
configured by the fork owner. VST3 hosting is currently included only in the
Windows packages.

## Create a draft release

Run these commands from a clean, reviewed release commit:

```powershell
$Version = '32.2.2-voodoo1'
git tag -a $Version -m "Voodoo OBS Studio $Version"
git push origin $Version
```

The tag starts the `Push` workflow. Do not publish the draft until every required
job is green and the release has all 14 assets.

## Release acceptance

### Repository

- The tag resolves to the intended reviewed commit.
- The tag's base version matches the merged upstream tag or release branch.
- The worktree is clean and the remote branch SHA matches the local SHA.
- No credential, environment file, private key, build directory, or local cache
  is present in the commit.

### Windows packages

- `obs64.exe` reports the intended Voodoo version.
- `obs-vst.dll`, `obs-vst2-scanner.exe`, `obs-vst3.dll`, and
  `obs-vst3-scanner.exe` are present in the x64 package.
- `obs-vst3.dll` loads once, and the log reports the expected cached plug-in
  count or a first-scan instruction.
- The installer and portable ZIP contain the same core runtime files.
- The unsigned warning is visible in the release body.

### Runtime

- OBS completes a cold launch from the packaged image.
- The Add Source window opens and shows the 32.2 source browser.
- The mixer, Program Output strip, movable Preview, custom layouts, and VST scan
  manager open without changing dock geometry.
- Restream Chat, Stream Information, and Channels appear once each when Restream
  is active.
- A normal close leaves no OBS or scanner process and creates no crash report.

### Downloads

- All 14 expected filenames are attached to the draft.
- `CHECKSUMS.txt` contains one SHA-256 value for every attached asset.
- A fresh download of each Windows package matches the published checksum.
- Debug-symbol archives are kept separate from end-user packages.

## Rollback

GitHub releases are drafts until manually published. If packaging or runtime
acceptance fails, delete the draft and leave the tag in place while investigating
only when preserving failure evidence is useful. Otherwise, remove the bad remote
tag through the normal repository-owner workflow and create a new Voodoo revision;
never move a tag that users may already have downloaded.
