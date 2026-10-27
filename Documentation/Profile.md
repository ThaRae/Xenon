# Where Xenon keeps its data

Xenon keeps its settings, history, cookies, extensions, cache, and crash reports apart from Ladybird's, so the two can be
installed side by side. The default profile is split across the platform's usual directories, each in a `Xenon` folder
of its own:

| What | macOS | Linux |
| --- | --- | --- |
| Settings and bookmarks | `~/Library/Preferences/Xenon/Profiles/default` | `~/.config/Xenon/Profiles/default` |
| History, cookies, sessions, extensions | `~/Library/Application Support/Xenon/Profiles/default` | `~/.local/share/Xenon/Profiles/default` |
| HTTP cache | `~/Library/Caches/Xenon/Profiles/default` | `~/.cache/Xenon/Profiles/default` |
| The running browser's lock and socket | `~/Library/Application Support/Xenon/Profiles/default` | `$XDG_RUNTIME_DIR/Xenon/Profiles/default` |
| Crash reports | `~/Library/Application Support/Xenon/CrashReports` | `~/.local/share/Xenon/CrashReports` |
| Xenon's own settings (layout and the like) | `~/Library/Preferences/com.xenon.Xenon.plist` | `~/.config/Xenon/Xenon.conf` |

**Settings → Profile and files** lists the running profile's directories, with a button that shows each one.

On macOS, the updater keeps its state in `~/Library/Preferences/org.xenon.Xenon.plist`, and the update it downloaded in
`~/Library/Caches/org.xenon.Xenon` ([Updates.md](Updates.md)).

## Moving it

`XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME` move these directories, on macOS too. Two options change the
profile:

- `--profile <name>` uses `Profiles/<name>` instead of `Profiles/default`.
- `--profile-path <dir>` keeps a whole profile in `config/`, `data/`, `cache/` and `runtime/` under one directory,
  which the tests use.

Crash reports and Xenon's own settings stay where they are with either. Ladybird's engine still names its database in
the profile `Ladybird.db`.

## Profiles from earlier builds

LibWebView put every profile under `Ladybird`, until engine patch `0020` let the browser name the directory. Xenon
names it in `UI/main.cpp`, before anything reads it.

Xenon doesn't bring over the profile that earlier builds kept in Ladybird's directories. Nothing there says whether
Xenon or Ladybird wrote it, so moving it could take Ladybird's history and cookies away, and copying it would give them
to Xenon. Xenon starts on a new profile instead, with the welcome flow. If only Xenon ever used the old one, move it
yourself before this version of Xenon first starts (on Linux, with `~/.config`, `~/.local/share` and `~/.cache`):

```bash
for root in ~/Library/Preferences ~/Library/"Application Support" ~/Library/Caches; do mkdir -p "$root/Xenon/Profiles" && mv -n "$root/Ladybird/Profiles/default" "$root/Xenon/Profiles/"; done
```
