# Updates and releases

Xenon updates itself on macOS with [Sparkle 2](https://sparkle-project.org), as Helium does. Releases are built on a
Mac with `Meta/release.py` and published as GitHub releases of
[ThaRae/xenon-releases](https://github.com/ThaRae/xenon-releases), a repository of their own.

- [The published releases](#the-published-releases)
- [Making a release](#making-a-release)
- [Signing](#signing)
- [In Xenon](#in-xenon)
- [Testing](#testing)
- [Setting up a Mac to release from](#setting-up-a-mac-to-release-from)

## The published releases

Each version is a GitHub release of ThaRae/xenon-releases, tagged `v<version>`, with one file:
`Xenon-<version>.tar.xz`, which holds `Xenon.app`. The repository's `main` holds `appcast.xml`, Sparkle's feed, which lists the newest release:

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
   - `Xenon-<version>.tar.xz`, signed with the update key;
   - `appcast.xml`, which lists it, signed too.
3. Try `Build/distribution/Xenon.app`.
4. `./Meta/release.py publish` asks, then creates the GitHub release with the archive (`gh release create`), commits
   the appcast to ThaRae/xenon-releases' `main`, and tags the commit `v<version>` in ThaRae/Xenon. It publishes the
   appcast only once the archive it names is up. It refuses a tree with uncommitted changes, and a commit that isn't
   on ThaRae/Xenon's `main`. It pushes over HTTPS with `gh`'s credentials, whatever Xenon's `origin` is.

### On GitHub's runners

`.github/workflows/release.yml` does step 2 on one of GitHub's `macos-26` runners, when it's run by hand, from
**Actions → Release → Run workflow** or with:

```bash
gh workflow run release.yml --repo ThaRae/Xenon --ref main
```

It also runs on pull requests that change it. Before it builds, it checks the secrets: the certificate's SHA-1 and the
update key's public half must be the ones [below](#setting-up-a-mac-to-release-from), so a secret holding another one
stops the run at once. It builds Xenon as CI's macOS job does, starting from the build caches `main`'s builds saved. A
cold build doesn't fit in one run, so with no caches to start from, it stops there. It runs `Meta/release.py test` with
the release certificate (untick **test** to skip it), then `Meta/release.py package`, and checks what that wrote: the
archive's and the appcast's signatures, with `sign_update --verify`, the app's `SUPublicEDKey`, and its code signature.

`Xenon-<version>.tar.xz` and `appcast.xml` are the run's artifact, `Xenon-<version>`, for 30 days, with the archive's
SHA-256 in the run's summary. The workflow doesn't publish: that's still `Meta/release.py publish`, from the Mac's
`Build/distribution`. The certificate and the update key are the repository's secrets
([Releasing from GitHub's runners](#releasing-from-githubs-runners)). The run imports the certificate into a keychain
of its own, and deletes the keychain and the key's file when it's done.

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

Without notarization, macOS's Gatekeeper still stops the first launch of a downloaded Xenon. Open it once from
**System Settings → Privacy & Security → Open Anyway**, or run `xattr -dr com.apple.quarantine /Applications/Xenon.app`.
Updates that Xenon installs itself aren't quarantined, so they open without asking.

The engine's helper processes run with the hardened runtime. With a Developer ID, every library in the bundle would
carry the same team identifier, and Ladybird's release entitlements keep library validation on. A self-signed
certificate has no team identifier, so `Meta/release.py` turns library validation off for the helpers, as development
builds do. `--sign "Developer ID Application: …"` keeps it on, and uses secure timestamps; notarizing isn't scripted
yet.

## In Xenon

`UI/Updater.mm` runs Sparkle. Development builds have no appcast URL or key in their `Info.plist`, so they don't
update themselves; Settings says so. `UI/Updater.cpp` stands in elsewhere than macOS. Sparkle is fetched by
`UI/Xenon.cmake`, as its release at a pinned SHA-256, and goes in `Contents/Frameworks`.

While **Update Xenon automatically** (**Settings → Updates**) and **Let Xenon connect on its own** (**Settings →
Privacy and data**) are both on, Xenon checks when it starts and once a day, downloads a new version in the
background, and installs it when it quits. Settings then says which version is ready, with **Relaunch to update**.
**Check for Updates…** in the Xenon menu, or **Check now** in Settings, checks at once in Sparkle's own window, with
either switch off.

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

1. 0.0.1 finds 0.0.2, downloads it through the redirect, checks it, and installs it when it quits. The installed app's
   signature checks out;
2. 0.0.2 turns down 0.0.3 with a byte changed after it was signed;
3. and 0.0.3 signed with another key.

It opens Xenon's windows while it runs, and needs **Update Xenon automatically** on, since Xenon's own settings aren't
the profile's. CI's builds don't run it; the release workflow does, before it packages. Run it before a release.

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

After making a new key or certificate, set its secret again, and change `UPDATE_PUBLIC_KEY` or `CERTIFICATE_SHA1` in
the workflow to match.

If either is ever lost, make a new one: `generate_keys --account xenon` for the key, or a self-signed code signing
certificate named **Xenon Release**, from Keychain Access's Certificate Assistant or OpenSSL. A new certificate only
means macOS asks for Xenon's permissions again once; a new update key means everyone installs the next release by hand
([Signing](#signing)).
