# Updates and releases

Xenon updates itself on macOS with [Sparkle 2](https://sparkle-project.org), as Helium does. On Linux, a release is an
AppImage, which AppImage updaters update, and on Arch Linux, two packages on the AUR install it. Releases are built with `Meta/release.py`, on a Mac and on Linux or by the
release workflow on GitHub's runners, and published as GitHub releases of
[ThaRae/xenon-releases](https://github.com/ThaRae/xenon-releases), a repository of their own.

- [The published releases](#the-published-releases)
- [Making a release](#making-a-release)
- [The AppImage](#the-appimage)
- [The AUR packages](#the-aur-packages)
- [Signing](#signing)
- [In Xenon](#in-xenon)
- [Testing](#testing)
- [Setting up a Mac to release from](#setting-up-a-mac-to-release-from)

## The published releases

Each version is a GitHub release of ThaRae/xenon-releases, tagged `v<version>`, with these files:

- `Xenon-<version>.dmg`, the installer people download. It opens a window with `Xenon.app` beside a link to
  Applications, over a background that says to drag one onto the other (`UI/Icons/macos/installer-background.svg`,
  rendered by `Meta/generate-app-icon.py`), and has Xenon's icon on its volume. `Meta/release.py` makes it with
  [dmgbuild](https://github.com/dmgbuild/dmgbuild), which lays the window out without scripting Finder, so it works the
  same on GitHub's runners as on a Mac. It's signed with the release certificate, like the app.
- `Xenon-<version>.tar.xz`, which holds `Xenon.app`, for Sparkle to update from.
- `Xenon-<version>-x86_64.AppImage`, Xenon for Linux, in one file ([The AppImage](#the-appimage)).
- `Xenon-<version>-x86_64.AppImage.zsync`, which AppImage updaters read to find out what changed, and download only
  that.

The repository's `main` holds `appcast.xml`, Sparkle's feed, which lists the newest release:

| In the appcast | What it is |
| --- | --- |
| `sparkle:version`, `sparkle:shortVersionString` | The version, which is `CFBundleVersion` and `CFBundleShortVersionString` in the app |
| `sparkle:minimumSystemVersion`, `sparkle:hardwareRequirements` | The macOS it needs (the build's deployment target), and `arm64` |
| `enclosure` | The archive's URL, size, and `sparkle:edSignature`, its Ed25519 signature |
| The comment at the end | The appcast's own Ed25519 signature, from Sparkle's `sign_update` |

Xenon reads the appcast from `https://raw.githubusercontent.com/ThaRae/xenon-releases/main/appcast.xml`, a URL that
stays the same from release to release. It downloads the archive from
`https://github.com/ThaRae/xenon-releases/releases/download/v<version>/Xenon-<version>.tar.xz`, which GitHub redirects
to its storage, `release-assets.githubusercontent.com`. Sparkle follows the redirect, takes any status below 400, and
doesn't mind that GitHub serves the appcast as `text/plain`. raw.githubusercontent.com caches a file for up to five
minutes, so a new release can take that long to show up. GitHub takes release files up to 2 GiB, and the archive is
about 54 MiB.

The version is in `VERSION.txt`, as `major.minor.patch`. It can't be called `VERSION`: the repository's root is on the
include path, and on macOS's case-insensitive disks `#include <version>` would find it.

## Making a release

1. Bump `VERSION.txt` on `main`.
2. `./Meta/release.py package`, with `--notes <file>` for release notes in plain text. It builds Xenon and writes
   `Build/distribution`:
   - `Xenon.app`, made to run on any Mac. The build's bundle links its libraries in place, in the build directory and
     vcpkg's, so this copies them in, with Qt's platform, style and image format plugins, makes every rpath relative,
     and strips the binaries. It uses the functions of Ladybird's `Meta/package-macos-test-bundle.py`, which does the
     same for Ladybird. Its `Info.plist` gets the version, the appcast's URL, and the update key's public half;
   - signed with the release certificate, from the inside out;
   - `Xenon-<version>.dmg`, the installer, signed with the release certificate too. `package` mounts it and checks it
     holds the app, the link to Applications, and its window's layout;
   - `Xenon-<version>.tar.xz`, signed with the update key;
   - `appcast.xml`, which lists it, signed too.
3. Try `Build/distribution/Xenon.app`, and open the installer to see its window.
4. Put `Xenon-<version>-x86_64.AppImage` and its `.zsync` file in `Build/distribution` too, after `package`, which
   empties it: from `Meta/release.py package` on Linux ([The AppImage](#the-appimage)), or the `Xenon-<version>-linux`
   artifact of a run of the release workflow on the same commit.
5. `./Meta/release.py publish` asks, then creates the GitHub release with the installer, the archive, and the AppImage
   and its `.zsync` file if `Build/distribution` has them (`gh release create`), commits the appcast to
   ThaRae/xenon-releases' `main`, and tags the commit `v<version>` in ThaRae/Xenon. It publishes the appcast only once
   the archive it names is up. It refuses a tree with uncommitted changes, and a commit that isn't on ThaRae/Xenon's
   `main`. It pushes over HTTPS with `gh`'s credentials, whatever Xenon's `origin` is. It reads the version and the
   feed the release was packaged for from the app in the archive the appcast names, so it runs on Linux too, from those
   files alone.
6. Once it's up, move the AUR packages to it and push them to the AUR, then commit the bump
   ([The AUR packages](#keeping-them-current)):

   ```bash
   ./Meta/aur.py update
   ./Meta/aur.py push
   ```

The release workflow can do steps 2, 4, 5 and 6 instead, without a Mac ([On GitHub's runners](#on-githubs-runners)).

### On GitHub's runners

`.github/workflows/release.yml` does steps 2 and 4, and steps 5 and 6 too with **publish** ticked, when it's run by
hand, from **Actions → Release → Run workflow** or with:

```bash
gh workflow run release.yml --repo ThaRae/Xenon --ref main
```

or, to publish what it builds:

```bash
gh workflow run release.yml --repo ThaRae/Xenon --ref main -f publish=true
```

It also runs on pull requests that change it, `Meta/release.py`, the AppImage's scripts in `Meta/ci`, or `UI/Linux`.
Its jobs:

- **macos**, on a `macos-26` runner. Before it builds, it checks the secrets: the certificate's SHA-1 and the update
  key's public half must be the ones [below](#setting-up-a-mac-to-release-from), so a secret holding another one stops
  the run at once. It builds Xenon as CI's macOS job does, starting from the build caches `main`'s builds saved. A cold
  build doesn't fit in one run, so with no caches to start from, it stops there. After a change to the compiler flags,
  `main`'s caches miss until CI's build of `main` saves new ones, so wait for that. It runs `Meta/release.py test` with
  the release certificate (untick **test** to skip it), then `Meta/release.py package`, and checks what that wrote:
  the archive's and the appcast's signatures, with `sign_update --verify`, the app's `SUPublicEDKey`, and the app's
  and the installer's code signatures. `Xenon-<version>.dmg`, `Xenon-<version>.tar.xz` and `appcast.xml` are the
  artifact `Xenon-<version>-macos`. The certificate and the update key are the repository's secrets
  ([Releasing from GitHub's runners](#releasing-from-githubs-runners)). The job imports the certificate into a keychain
  of its own, and deletes the keychain and the key's file when it's done.
- **linux**, on an `ubuntu-24.04` runner, builds Xenon in Ladybird's CI image as CI's Linux job does, from `main`'s
  build caches, and runs `Meta/release.py package` there (`Meta/ci/package-appimage.sh`). The AppImage, its `.zsync`
  file and `linux-requirements.txt` (the glibc and libstdc++ it needs) are the artifact `Xenon-<version>-linux`, and
  linuxdeploy's log is the artifact `linuxdeploy-log`.
- **linux-test** runs `Meta/release.py test` against that AppImage in containers of Ubuntu 26.04, Fedora 44 and Arch
  Linux that have nothing of the build ([Testing the AppImage](#testing-the-appimage)), with each one's render of the
  welcome page as an artifact.
- **publish**, with **publish** ticked, once the other three have passed: it downloads both artifacts into
  `Build/distribution` and runs `Meta/release.py publish --yes`, which puts up the release, the appcast and the tag as
  it does on a Mac, and the run's summary links the release. That needs the `XENON_RELEASES_TOKEN` secret, and the
  run has to be on `main`. Before it builds, the macos job checks both, and that ThaRae/xenon-releases has no release
  of `VERSION.txt`'s version yet, so a run that couldn't publish stops at once rather than after the build. If
  publishing fails anyway, the release is still the run's artifacts.
- **aur**, once **publish** has put the release up: `Meta/aur.py update` moves the AUR packages to it, with the
  published AppImage's checksum, and `Meta/ci/test-aur-package.sh` builds, installs and tests `xenon-bin`
  ([Testing them](#testing-them)). With the `AUR_SSH_PRIVATE_KEY` secret set, `Meta/aur.py push` pushes both packages
  to the AUR. Either way, it pushes the bump to the branch `aur-<version>` with `XENON_RELEASES_TOKEN`, and opens a pull
  request from it if the token may, or links the branch in the run's summary.

The artifacts are kept for 30 days, and the run's summary has each file's SHA-256.

## The AppImage

`Xenon-<version>-x86_64.AppImage` is one file that runs Xenon on Linux without installing anything: make it
executable and run it. Inside is an AppDir laid out as the build's own prefix (`Build/release/xenon`, from
`UI/Xenon.cmake`), where Xenon finds its helper processes and resources relative to its executable:

| In the AppDir | What it is |
| --- | --- |
| `usr/bin/Xenon` | Xenon, and `qt.conf`, which points Qt at `usr/plugins` |
| `usr/libexec/` | The engine's helper processes: WebContent, RequestServer, ImageDecoder, WebWorker, and the rest |
| `usr/share/Lagom/` | Ladybird's resources, with Xenon's overrides and its own, as the build installs them |
| `usr/plugins/` | Qt's plugins: the X11 and Wayland platforms, and what each needs to draw and take input, the image formats, and GeoClue for geolocation |
| `usr/lib/` | Every library those load that systems don't bring themselves: the engine's, vcpkg's, Qt's |
| `usr/share/applications/org.xenon.Xenon.desktop` | The desktop file (`UI/Linux`), which Xenon names as its own to Wayland and X11 |
| `usr/share/icons/hicolor/` | The icon, in eight sizes (`UI/Icons/linux`, from `Meta/generate-app-icon.py`) |
| `AppRun` | The entry point (`UI/Linux/AppRun`) |

`Meta/release.py package`, on Linux, builds Xenon and copies all of that in, then runs
[linuxdeploy](https://github.com/linuxdeploy/linuxdeploy), which copies in the libraries the binaries load, from the
build, vcpkg's tree and the system. It leaves out the ones every system has its own of, which have to match the
system's drivers and settings (its excludelist: glibc, libstdc++, OpenGL and EGL, the core of X11 and Wayland,
fontconfig, freetype, harfbuzz...), and makes every rpath relative: `$ORIGIN/../lib` for the executables, so the
helpers find their libraries without the environment, which the engine doesn't pass on to them.
[appimagetool](https://github.com/AppImage/appimagetool) then packs the AppDir behind the
[type 2 runtime](https://github.com/AppImage/type2-runtime), which mounts it with FUSE when it runs. All three are
pinned releases, kept under `Build/tools/appimage` and checked against their SHA-256 before each run. `package`
checks the AppDir before it packs it: every rpath relative, and no library missing or loaded from the build. Then it
reports the newest glibc and libstdc++ symbol versions anything in it uses, in `linux-requirements.txt` too.

`AppRun` unsets `QT_PLUGIN_PATH`, since the system's Qt plugins are built for another Qt, and points OpenSSL at the
system's certificates (`SSL_CERT_FILE`), unless the environment already does: the AppImage's OpenSSL looks in
`/etc/ssl`, which isn't where every distribution keeps them, Fedora among them.

The update information in the AppImage, `gh-releases-zsync|ThaRae|xenon-releases|latest|Xenon-*-x86_64.AppImage.zsync`,
tells [AppImageUpdate](https://github.com/AppImageCommunity/AppImageUpdate), and the AppImage managers that use it
(Gear Lever, AppImageLauncher), to update it from the newest release, downloading only the blocks that changed. Xenon
doesn't check for updates itself on Linux (`UI/Updater.cpp`), and the AppImage isn't signed.

### Where it runs

An AppImage brings its own libraries, but not glibc or libstdc++, so it runs on systems whose glibc and libstdc++ are
at least as new as the newest symbol versions it uses. Built in Ladybird's CI image, which is Ubuntu 26.04, that's
`GLIBC_2.43`, `GLIBCXX_3.4.32` and `CXXABI_1.3.15` (Xenon 0.1.0; `package` reports them for each release): glibc 2.43
and the libstdc++ of GCC 13.2 or later. That means Ubuntu 26.04 and later, Fedora 44 and later, and Arch Linux, which
the release workflow tests it on, and other distributions as new; not Fedora 43 (glibc 2.42), Debian 13 (2.41) or
Ubuntu 24.04 (2.39). The system also needs FUSE, as for any AppImage (`fuse3`, which desktop installs have), or
`--appimage-extract-and-run`.

The CPU has to be x86-64-v2 (SSE4.2 and POPCNT): Intel since Nehalem (2008), AMD since Bulldozer (2011). Xenon is
built for that baseline, not for the runner that builds it ([Development.md](Development.md#building)), so it doesn't
need AVX. The macOS release is built for the M1, and runs on any Apple silicon Mac.

Building in an older image would widen that, but Xenon needs Qt 6.10, a compiler and a C++ library new enough for
Ladybird, and Ladybird's Rust toolchain, and only Ladybird's image is tested with all of them; older distributions
have Qt 6.4 (Ubuntu 24.04) or 6.8 (Debian 13), so Qt would have to come from vcpkg, as on macOS. A Flatpak, whose
runtime brings glibc, is the likelier way to older systems.

### Building one on Linux

On Linux, `package` packages the build of `Meta/xenon.py build`, so it needs what that needs, and Qt's Wayland plugin
and geolocation backends, which Ubuntu has in `qt6-wayland` and `libqt6positioning6-plugins`, and binutils. The release
workflow builds in Ladybird's CI image (`Meta/ci/build-in-ladybird-image.sh Meta/ci/package-appimage.sh`), which is
the system the AppImage's requirements come from, so building on a newer one raises them.

```bash
./Meta/release.py package    # writes Build/distribution/Xenon-<version>-x86_64.AppImage and its .zsync file
./Meta/release.py test       # runs Meta/xenon.py test's checks against it
```

### Testing the AppImage

`Meta/release.py test`, on Linux, first checks that the system has every library the AppImage leaves to it, with
every function the AppImage uses from them (`ldd -r`). Then it runs `Meta/xenon.py test`'s checks of a running Xenon
against the AppImage, as it's run once downloaded: it loads and renders `about:welcome`, keeps to Xenon's directories
on a fresh start, blocks what the bundled filter lists block, takes filter list updates, shows the blocking dashboard
and the certificate warning, runs extensions, sends WebAuthn requests to the UI process, installs password managers,
and opens Xenon's windows for session restore. Then it loads `https://example.com` over HTTPS, with the system's
certificates, unless Xenon loaded no page at all, which skips it too. `--appimage <file>` tests another AppImage, and
`--installed <executable>` a Xenon installed from its AppDir, as the [AUR packages](#the-aur-packages) install it.

`Meta/ci/test-appimage.sh <image>` runs it in a container of a distribution's image (Ubuntu or Debian, Fedora, Arch
Linux) with only what a desktop system has: the libraries the AppImage leaves to the system, fonts, certificates, a
D-Bus session, Xvfb and FUSE. Nothing of the build is there, so a library the AppImage misses fails it there rather
than on someone's machine. The release workflow runs it on Ubuntu 26.04, Fedora 44 and Arch Linux.

## The AUR packages

Two packages on the [Arch User Repository](https://aur.archlinux.org) install Xenon on Arch Linux. They're kept in
`Packaging/aur`, a directory each, which is where they're changed; the AUR's copies are pushed from there.

| Package | What it installs |
| --- | --- |
| [`xenon-bin`](https://aur.archlinux.org/packages/xenon-bin) | The release's AppImage, downloaded from ThaRae/xenon-releases and checked against its SHA-256 |
| [`xenon`](https://aur.archlinux.org/packages/xenon) | The release built from source: ThaRae/Xenon's `v<version>` tag, built as the release workflow builds the AppImage |

Both install the AppImage's AppDir in `/usr/lib/xenon`, with `/usr/bin/xenon` a link to its `AppRun`, and the desktop
file (its `Exec` pointed at `xenon`) and the icons where desktops look for them. The AppDir is the same either way, so
Xenon runs with the libraries it brings, as from the AppImage, and the packages depend on what it leaves to the system
([Where it runs](#where-it-runs)) and a font. The two conflict, and `xenon-bin` provides `xenon`. Nothing is stripped,
as in the AppImage. `xenon-bin` unpacks the AppImage with `unsquashfs` from where its runtime ends, which it reads from
the runtime's ELF header, rather than running anything it downloaded.

`xenon` has makepkg clone the tag and Ladybird, and points the `ladybird` submodule at that clone. `Meta/xenon.py build`
then applies the patch series and builds, with vcpkg fetching and building Ladybird's dependencies as it goes, and
`Meta/release.py package` puts the AppDir together, as the release workflow's linux job does. It builds with Arch
Linux's clang and Qt rather than the CI image's, needs the network while it builds (vcpkg, cargo, and the AppImage
tools), and takes hours and tens of gigabytes from a cold start.

Xenon doesn't update itself on Linux, and Settings says to update it the way it was installed. Installed from the AUR,
it updates as other AUR packages do, once the packages there move to the next release: with an AUR helper such as yay,
or by building the new PKGBUILD.

### Keeping them current

```bash
./Meta/aur.py update    # both packages to VERSION.txt's release, with the published AppImage's checksum
./Meta/aur.py check     # each .SRCINFO is what makepkg makes of its PKGBUILD
./Meta/aur.py push      # commit both to their repositories on the AUR, and push them
```

`update` sets `pkgver`, puts `pkgrel` back to 1 for a new version, and sets `xenon-bin`'s checksum from the published
AppImage, which it downloads, so it runs once the release is up (`--appimage <file>` takes a file's checksum instead).
Then it writes each `.SRCINFO`, the AUR's summary of a PKGBUILD, which makepkg writes: pacman's own, on Arch Linux, and
elsewhere one in Arch Linux's container image, with docker. To change a PKGBUILD between releases, bump its `pkgrel`,
and run `update` for the `.SRCINFO`. `Meta/check.py` checks that each `.SRCINFO` has its PKGBUILD's version, and the
AUR workflow that makepkg writes the same one.

`push` clones each package's repository on the AUR, replaces what's there with the package's files as git has them, and
commits and pushes them, as whichever AUR account ssh is set up for. With `--ssh-key <file>`, it uses that key, and
trusts only the AUR's own host key, whose fingerprint is on [aur.archlinux.org](https://aur.archlinux.org). A package
that isn't on the AUR yet is created by its first push. The release workflow does all three once a release is up
([On GitHub's runners](#on-githubs-runners)).

### Testing them

`Meta/ci/test-aur-package.sh <package>` builds a package as an AUR user would, with makepkg in a container of Arch
Linux's own image, after checking its `.SRCINFO`. It checks the package with namcap, whose errors fail the test and
whose warnings it only shows, installs it with pacman, and runs `Meta/release.py test --installed /usr/bin/xenon`:
[the AppImage's checks](#testing-the-appimage), against the Xenon the package installed. The package is left in
`Build/aur`.

`.github/workflows/aur.yml` runs it for `xenon-bin` on pull requests that change the packaging, and for `xenon` by
hand, with **xenon** ticked, or on pull requests that change its PKGBUILD. `xenon`'s job can't start from `main`'s build
caches, which are the CI image's, so it keeps ccache's objects and vcpkg's packages in a cache entry of its own
(`aur-xenon-…`, up to about 2 GB of ccache), saved whether or not the build finished, so that a run that stops at the
time limit leaves the next one less to build.

On Apple silicon, Docker runs the x86-64 container through emulation, where Xenon's helper processes stop at their
sandbox ("Landlock must be applied before the process starts a second thread"), so the checks of a running Xenon only
pass on x86-64 machines, GitHub's runners among them. Building, namcap and installing work either way.

## Signing

Two keys sign a release, and both live in the login keychain of the Mac that releases are made on. Back them up.

### The update key

An Ed25519 key, which signs the archive and the appcast. Sparkle's `generate_keys` made it, under the account `xenon`.
Its public half goes into each release's `Info.plist` as `SUPublicEDKey`. It's separate from the key the
[daily filter lists](FilterListUpdates.md) are signed with, which is a CI secret.

An installed Xenon only takes updates signed with the key it has. Sparkle moves to a new key only through a disk image
signed with the same Developer ID as the installed app, which Xenon's releases don't have, so if the key is lost,
everyone has to install the next release by hand.

### The release certificate

Xenon has no Apple Developer ID. Releases are signed with a self-signed code signing certificate, **Xenon Release**,
rather than ad hoc, because that gives every release the same identity, so macOS keeps what you allowed Xenon, like
location or the microphone, across updates. An ad hoc signature is identified by its hash, which changes with every
release, so macOS would ask again after each update.

Sparkle doesn't hold an update to the certificate: once the update key's signature checks out, it only checks that the
new app's code signature is intact. A new certificate costs one round of those questions, not the updates.

Without notarization, macOS's Gatekeeper still stops the first launch of a Xenon installed from a downloaded
installer. Open it once from
**System Settings → Privacy & Security → Open Anyway**, or run `xattr -dr com.apple.quarantine /Applications/Xenon.app`.
Updates that Xenon installs itself aren't quarantined, so they open without asking.

The engine's helper processes run with the hardened runtime. With a Developer ID, every library in the bundle would
carry the same team identifier, and Ladybird's release entitlements keep library validation on. A self-signed
certificate has no team identifier, so `Meta/release.py` turns library validation off for the helpers, as development
builds do. `--sign "Developer ID Application: …"` keeps it on, and uses secure timestamps; notarizing isn't scripted
yet.

## In Xenon

`UI/Updater.mm` runs Sparkle. Development builds have no appcast URL or key in their `Info.plist`, so they don't
update themselves; Settings says so. `UI/Updater.cpp` stands in elsewhere than macOS, where Xenon doesn't update
itself: AppImage updaters update the AppImage ([The AppImage](#the-appimage)). Sparkle is fetched by
`UI/Xenon.cmake`, as its release at a pinned SHA-256, and goes in `Contents/Frameworks`.

While **Update Xenon automatically** (**Settings → Updates**) and **Let Xenon connect on its own** (**Settings →
Privacy and data**) are both on, Xenon checks when it starts and once a day, downloads a new version in the
background, and installs it when it quits. Settings then says which version is ready, with **Relaunch to update**.
**Check for Updates…** in the Xenon menu, or **Check now** in Settings, checks at once in Sparkle's own window, with
either switch off.

An update that's ready also puts an **Update** button at the end of every window's toolbar (`UI/UpdateButton.cpp`,
[Features.md](Features.md#the-update-button)), and turns **Check for Updates…** into **Relaunch to Update to
<version>**. The button's popover, the menu item and Settings all relaunch through Sparkle's immediate installation
(`Updater::install_and_relaunch()`): Sparkle starts its installer and asks Xenon to quit, which asks every page and any
download that can't resume first, as any quit does, and the installer swaps the app and opens it again. A page that
keeps Xenon open leaves the update to install at the next quit. Before it relaunches, Xenon notes the version it's
installing in its preferences (`XenonRelaunchToUpdateVersion`); the Xenon that starts with that version reopens every
window and tab, whatever the startup setting says, and forgets the note. Private windows don't reopen, so the
popover says so when any are open, and the menu item and Settings ask first (`UI/RelaunchToUpdate.cpp`). **Later**
hides the button until Xenon starts again or a newer version is ready.

Before Sparkle replaces anything, it checks:

1. the appcast's signature (`SURequireSignedFeed`). An appcast whose signature fails is used in Sparkle's safe mode,
   without release notes or links;
2. the archive's Ed25519 signature, against `SUPublicEDKey`, before it unpacks anything
   (`SUVerifyUpdateBeforeExtraction`);
3. that the new app is code signed, and its signature is intact;
4. that it's a newer version.

Its installer waits for Xenon to quit, then swaps the new app in for the old one in a single step, so an update that
fails or is turned down leaves Xenon as it was. Sparkle keeps what it remembers (`SULastCheckTime` and the like) in
Xenon's preferences, `~/Library/Preferences/org.xenon.Xenon.plist`, and the update it downloaded in
`~/Library/Caches/org.xenon.Xenon/org.sparkle-project.Sparkle`.

## Testing

```bash
./Meta/release.py test
./Meta/release.py test --sign "Xenon Release"    # with the release certificate, rather than ad hoc
```

It packages three test releases of the current build, signed with a throwaway key, with an identifier of their own
(`org.xenon.Xenon.UpdateTest`) so that Sparkle's state stays apart from Xenon's. It serves them from a local server laid
out like GitHub's: the appcast at a fixed path, and each archive at `releases/download/v<version>/`, which redirects to
another host name, as github.com does. It runs Xenon with a profile of its own:

1. 0.0.1's installer holds the app, a link to Applications, and its window's layout. The test installs 0.0.1 from it,
   copying the app out as dragging it to Applications does;
2. 0.0.1 finds 0.0.2, downloads it through the redirect, checks it, and installs it when it quits. The installed app's
   signature checks out;
3. 0.0.2 turns down 0.0.3 with a byte changed after it was signed;
4. and 0.0.3 signed with another key.

Step 2 also checks that the toolbar's Update button shows for 0.0.2, with `XENON_TEST_UPDATE_READY` set: a release
ignores it (below). It opens Xenon's windows while it runs, and needs **Update Xenon automatically** on, since Xenon's own
settings aren't the profile's. CI's builds don't run it; the release workflow does, before it packages. Run it before a
release.

### Without a release

Builds that don't update themselves, development builds and Xenon on Linux, take `XENON_TEST_UPDATE_READY=<version>`
as an update of that version that's ready (`UI/Updater.h`): Settings, the Update button and the Xenon menu offer it, and
relaunching only quits, through the same questions as a real relaunch. A release, whose `Info.plist` names an appcast,
ignores it. To see the button in a development build:

```bash
XENON_TEST_UPDATE_READY=9.9.9 ./Meta/xenon.py run
```

`Meta/xenon.py test` runs Xenon in a window that way, and checks that its toolbar shows the button.
`TestXenonUpdateButton` (`Tests/TestUpdateButton.cpp`) puts a stand-in for the updater under the button itself, and
checks that it shows and hides with the update, that its tooltip and popover name the version, that **Relaunch to
Update** installs, that **Later** hides it in every window until a newer version, that the popover warns about private
windows, and that a narrow toolbar gets the arrow alone. With `XENON_SCREENSHOT_DIR` set, it saves how the button and
popover look there.

## Setting up a Mac to release from

The update key and the certificate exist, made on 2026-10-27:

| | |
| --- | --- |
| Update key | Ed25519, under the account `xenon`. Public half `o7cmjh8R2wfWfRfJOTcyLisyoRVEEQIxi7L+SFWXUNI=` |
| Release certificate | **Xenon Release**, self-signed RSA 3072 for code signing, valid until 2036-10-24. SHA-1 `EB4C4127CEA7DA5990952E139293BA15C0BA1D37` |

Their backups (`xenon-update-key.txt`, and `Xenon Release.p12` with its password) are kept outside the repository. To
release from another Mac, build Xenon there (which fetches Sparkle's tools), then import both into its login keychain:

```bash
Build/release/Xenon/Sparkle-*/bin/generate_keys --account xenon -f xenon-update-key.txt
security import "Xenon Release.p12" -k ~/Library/Keychains/login.keychain-db -T /usr/bin/codesign
```

`security find-identity -p codesigning` then lists **Xenon Release** as `CSSMERR_TP_NOT_TRUSTED`, as a self-signed
certificate is, and `codesign` signs with it all the same. `gh` needs to be logged in (`gh auth login`) as an account
that can create releases on ThaRae/xenon-releases and push to it and to ThaRae/Xenon.

### Releasing from GitHub's runners

The release workflow reads the same backups from the repository's secrets. From the folder that holds them:

```bash
base64 -i "Xenon Release.p12" | gh secret set XENON_RELEASE_P12 --repo ThaRae/Xenon
gh secret set XENON_RELEASE_P12_PASSWORD --repo ThaRae/Xenon    # asks for it
gh secret set XENON_UPDATE_KEY --repo ThaRae/Xenon < xenon-update-key.txt
```

Publishing from the workflow also needs `XENON_RELEASES_TOKEN`, since the run's own token can't write to
ThaRae/xenon-releases. Make a fine-grained personal access token at **GitHub → Settings → Developer settings →
Personal access tokens → Fine-grained tokens**, for the repositories ThaRae/Xenon (for the tag) and
ThaRae/xenon-releases (for the release and the appcast), with the **Contents** permission set to **Read and write**.
Then:

```bash
gh secret set XENON_RELEASES_TOKEN --repo ThaRae/Xenon    # asks for it
```

It also pushes the AUR packages' bump to a branch. To have the workflow open the pull request from it too, give the token
the **Pull requests** permission, set to **Read and write**, as well.

When the token expires, make a new one and set the secret again. Only publishing needs it: the workflow builds without
it.

### Pushing to the AUR

The packages go to the AUR over SSH, as an AUR account that maintains them. Once:

1. Make an account at [aur.archlinux.org](https://aur.archlinux.org/register), and a key for it alone:

   ```bash
   ssh-keygen -t ed25519 -f ~/.ssh/aur -N "" -C "Xenon's AUR packages"
   ```

   Add `~/.ssh/aur.pub` to the account, under **My Account → SSH Public Key**.
2. Push the packages for the first time, which creates them on the AUR:

   ```bash
   ./Meta/aur.py push --ssh-key ~/.ssh/aur
   ```

3. For the release workflow to push them from then on:

   ```bash
   gh secret set AUR_SSH_PRIVATE_KEY --repo ThaRae/Xenon < ~/.ssh/aur
   ```

Without the secret, the workflow still moves the packages to each release and tests `xenon-bin`, and the push is left
to `Meta/aur.py push`.

After making a new key or certificate, set its secret again, and change `UPDATE_PUBLIC_KEY` or `CERTIFICATE_SHA1` in
the workflow to match.

If either is ever lost, make a new one: `generate_keys --account xenon` for the key, or a self-signed code signing
certificate named **Xenon Release**, from Keychain Access's Certificate Assistant or OpenSSL. A new certificate only
means macOS asks for Xenon's permissions again once; a new update key means everyone installs the next release by hand
([Signing](#signing)).
