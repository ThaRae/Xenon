# Xenon's features

What each feature does, how to reach it, and where it lives in the code. The [README](../README.md) has the short
version. Xenon brings over [Helium](https://helium.computer)'s feature set, measured against Helium, in its own look.

- [Setup and internal pages](#setup-and-internal-pages): welcome flow, new tab page, settings
- [Tabs and windows](#tabs-and-windows): layouts, split view, tab groups, recently used order, hibernation, memory use
- [Search and the address bar](#search-and-the-address-bar): !bangs, quick link copy
- [Ads and trackers](#ads-and-trackers): uBlock Origin's filters and engine features, its popup and dashboard, daily
  updates
- [Password managers and extensions](#password-managers-and-extensions): Chrome extensions, Bitwarden and Proton Pass,
  security keys
- [Updates](#updates): Xenon updating itself
- [Look](#look): palette and icon
- [Not done yet](#not-done-yet)

Where Xenon keeps its data is in [Profile.md](Profile.md).

## Setup and internal pages

### Welcome and setup

On first run (or any time at `about:welcome`), either take Xenon's picks (DuckDuckGo, with blocking on) or answer the
questions:

- which search engine, from a private/mainstream list with a live preview of where a search goes;
- whether to block ads and trackers (on by default, with all the filter lists);
- which password manager, if any: Bitwarden or Proton Pass, as Helium's setup offers.

Nothing is saved until you finish.

### New tab page

Your eight most visited sites, one per site with its favicon, ranked by Ladybird's decaying visit score. Private
windows say so instead and show no history. The page is static, so Xenon's UI hands it the sites once it has loaded
(`UI/NewTabPage.cpp`).

### Settings

`about:settings` (**Settings** in the menu) puts Xenon's own settings first, as Helium's does, and keeps Ladybird's
below them. Every change applies at once, and the page follows changes made from the menus.

- Xenon's sections:
  - **Look and layout**: the three layouts, the sidebar's side, the bookmarks bar, zoom, dark pages.
  - **Tabs**: recently used order, hibernation and its timeout, dragging a tab to a page's edge to split, groups.
  - **Search and bangs**: the engine, suggestions, bangs on or off, and a lookup of all 13,000.
  - **Ads and trackers**: blocking, which lists are in use and when they were last checked, daily updates with
    **Check now**, subscriptions and local lists, and links into the blocking dashboard.
  - **Password manager** and **Extensions**.
  - **Updates**: the version, whether a new one is ready, automatic updates, and **Check now** or **Relaunch to
    update**.
- Browsing: startup and the new tab page, languages, privacy and browsing data, background connections, site
  permissions, DNS, where the profile keeps its files (each with **Show**), and Ladybird's configuration keys.
- A sidebar of sections that follows the scroll, and **Find a setting**, which hides everything that doesn't match.

The page is Xenon's `UI/Resources/about-pages/settings.*`. It runs Ladybird's own settings scripts, by their element
ids, for Ladybird's settings. `UI/SettingsPage.cpp` answers it about Xenon's (`UI/XenonSettings`), through engine patch
`0022` (`LibWebView: Let the embedder add interfaces to its WebUI pages`), which lets the browser give WebUI pages
interfaces of its own (`UI/WebUIInterfaces.cpp`).

## Tabs and windows

### Layouts

Right-click the tab strip or window frame and pick **Layout → Classic / Compact / Vertical Tabs**, or pick one in
**Settings → Look and layout**.

- **Classic**: the tab strip above the toolbar.
- **Compact**: the toolbar and the tabs in one row.
- **Vertical**: tabs in a collapsible, resizable sidebar (from Ladybird).

### Split view

Two tabs side by side in one window, each with its own page and history (`UI/SplitView.cpp`).

- **Opening one**: from a tab's context menu (**New Split View with Current Tab**, or **Add Tab to New Split View** on
  the current tab), with <kbd>⌘</kbd><kbd>⌥</kbd><kbd>N</kbd> (<kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>N</kbd> elsewhere),
  with **Open Link in Split View**, or by holding a dragged tab on the left or right edge of a page for half a second.
  **Settings → Tabs** turns off the dragging.
- **Using it**: the two tabs sit together in the tab strip. The active page has a rose ring and the toolbar; click the
  other page to switch. The inactive page's corner names it and has the split view's menu.
- **Changing it**: drag the divider to resize (it snaps to the middle), or double-click it to swap the pages.
  **Reverse Views**, **Close Left/Right View** and **Separate Views** are in the split button left of the address bar
  and in the tab's context menu. Closing either tab leaves the other as a normal tab.

Split views don't come back after a restart yet.

### Tab groups

Right-click a tab and pick **Add Tab to New Group** (or **Add Tab to Group** when there are groups already).

- A group has a title and one of nine colors picked to sit with the rose. It shows as a chip before its tabs with a
  line below them, or as a header row above them in the sidebar.
- Click the chip to collapse or open the group. Right-click it to rename, recolor, ungroup, or close the group, open a
  new tab in it, or move it to a new window.
- Drag a tab between two of a group's tabs to add it, or out of the group to take it out. Tabs opened from a grouped
  tab join its group.
- Ctrl+Tab passes over collapsed groups, and groups come back with a restored session.

The model is `UI/TabGroups.cpp`.

### Recently used tab order

Hold <kbd>Ctrl</kbd> and press <kbd>Tab</kbd> to walk back through a window's tabs in the order you last used them, and
let go to stay on one. One quick press flips between the last two tabs, and <kbd>Ctrl</kbd><kbd>⇧</kbd><kbd>Tab</kbd>
walks the other way, from the least recently used tab. On macOS it's the Control key, as in Helium.

- Tabs opened in the background join the end of the order.
- As in Helium, there's no switcher overlay: each press shows its tab.
- The other next and previous tab shortcuts (<kbd>Ctrl</kbd><kbd>PgDn</kbd>, and <kbd>⌘</kbd><kbd>⇧</kbd><kbd>]</kbd>
  on macOS) still follow the tab strip. So does Ctrl+Tab, if you untick **Cycle Tabs in Recently Used Order** in the
  tab strip's context menu or in **Settings → Tabs**.

### Tab hibernation

Xenon's take on the Memory Saver that Helium has from Chromium. A background tab left unused for two hours gives up its
WebContent process, and its favicon turns grey. It keeps its title, favicon, URL, back/forward history, and scroll
position, and reloads when you select it.

A tab never hibernates while it's:

- a window's current tab, or pinned;
- playing audio, or was a minute ago;
- showing picture-in-picture, or has DevTools open;
- waiting on a dialog;
- holding text typed into a form and not sent.

Right-click the tab strip (or open **Settings → Tabs**) for **Tab Hibernation → Off** or a timeout from 15 minutes to 6
hours, or right-click a background tab for **Hibernate Tab**. The code is `UI/TabHibernation.cpp`, with engine patch
`0021`, which saves the scroll position first.

### Memory use on hover

As in Chromium's tab hover cards: hovering a tab, the current one included, shows "Memory usage: 153.8 MB".

- That's the footprint of the processes serving the tab: its WebContent process, the processes of its cross-site
  frames, the workers only it runs, and their image decoders.
- A process that several tabs share, as a page and the popup it opened can, is split between them, and the card says
  "about".
- A hibernated tab uses none, and its card says how much hibernating it freed.

It's measured as the card shows, at most once a second (`UI/TabMemory.cpp`, with engine patch `0022`, which lets the UI
see which tab a worker belongs to).

## Search and the address bar

### !bangs

`!w ladybird` or `ladybird !w` goes straight to Wikipedia's results, and `!gh` alone opens GitHub. Bangs resolve
locally, with no request to a search engine, from Kagi's list of about 13,400 triggers for 10,700 sites (see
[Updating the !bangs](Development.md#updating-the-bangs)).

- While a bang at the start of the address is typed, a chip names its site, as Chrome does in keyword mode. The chip
  appears once the space after the bang is typed.
- Backspace at the start of the text takes the chip away, leaving the bang as plain text, so Enter then searches the
  text as typed.
- A trailing bang like `ladybird !w` resolves on Enter, with no chip.
- **Settings → Search and bangs** turns bangs off, and looks one up by its trigger or its site's name.

### Quick link copy

<kbd>⌘</kbd><kbd>⇧</kbd><kbd>C</kbd> copies the current page's URL. DevTools stays on
<kbd>⌘</kbd><kbd>⇧</kbd><kbd>I</kbd> and <kbd>F12</kbd>.

## Ads and trackers

### uBlock Origin's filter lists, built in

The lists Helium's uBlock Origin fork turns on (uBlock filters, EasyList, EasyPrivacy, AdGuard, URLhaus, and the
cookie banner and annoyance lists), converted for Ladybird's content blocker by
[xenon-ublock](https://github.com/ThaRae/xenon-ublock). They keep 99.3% of their filters (378,644 of 381,140), with
uBlock Origin's scriptlets and redirect resources.

They block ads, trackers, malware sites, and cookie banners whenever content blocking is on, which the welcome flow
turns on by default. They ship with Xenon in `UI/Resources/filter-lists`, and update daily (below). Ladybird's own
downloadable catalog stays off, since it holds raw copies of the same lists.

### uBlock Origin's engine features

Scriptlets (YouTube ads, anti-adblock walls), `$redirect` surrogates, `$removeparam`, popup blocking, procedural
hiding (`:has-text()`), CNAME uncloaking, and an element picker (**Block Element...** in a page's context menu). They
follow the same on/off switch and per-site toggle as the rest of blocking.

They come from engine patches `0004`–`0009`, from xenon-ublock. What's still missing is in xenon-ublock's
`docs/gap-analysis.md`.

### uBlock Origin's popup and dashboard

The toolbar shield opens a popup laid out like uBlock Origin's:

- a power button for the site;
- what was blocked on the page and since install, and the domains the page connected to;
- the element picker and zapper;
- switches that block a site's pop-ups or remote fonts.

Its dashboard, `xenon://blocking/`, has the filter lists (each can be turned off), My filters, and trusted sites.
**Settings → Ads and trackers** links to each.

uBlock Origin's switches for large media, cosmetic filtering and JavaScript, and which domains had requests blocked,
need engine work.

### Daily filter list updates

[xenon-filter-lists](https://git.avx512.dev/tharae/xenon-filter-lists) rebuilds the lists every day and signs them. Once
a day Xenon downloads the lists that changed, checks them against the signed manifest, and switches to them without a
restart. It falls back to the bundled lists if anything fails.

The welcome flow and **Settings → Ads and trackers** turn updates off. Settings also says when the lists in use were
made and last checked, and has **Check now**. See [FilterListUpdates.md](FilterListUpdates.md).

## Password managers and extensions

### Chrome extensions

Manifest V3 extensions, for password managers first, as Helium offers Bitwarden, Proton Pass, and 1Password. This is
still in progress; [Extensions.md](Extensions.md) has the details.

- Unpacked extensions load from the Extensions dialog or with `--load-extension=<dir>`, get a toolbar button and
  popup, and run their background service worker.
- They have `chrome.runtime` (with ports), `storage`, `i18n`, `alarms`, `tabs`, `windows`, `action`, `commands`, and
  `offscreen`, and fetch from the hosts they have permission for.
- Their content scripts run in isolated worlds of their own in web pages, and `chrome.scripting` injects scripts and
  stylesheets on request.

### Bitwarden and Proton Pass

The welcome flow offers both, as Helium's setup does. Xenon installs the one picked from the Chrome Web Store, after
checking its signatures, and keeps it up to date. **Settings → Password manager** and the Extensions window switch or
remove it.

- **Bitwarden** logs in (with a security key too) and autofills logins.
- **Proton Pass** signs in through Proton's account page, unlocks with its extra password, autofills logins, and saves
  new ones.

### Security keys (WebAuthn)

On macOS, sites and password managers can register a USB security key and sign in with it
(`navigator.credentials.create()` and `get()`), PINs included, and in cross-origin frames that are allowed to. A prompt
over the page says what the key needs. It comes from engine patches `0010`–`0015`; see [WebAuthn.md](WebAuthn.md).

## Updates

On macOS, Xenon checks for a new version when it starts and once a day, downloads it in the background, and installs it
when you quit, as Helium does, with Sparkle. **Settings → Updates** says when a new version is ready and has **Relaunch
to update**, and turns automatic updates off. **Check for Updates…** in the Xenon menu checks at once.

Releases come from [tharae/xenon-releases](https://git.avx512.dev/tharae/xenon-releases). Xenon only installs one
whose archive is signed with its update key, from a signed appcast, and the installer swaps the new app in at once, so
a failed update leaves the old one as it was. Development builds,
and Xenon elsewhere than macOS, don't update themselves. `UI/Updater.mm` runs Sparkle, and `Meta/release.py` makes and
publishes the releases; see [Updates.md](Updates.md).

## Look

- Xenon's own palette, built around **#ff0050**: `UI/BrandColors.h` for the chrome, and
  `UI/Resources/about-pages/webui.css` for the internal pages.
- Xenon's own app icon: two orbits woven around a nucleus, in white and rose. Internal pages and the Task Manager show
  it in place of Ladybird's logo.

## Not done yet

- **Frameless mode**: hiding all chrome, so the page fills the window.
- **Split views after a restart**: they don't come back yet.
- **Security keys, next**: PIN setup and passkey management, and Linux. Platform passkeys (iCloud Keychain, Touch ID,
  phones) need an entitlement from Apple.
- **More extensions**: beyond what password managers need; see [Extensions.md](Extensions.md).
- **Web app installs**, which depend on Chromium in Helium, are out of scope until Ladybird supports them.
