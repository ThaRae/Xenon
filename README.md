# Xenon

Xenon is a web browser built on the [Ladybird](https://ladybird.org) engine. It brings over the features of
[Helium](https://helium.computer): uBlock Origin's ad blocking built in, !bangs in the address bar, a choice of layouts,
and a private, low-noise setup.

It's early. macOS is the main platform; Linux builds and passes its tests in CI.

## What it does

**Tabs and windows**

- Three layouts: tabs above the toolbar, tabs and toolbar in one row, or tabs in a sidebar
- Split view: two tabs side by side in one window
- Tab groups, with names and colors, that collapse
- <kbd>Ctrl</kbd><kbd>Tab</kbd> switches tabs in the order you last used them
- Tabs you haven't used in a while hibernate to free memory, and come back when you select them
- Hover a tab to see how much memory it uses

**Search**

- !bangs: `!w ladybird` searches Wikipedia directly, and there are bangs for about 10,700 sites
- <kbd>⌘</kbd><kbd>⇧</kbd><kbd>C</kbd> copies the current page's address

**Ads and trackers**

- uBlock Origin's filter lists and features built in, scriptlets for YouTube ads and anti-adblock walls included
- A popup and dashboard laid out like uBlock Origin's, with an element picker
- The lists update daily, from a signed source
- Fingerprinting protection inspired by Brave: canvas, audio, and hardware details differ slightly per site and session, and Xenon identifies itself as Chrome

**Password managers**

- Pick Bitwarden, Proton Pass, 1Password, or (on macOS) iCloud Passwords when you first start Xenon, and it installs and updates it for you
- Sign in with a USB security key (WebAuthn, on macOS)
- Extensions stop running in the background when idle, as in Chrome, and start again when needed

**Security**

- A warning for sites whose certificate can't be verified that says why, with a way past it for your own servers

**Setup**

- A welcome flow that asks three questions (search engine, blocking, password manager) or takes Xenon's picks
- A new tab page with your most visited sites
- A settings page with Xenon's own settings first
- Updates itself on macOS, from signed releases, when you quit

[Documentation/Features.md](Documentation/Features.md) describes each feature: how to use it, what it does in edge
cases, and where it lives in the code.

**Not done yet**: frameless mode, restoring split views after a restart, passkeys beyond USB security keys, and Linux
support for security keys.

## Installing it

Each release on [ThaRae/xenon-releases](https://github.com/ThaRae/xenon-releases/releases) has a
`Xenon-<version>.dmg`. Open it and drag Xenon into Applications. Releases aren't notarized yet, so the first time,
macOS asks you to allow it in **System Settings → Privacy & Security → Open Anyway**. After that, Xenon updates
itself.

## Building it

On macOS, with Xcode, CMake 3.30+, and Rust (via rustup) installed:

```bash
brew install autoconf autoconf-archive automake ccache cmake libtool nasm ninja pkg-config
git submodule update --init --depth 1
./Meta/xenon.py run
```

The first build compiles about 74 dependencies, Qt and Skia among them, so it takes a while; later builds are
incremental. The app ends up at `Build/release/bin/Xenon.app`.

## Documentation

| Read | For |
| --- | --- |
| [Features.md](Documentation/Features.md) | What each feature does and where it lives in the code |
| [Development.md](Documentation/Development.md) | How the repo fits together, building, tests and CI, and updating the engine, filter lists, and !bangs |
| [Profile.md](Documentation/Profile.md) | Where Xenon keeps your settings, history, and cache |
| [Extensions.md](Documentation/Extensions.md) | How Chrome extensions run in Xenon, and how far each password manager gets |
| [WebAuthn.md](Documentation/WebAuthn.md) | Security key support |
| [FilterListUpdates.md](Documentation/FilterListUpdates.md) | How the daily filter list updates are built, signed, and checked |
| [Fingerprinting.md](Documentation/Fingerprinting.md) | How fingerprinting protection works, and how it relates to Brave's |
| [Updates.md](Documentation/Updates.md) | How releases are made and signed, and how Xenon updates itself |
| [CLAUDE.md](CLAUDE.md) | The rules for changing Xenon |

## Contributing

Run `./Meta/check.py` before you push (or `git config core.hooksPath .githooks` to have git run it), and
`./Meta/xenon.py test` for the full tests. CI runs both on every pull request. Engine changes go in the patch series in
`Patches/ladybird`, never in the `ladybird/` submodule directly; [Development.md](Documentation/Development.md#changing-the-engine)
explains how.

## License

Xenon's code is BSD-2-Clause, like Ladybird's, and code that came from Ladybird keeps its copyright headers. The filter
lists in `UI/Resources/filter-lists` are third-party data under their own licenses (GPL-3.0, GPL-3.0 or CC BY-SA, and
CC0), listed in their `NOTICE.txt`. The !bang list is Kagi's, under MIT.
