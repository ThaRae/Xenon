# Updates and releases

Xenon updates itself on macOS with [Sparkle 2](https://sparkle-project.org), as Helium does. Releases are built on a
Mac with `Meta/release.py` and published to [tharae/xenon-releases](https://git.avx512.dev/tharae/xenon-releases), a
public repository on git.avx512.dev, so that Xenon can fetch them without an account. Xenon itself stays private.

- [The published releases](#the-published-releases)
- [Making a release](#making-a-release)
- [Signing](#signing)
- [In Xenon](#in-xenon)
- [Testing](#testing)
- [Setting up a Mac to release from](#setting-up-a-mac-to-release-from)

## The published releases

Each version is a release of tharae/xenon-releases, tagged `v<version>`, with one file: `Xenon-<version>.tar.xz`,
which holds `Xenon.app`. The repository's `main` holds `appcast.xml`, Sparkle's feed, which lists the newest release:

| In the appcast | What it is |
| --- | --- |
| `sparkle:version`, `sparkle:shortVersionString` | The version, which is `CFBundleVersion` and `CFBundleShortVersionString` in the app |
| `sparkle:minimumSystemVersion`, `sparkle:hardwareRequirements` | The macOS it needs (the build's deployment target), and `arm64` |
| `enclosure` | The archive's URL, size, and `sparkle:edSignature`, its Ed25519 signature |
| The comment at the end | The appcast's own Ed25519 signature, from Sparkle's `sign_update` |

Xenon reads `https://git.avx512.dev/tharae/xenon-releases/raw/branch/main/appcast.xml`, and downloads the archive from
the release. Gitea takes attachments up to 100 MB, and the archive is about 57 MiB.

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
4. `./Meta/release.py publish` asks, then creates the release with the archive (through `tea`, logged in as `avx512`),
   commits the appcast to tharae/xenon-releases (over the same SSH host as Xenon's `origin`), and tags the commit
   `v<version>` in Xenon. It publishes the appcast only once the archive it names is up, and refuses a tree with
   uncommitted changes.

CI runs on Linux, so releases are made on a Mac. A macOS runner could run the same script.

## Signing

Two keys sign a release, and both live in the login keychain of the Mac that releases are made on. Back them up: an
installed Xenon only takes updates signed with them, so if they're lost, everyone has to install the next release by
hand.

### The update key

An Ed25519 key, which signs the archive and the appcast. Sparkle's `generate_keys` made it, under the account `xenon`.
Its public half goes into each release's `Info.plist` as `SUPublicEDKey`. It's separate from the key the
[daily filter lists](FilterListUpdates.md) are signed with, which is a CI secret.

### The release certificate

Xenon has no Apple Developer ID. Releases are signed with a self-signed code signing certificate, **Xenon Release**,
rather than ad hoc, because that gives every release the same identity:

- macOS keeps what you allowed Xenon, like location or the microphone, across updates. An ad hoc signature is
  identified by its hash, which changes with every release, so macOS would ask again after each update.
- Sparkle checks that each update is signed with the same certificate as the Xenon it replaces.

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
3. the new app's code signature, and that it's signed with the same certificate as the running one;
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
(`org.xenon.Xenon.UpdateTest`) so that Sparkle's state stays apart from Xenon's. It serves them from a local server,
and runs Xenon with a profile of its own:

1. 0.0.1 finds 0.0.2, downloads and checks it, and installs it when it quits. The installed app's signature checks
   out;
2. 0.0.2 turns down 0.0.3 with a byte changed after it was signed;
3. and 0.0.3 signed with another key.

It opens Xenon's windows while it runs, and needs **Update Xenon automatically** on, since Xenon's own settings aren't
the profile's. CI can't run it, so run it before a release.

## Setting up a Mac to release from

Once, on the Mac releases are made on, after building Xenon (which fetches Sparkle's tools):

```bash
# The update key, in the login keychain. Note the public key it prints.
Build/release/Xenon/Sparkle-*/bin/generate_keys --account xenon
# Its backup. Keep it somewhere safe, like a password manager, and delete the file.
Build/release/Xenon/Sparkle-*/bin/generate_keys --account xenon -x xenon-update-key.txt
```

The release certificate: in **Keychain Access → Certificate Assistant → Create a Certificate…**, name it
`Xenon Release`, with **Identity Type** *Self-Signed Root* and **Certificate Type** *Code Signing*. Export it with its
private key (**File → Export Items…**, as a `.p12`), and keep that somewhere safe too.

`tea` needs a login named `avx512` that can create releases on tharae/xenon-releases.
