# Extensions

Xenon runs Chrome extensions (WebExtensions, Manifest V3). Password managers come first, since Helium offers three
of them during setup: Bitwarden, then Proton Pass, then 1Password. This document is the design. The status section at
the end says what exists so far.

Ladybird has no extension support. Some of the groundwork is there, though, and the design is shaped around it:

| Ladybird already has | Where | What extensions get from it |
| --- | --- | --- |
| Wrapper worlds: one DOM, a separate set of JS wrappers per world, with a `WrapperWorldType::Extension` kind | `LibWeb/Bindings/WrapperWorld.h`, `Tests/LibWeb/TestWrapperWorld.cpp` | Content scripts that share the page's DOM but not its JS objects. Nothing creates an extension world yet. |
| A per-world `WindowProxy` cache | `BrowsingContext::window_proxy_for()` | A content script's `window` that the page can't reach |
| WebUI: a JSON channel between an internal page and the UI process, one IPC transport per page | `LibWebView/WebUI.cpp`, `WebContent/WebUIConnection.cpp` | The model for extension messaging. It only exists for a hardcoded list of `about:` pages, and only after the page loads, so it can't be used as is. |
| A process per WebUI host and for local file content | `CanonicalNavigable.cpp`, `WebContentClient::hosted_web_ui()` | The model for a process per extension |
| File loads that WebContent asks the UI process for | `ResourceLoader.cpp` (`request_file`) | How `chrome-extension://` files get past the renderer sandbox |
| Dedicated and shared workers, in the WebWorker process | `Services/WebWorker`, `LibWebView/WorkerProcessManager` | A host for the background service worker |
| Service worker registration, but not execution | `LibWeb/ServiceWorker/Job.cpp` rejects with "Run Service Worker" not implemented | Nothing yet. See [Background context](#background-context). |

And it lacks these, each of which needs an engine change:

- **The `chrome-extension` scheme.** `URL::origin()` gives any unknown scheme a new opaque origin, so an extension page
  couldn't use `localStorage`, IndexedDB, or same-origin `fetch()` of its own files. Fetch's scheme fetch rejects the
  scheme outright (`Fetching.cpp`: "must be one of 'about', 'blob', 'data', 'file', 'http', or 'https'"). Secure contexts
  don't recognize it either, and password managers need `crypto.subtle`, which only exists in a secure context.
- **Injecting a script into a document before its own scripts run**, in the page's world or in an isolated one.
  `run_javascript` (WebContentServer.ipc) evaluates in the top-level document's main world, after the fact, and
  can't target a frame. Content scripts in the page's world are out: the page could read everything a password
  manager hands its content script.
- **A native channel to the UI process from an extension context**, available synchronously when the context starts.
- **A background context the browser owns**, with no tab and no document.

## The engine question

Xenon's rule so far is that `ladybird/` is an unmodified upstream checkout. Extensions can't be built that way. The
plan keeps the engine changes few, small, and generic, so that they could go upstream. They give Ladybird an embedder
API much like WebKit's `WKUserScript`, `WKContentWorld`, `WKScriptMessageHandler` and `WKURLSchemeHandler`.
Everything specific to Chrome extensions (manifests, `chrome.*`, permissions, the popup) stays in Xenon's `UI/`.

There were two ways to carry the engine changes. Xenon uses the second, a patch series.

### Option A: a Ladybird fork on Gitea

`tharae/ladybird` on git.avx512.dev, a pull mirror of upstream plus a `xenon` branch with Xenon's commits on top. The
submodule URL points at the fork, pinned to a commit on `xenon`.

- Day to day: engine changes are ordinary commits in `ladybird/`. Build, debug, and `git log` work as usual.
- Upstream sync: rebase `xenon` onto the new upstream `master`, force-push it, bump the submodule. The weekly Upstream
  workflow would rebase onto `master` and build; a conflict fails the job without saying much more.
- Costs: two repositories that have to move together. A Xenon PR that needs an engine change depends on a fork
  commit being pushed first, and the engine half is reviewed somewhere else. The fork has to keep upstream's full
  history for rebases to work, so the shallow submodule clone goes. Force-pushed rebases make old submodule pins
  unreachable unless every rebase is tagged.

### Option B: a patch series in this repo (chosen)

`Patches/ladybird/0001-*.patch`, ..., in `git format-patch` form, applied to the pinned upstream commit at build
time. This is how Helium and ungoogled-chromium carry their Chromium changes.

- Day to day: `Meta/patches.py apply` runs `git am` on the series in `ladybird/`, on a local `xenon-patches` branch, so
  the patches become ordinary commits to edit, amend, or rebase. `Meta/patches.py export` writes them back to
  `Patches/` with `git format-patch`. `Meta/xenon.py build` applies the series when the checkout doesn't have it yet.
- Upstream sync: bump the pin, `Meta/patches.py apply` (three-way), fix up whichever patch stops applying, export. The
  weekly Upstream workflow does the same against `master` and names the patch that broke, so engine drift shows up
  one patch at a time.
- Costs: a layer of tooling, and `Meta/check.py`'s "no local edits in `ladybird/`" check becomes "`ladybird/` is the
  pinned commit plus exactly the patch series". Patches to patches are awkward to review; reviewing the result of
  `export` (a whole regenerated patch file) is the norm.

**Why B.** The engine and frontend halves of a feature land in one Xenon commit and one review. The submodule still
points at a real upstream commit, so `check.py --online` and the shallow clone keep working. Each patch is a
self-contained change with its own commit message, which is the shape an upstream PR needs, and a patch is deleted
outright once upstream has the equivalent. A fork does make engine work a little more direct, but with `apply` and
`export` doing the round trip, B costs about the same while editing and less when syncing.

### The patch series

In order. Each is generic: none mentions Chrome extensions, Xenon, or `chrome.*`.

1. **Embedder URL schemes.** The embedder registers schemes with LibWebView, with three properties each: a tuple
   origin (`scheme://host`), potentially trustworthy (a secure context), and loaded by the UI process. LibURL's
   origin computation, SecureContexts, and Fetch's scheme fetch consult the registry. A request for a registered scheme
   goes from WebContent to the UI process, like a `file://` load does, and the embedder answers it with a status,
   headers, and a file descriptor or bytes, or refuses it. Navigations, subresources, `fetch()`, and workers all get
   it.
2. **User scripts and script worlds.** `ViewImplementation` gains `add_user_script(world, source, url, injection
   time, all_frames, match)` and a per-world message handler. A world is either the main world or a named isolated
   world, built on `WrapperWorldType::Extension`: a realm per document and world, whose global wraps the same `Window`,
   with its own `WindowProxy` from `window_proxy_for()`. Injection times are document start (after the document
   element exists, before any parser-inserted script runs), document end (after parsing, before `DOMContentLoaded`
   handlers), and document idle. Each world gets a native `postMessage(json)` to its handler in the UI process and a
   way for the UI process to call into it. The UI process decides which scripts go into which frames, since it knows
   every navigable's URL, and the WebContent process that hosts the frame runs them.
3. **Embedder-owned workers.** The UI process starts a worker with a given script URL (of a registered scheme) and no
   owning document, in the WebWorker process, with the same user-script and messaging hooks as a world. It lives until
   the UI process closes it.
4. **Embedder-chosen processes.** The embedder can say which origins get a process of their own, generalizing what
   WebUI hosts and `file://` content already get.

Later patches, as the milestones need them: request interception for `webRequest` (in RequestServer, next to the
content blocker), HTTP auth challenges for `webRequestAuthProvider`, and hidden pages with no tab, for offscreen
documents.

## Architecture

```
 UI process (Xenon)                                     WebContent process, one per extension
 ┌──────────────────────────────────────────┐          ┌──────────────────────────────────────┐
 │ ExtensionRegistry                         │          │ chrome-extension://<id>/popup.html   │
 │  ├ Extension (manifest, id, locales)      │  scheme  │  main world: chrome.* (full)         │
 │  ├ ExtensionStorage (per id, on disk)     │◄────────►│                                      │
 │  ├ ExtensionRouter (messages, ports,      │  JSON    └──────────────────────────────────────┘
 │  │   events, sender info)                 │  over    ┌──────────────────────────────────────┐
 │  ├ ExtensionPermissions                   │  IPC     │ WebWorker: background service worker │
 │  └ API handlers: runtime, storage, i18n,  │◄────────►│  chrome.* (full)                     │
 │      tabs, scripting, alarms, ...         │          └──────────────────────────────────────┘
 │ Toolbar: action buttons, popup Popover    │          ┌──────────────────────────────────────┐
 │ Extensions dialog (load, enable, remove)  │◄────────►│ WebContent for https://example.com    │
 └──────────────────────────────────────────┘          │  main world: the page                │
                                                        │  isolated world per extension:       │
                                                        │   content scripts, chrome.* (subset) │
                                                        └──────────────────────────────────────┘
```

The UI process is the browser in Chrome's terms. It owns every extension's state and enforces every permission.
Renderers only carry messages.

### Loading extensions

**Unpacked first.** The Extensions dialog has **Load unpacked…**, and `--load-extension=<dir>` (Chrome's flag) loads
one for a session. Loading:

1. Reads `manifest.json`. `manifest_version` has to be 3 (2 is refused, with a message saying so). Unknown keys are
   kept and ignored, as Chrome does, with a warning per key in the dialog.
2. Computes the ID the way Chrome does, so that IDs other software relies on (native messaging hosts'
   `allowed_origins`, sites that detect an extension) match Chrome's: the first 128 bits of a SHA-256, as 32 letters
   `a`–`p`. The hash is of the decoded `key` if the manifest has one, or of the directory's absolute path if not.
3. Loads `_locales/<default_locale>/messages.json` and the UI language's, and resolves `__MSG_name__` in `name`,
   `description`, `action.default_title`, and the other strings Chrome localizes.
4. Validates paths: every file the manifest names has to exist inside the directory.
5. Records it in the registry (`<profile>/Extensions/registry.json`): ID, path, enabled, granted permissions, and
   whether it is unpacked. Unpacked extensions load from their directory; nothing is copied.

**CRX later.** A CRX3 file is a signed header (a protobuf with the public key and signatures) in front of a zip.
Installing one verifies the signature, derives the ID from the key, unpacks to `<profile>/Extensions/<id>/<version>/`,
and then takes the unpacked path. The Chrome Web Store serves CRX files at a public update URL, and its update
protocol tells Xenon when to fetch a new version. Both come after the password managers work unpacked.

### Serving `chrome-extension://`

The scheme is `chrome-extension`, Chrome's own, since extensions compare URLs against it.

- **Origin:** the tuple `chrome-extension://<id>`. Storage, IndexedDB, and same-origin checks then work as they do in
  Chrome, and each extension's are separate from every other's.
- **Secure context:** yes, so `crypto.subtle`, which password managers depend on, is there.
- **Loading:** WebContent asks the UI process for each URL (patch 1). `ExtensionRegistry` maps the host to an extension
  and the path to a file under its root. It normalizes the path, refuses `..`, symlinks out of the root, and anything
  not a regular file, and opens the file read-only. The MIME type comes from the extension; there is no sniffing.
- **Who may load what:** an extension's own pages may load any of its files. Any other origin may only load files
  listed in `web_accessible_resources` for a `matches` pattern that covers it. With `use_dynamic_url`, those files are
  served only under a per-session random host that `chrome.runtime.getURL()` returns, so a site can't probe for the
  extension.
- **Headers:** every extension page carries the manifest's `content_security_policy.extension_pages`, or Chrome's MV3
  default, `script-src 'self' 'wasm-unsafe-eval'; object-src 'self';`. MV3 forbids remote script sources and
  `'unsafe-eval'` there, so a manifest that asks for them is refused at load. `sandbox.pages` are served with
  `content_security_policy.sandbox` and a `sandbox` CSP directive, which gives them an opaque origin and no `chrome.*`.
- **Process:** each extension's pages share one WebContent process, which hosts nothing else (patch 4).

### Background context

Bitwarden's MV3 manifest declares `background.service_worker`: a classic (not module) script, built for webpack's
`webworker` target. Its Firefox build runs the same code as a background page, so it needs a worker-like global rather
than anything particular to service workers. The one exception is Web Push through `self.registration`, which it
skips when `registration` is missing, falling back to a WebSocket. Ladybird registers service workers but can't run
one. Rather than wait for a full Service Worker implementation, Xenon starts the background script as an
embedder-owned worker (patch 3) at the extension's origin:

- It has a worker global: `self`, `importScripts()`, `fetch()`, IndexedDB, `crypto`, timers, and no DOM, which is what
  MV3 extension code expects. `chrome.*` is installed before the script runs.
- `ServiceWorkerGlobalScope` members extensions touch (`self.registration`, `clients`, `skipWaiting()`, the `install`
  and `activate` events) are filled in by the bootstrap script as far as extensions need them.
- It starts when the extension is enabled and stays running. Chrome stops idle service workers after 30 seconds and
  restarts them on an event. Xenon doesn't at first: an extension that survives being stopped also survives not being
  stopped. Stopping idle workers is a memory optimization for later.
- Once Ladybird runs real service workers, the background script moves to one, and the bootstrap script stays as is.

The background has to come early. Bitwarden's popup won't show its unlock screen until the background has run its
state migrations: it polls `storage.local` for the state version the worker writes, for up to about 16 seconds. It
also opens ports to the worker as it starts.

**Offscreen documents** (`chrome.offscreen`) are extension pages loaded in a hidden view with no tab. Bitwarden uses
one for the clipboard and for a `localStorage` backup of device-trust keys. It picks those code paths by
`manifest_version`, not by checking that `chrome.offscreen` exists, so the API has to be there with the background.
Offscreen documents need hidden views (a later patch).

### Content scripts and isolation

For each navigation, `ExtensionRegistry` matches every enabled extension's `content_scripts` against the frame's URL:
`matches` and `exclude_matches` (match patterns), `include_globs` and `exclude_globs`, `all_frames`, and
`match_about_blank` / `match_origin_as_fallback` for `about:blank` and `blob:` frames. It also checks the extension
has host permission for the URL. Matching scripts go to the frame's WebContent process as user scripts (patch 2) at
the requested `run_at`: `document_start`, `document_end`, or `document_idle` (the default).

- **Isolation:** each extension gets its own isolated world per frame. The world shares the DOM with the page but
  nothing else: its own globals, its own prototypes, and its own wrappers, so expando properties and monkey-patched
  builtins in the page are invisible to it, and the other way round. Ladybird's wrapper worlds already give each world
  its own wrappers for the same DOM object, which is the hard part.
- **`chrome.*` in content scripts:** the subset Chrome gives them: `runtime` (`id`, `getURL`, `getManifest`,
  `sendMessage`, `connect`, `onMessage`, `onConnect`), `storage`, and `i18n`. Nothing else is installed.
- **Content script ↔ page:** through the shared DOM, `window.postMessage`, and DOM events, which is what Bitwarden's
  autofill and inline menu use. A content script's `fetch()` goes out with the page's origin, as in Chrome MV3.
- **`chrome.scripting.executeScript` / `insertCSS`** (and `registerContentScripts`) use the same path, into the
  extension's isolated world by default, or the main world when an extension asks for `world: "MAIN"`.
- **Extension iframes in pages:** Bitwarden's inline autofill menu is an `<iframe credentialless>` of a
  `web_accessible_resources` page (`overlay/menu.html`), in a closed shadow root inside the site. It is an extension
  page with its own origin, process, and full `chrome.*`; the site only sees a cross-origin frame. That page nests a
  sandboxed extension page (`sandbox="allow-scripts"`) and relays `postMessage` between them. Its notification bar
  (`notification/bar.html`) works the same way.

### Messaging

Every `chrome.*` call and every message between extension contexts goes through the UI process. Nothing goes from one
renderer to another directly.

- **Transport:** each extension context (page, worker, or isolated world) has one channel to the UI process, created
  with the context, so it exists before the extension's first line of code runs. Calls are `{id, name, args}`; replies
  are `{id, result}` or `{id, error}`; events are `{event, args}`. Payloads are JSON, which is also what Chrome uses to
  serialize extension messages, so values that don't survive JSON don't survive in Chrome either.
- **`chrome.*` is JavaScript** (`UI/Resources/extensions/`), which Xenon's bootstrap script installs on top of the
  channel. It handles callbacks and promises (MV3 APIs return a promise when no callback is passed), `lastError`,
  event objects (`addListener`, `removeListener`, `hasListener`), and argument checks. The work happens in C++
  handlers in the UI process.
- **Sender identity comes from the UI process.** It knows which channel a message arrived on, and so which extension,
  tab, frame, URL, and origin sent it, and it fills in `MessageSender` itself. A renderer can't claim to be another
  extension or another frame.
- **Routing:** `runtime.sendMessage` goes to every extension page and the background worker of the same extension,
  except the sender, and resolves with the first response. `tabs.sendMessage` goes to that tab's content scripts (one
  frame with `frameId`). `runtime.connect` and `tabs.connect` open ports, which the UI process tracks and closes when
  either end's context goes away (`onDisconnect`). `runtime.onMessageExternal` and `externally_connectable` come later.

### The `chrome.*` API, in milestone order

Ordered by what Bitwarden needs, then by what Proton Pass and 1Password add. Bitwarden's needs come from reading its
source (`bitwarden/clients`, `apps/browser`, version 2026.10.1); most of its calls go through `BrowserApi`
(`platform/browser/browser-api.ts`). "Stub" means the API exists and behaves plausibly without doing anything real
yet, so that an extension's startup doesn't throw. Bitwarden feature-detects `sidePanel`, `management`,
`extension.getViews`, `dom.openOrClosedShadowRoot`, and Web Push, so those can come late.

| Milestone | APIs | For |
| --- | --- | --- |
| **1. Extension pages** | Loading unpacked, `chrome-extension://`, the toolbar button and popup. `runtime`: `id`, `getURL`, `getManifest`, `getPlatformInfo`, `lastError`, `sendMessage`/`onMessage` between pages of one extension. `storage`: `local`, `session`, `sync` (stored locally), `managed` (empty), `get`/`set`/`remove`/`clear`/`getKeys`/`getBytesInUse`, `onChanged` on `chrome.storage` and on each area, `setAccessLevel`. `i18n`: `getMessage`, `getUILanguage`, `getAcceptLanguages`, `__MSG_` in the manifest. | A test extension's popup runs and keeps its state. Bitwarden's popup loads, but waits for the background. |
| **2. Background** | The background service worker. `runtime`: `connect`/`onConnect` and ports (`sender` with `tab`, `frameId`, `url`, `origin`, `documentId`), `onInstalled`, `onStartup`, `getContexts`, `reload`, `openOptionsPage`, `setUninstallURL`. `alarms`. `tabs`: `query` (with URL patterns), `get`, `getCurrent`, `create`, `update`, `remove`, `sendMessage`, `connect`, `captureVisibleTab`, `onActivated`, `onAttached`, `onCreated`, `onDetached`, `onRemoved`, `onReplaced`, `onUpdated`. `windows`: `getAll`, `get`, `getCurrent`, `getLastFocused`, `create` (including `type: "popup"`, for Bitwarden's popout), `update`, `remove`, `onCreated`, `onFocusChanged`, `onRemoved`. `action`: `setIcon`, `setBadgeText`, `setBadgeBackgroundColor`, `setTitle`, `setPopup`, `openPopup`. `commands`. `idle` (with the `locked` state). `permissions`: `contains`, `getAll`, `request`, `onAdded`. `offscreen` and the clipboard. `management.getSelf`. | Bitwarden's popup reaches its unlock screen; Bitwarden unlocks, syncs, and keeps its vault in the background; the badge shows matching logins |
| **3. Content scripts** | Isolated worlds, `content_scripts` (Bitwarden's two, at `document_start`, one in all frames), `web_accessible_resources` (with `use_dynamic_url`), sandboxed pages. `scripting`: `executeScript` (`files`, `func` with results, `allFrames`/`frameIds`, `injectImmediately`, `world: "MAIN"`), `insertCSS`, `removeCSS`, `registerContentScripts`/`unregisterContentScripts` (Bitwarden's passkey support registers a `world: "MAIN"` script). `webNavigation`: `getFrame`, `getAllFrames`, `onCommitted`, `onCompleted`, `onErrorOccurred`, `onBeforeNavigate`. `tabs.sendMessage` with `frameId`. `activeTab`. | Autofill, the inline menu, saving new logins, and passkeys |
| **4. The rest of Bitwarden** | `contextMenus`. `notifications` (with buttons). `webRequest`: `onAuthRequired` with `asyncBlocking` (`webRequestAuthProvider`, for HTTP auth dialogs), and the non-blocking `onBeforeRequest`, `onBeforeRedirect`, `onHeadersReceived` (with `responseHeaders`), `onCompleted`, `onErrorOccurred`. `sidePanel`. `privacy.services` `passwordSavingEnabled`, `autofillAddressEnabled`, `autofillCreditCardEnabled` (stubs: Ladybird has no password manager or autofill of its own to turn off). `nativeMessaging` (`connectNative("com.8bit.bitwarden")`), for unlocking through the desktop app. Web Push for the background. | Feature parity with Bitwarden in Chrome |
| **5. Installing from the store** | CRX3 install and updates, the Chrome Web Store, offering password managers in the welcome flow as Helium does. Then whatever Proton Pass and 1Password use beyond the above, found the same way Bitwarden's needs were. | The setup flow Helium has |

### Where each piece lives

| Piece | Engine patch | Xenon |
| --- | --- | --- |
| `chrome-extension://` origin, secure context, loading | 1: scheme registry in LibURL, SecureContexts, Fetch, ResourceLoader, LibWebView | `UI/Extensions/ExtensionRegistry`: resolves URLs to files, adds CSP, checks web-accessible resources |
| Manifest, ID, locales, `__MSG_` | | `UI/Extensions/ExtensionManifest`, `ExtensionLocales` |
| Isolated worlds and injection timing | 2: `WrapperWorld` realms, user scripts, injection points in the HTML parser and document lifecycle | Content-script matching (`UI/Extensions/MatchPattern`) |
| The channel to each context | 2 and 3: a per-context IPC endpoint like WebUI's, created with the context | `UI/Extensions/ExtensionRouter`: calls, events, ports, sender info |
| Background worker | 3: embedder-owned workers | Starts and stops it with the extension |
| Process per extension | 4 | Asks for it per extension origin |
| `chrome.*` | | `UI/Resources/extensions/*.js` (the API surface), `UI/Extensions/Api*.cpp` (handlers) |
| `chrome.storage` | | `UI/Extensions/ExtensionStorage`: a JSON file per extension and area under `<profile>/Extensions/Storage/<id>/`; `session` is in memory |
| Toolbar and popup | | `UI/Extensions/ExtensionActionButton`, using `UI/Popover` and a `WebContentView` |
| Managing extensions | | `UI/Extensions/ExtensionsDialog` (Qt) at first. An `about:extensions` page would need LibWebView's hardcoded WebUI list to grow, so it waits. |

### The toolbar and the popup

- Each enabled extension with an `action` gets a toolbar button after the address bar, with `action.default_icon`
  (the size closest to the toolbar's, for the screen's scale) and `default_title` as the tooltip. The badge
  (`setBadgeText`, `setBadgeBackgroundColor`) is drawn on the icon, in the extension's color if it set one and
  Xenon's rose otherwise.
- Clicking it opens `action.default_popup` in a `Popover` anchored to the button, as the content blocking popover is,
  hosting a `WebContentView` of the popup page. The popover sizes itself to the page's content, between 25×25 and
  800×600 as in Chrome. It closes when it loses focus, on Escape, or when the page calls `window.close()`.
- With no popup, a click fires `action.onClicked` in the background worker and grants `activeTab`.
- `commands` become shortcuts, with `_execute_action` opening the popup. Chrome's defaults apply unless they clash
  with Xenon's own (⌘⇧C copies the URL).
- The chrome uses Xenon's palette from `UI/BrandColors.h`; the popup page draws itself.

### Security

- **Permissions** are granted at load. Unpacked extensions are developer-loaded, so loading one grants its required
  `permissions` and `host_permissions`, as Chrome does. CRX installs (milestone 5) ask first, listing what the
  extension can do in plain words. `optional_permissions` need `permissions.request()` and a user gesture, and ask.
  Every API handler checks the calling extension's permissions in the UI process; a missing `chrome.foo` in the
  bootstrap is a convenience, not the check.
- **Host access** is checked at injection time and per call (`scripting`, `tabs` URL fields, `webRequest`).
  `activeTab` grants the current tab's origin until it navigates.
- **Isolation:** content scripts in isolated worlds, extension pages in their own process with their own origin, and
  sender identity from the UI process. A compromised site's renderer can't reach extension pages, which live in
  another process, and can only send messages as the content scripts in its own frames.
- **CSP:** extension pages get MV3's policy and can't loosen it beyond what MV3 allows. The page's CSP doesn't stop
  content scripts from running, since the UI process injects them, but what they insert into the page (a `<script>`,
  an `<img>`) is still subject to it, as in Chrome.
- **Storage** is per extension ID, in the profile, and deleted with the extension.
- **Native messaging** (milestone 4) runs only hosts registered in Chrome's manifest locations whose
  `allowed_origins` list the extension, over stdin/stdout with Chrome's length-prefixed framing.
- **No `file://` access** for content scripts unless the user turns it on per extension, as in Chrome.

## Testing

- Unit tests (`Tests/TestExtension*.cpp`, run by `Meta/xenon.py test`): manifest parsing and validation, IDs (against
  IDs Chrome computes for the same keys), locales and `__MSG_`, match patterns, URL-to-file resolution including path
  traversal, and storage.
- `Tests/Extensions/` holds small test extensions. `Meta/xenon.py test` loads one headlessly with
  `--load-extension`, opens its page, and checks what its scripts report through `chrome.*` and storage.
- Real extensions: Bitwarden's unpacked Chrome MV3 build (`npm run build:chrome` in `bitwarden/clients/apps/browser`)
  is the target for milestones 1 to 4, checked by hand against a test vault.

## Status

- [x] Engine strategy: a patch series in `Patches/ladybird`, applied by `Meta/patches.py`.
- [ ] Milestone 1: extension pages
- [ ] Milestone 2: background
- [ ] Milestone 3: content scripts
- [ ] Milestone 4: the rest of Bitwarden
- [ ] Milestone 5: installing from the store
