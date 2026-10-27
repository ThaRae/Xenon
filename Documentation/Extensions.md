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
- **A background context the browser owns**, with no tab. This one turned out not to need a change of its own: a
  hidden page in a `HeadlessWebView` runs the background script as a dedicated worker.

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

`Patches/ladybird/`, in order. Each is generic: none mentions Chrome extensions, Xenon, or `chrome.*`.

1. **Embedder URL schemes** (`0001`, done). `WebView::register_url_scheme()` registers a scheme the UI process serves.
   Its URLs get tuple origins (`scheme://host`), are potentially trustworthy (secure contexts), and are fetch schemes,
   with CORS requests allowed. LibURL's origin computation, SecureContexts, and Fetch consult a registry
   (`LibURL/EmbedderSchemes.h`), which renderers learn from `--embedder-scheme` arguments. A request for such a URL
   or WebWorker goes to the UI process, as a `file://` load does, and the embedder's handler answers it with a file and
   headers, or refuses it. The handler learns whether the asking process hosts a document (or runs a worker) of the
   URL's origin, and, for a navigation, who started it.
2. **A script bridge for the embedder's pages** (`0002`, done). `register_url_scheme()` takes an optional
   `URLSchemeBridge`. Before a document of the scheme may run scripts (at the start of
   `Document::set_ready_to_run_scripts()`), WebContent asks the UI process for a bootstrap script and runs it as the
   body of a function given a bridge object, whose `postMessage(string)` reaches the UI process and whose `onmessage`
   receives its replies. The UI process takes each message's sender from its own records of the documents a page hosts,
   including which view the page is in, which is how Xenon tells a popup from a tab or a hidden page.
3. **An origin access allowlist** (`0003`, done). The UI process may let an origin fetch from some others as if they
   were the same origin: with no CORS, and with a same-origin request's credentials. Blink and WebKit keep the same
   kind of list for browser extensions. An entry is a source origin, a destination scheme and host (or any host), and
   whether the host's subdomains count; ports aren't compared. Main fetch treats a matching request as same-origin, and
   `Application::set_origin_access_allow_list()` gives the list to every WebContent and WebWorker process.
4. **User scripts in isolated worlds** (`0016`, done). Between it and patch 3 come the six content blocker patches
   from xenon-ublock and the six WebAuthn patches, `0004` to `0015`, which their own documents describe. The embedder
   sets a `WebView::UserScriptHandler`, as WebKit's `WKUserContentController`. While it has one, each document asks the
   UI process for its scripts once, as it becomes ready for scripts, at the three places uBlock Origin's scriptlets
   run (the places WebDriver BiDi's `document_is_ready_for_scripts` fires): `Document::update_for_history_step_application`
   (navigated documents), `LocalNavigable::initialize_navigable` (a child navigable's first `about:blank`), and
   `LocalTraversableNavigable::create_for_new_web_view` (popups). A `Web::HTML::UserScript` names its world (the
   document's own, or an isolated world by the embedder's name for it), its injection time (`DocumentStart`;
   `DocumentEnd`, once DOMContentLoaded has fired; `DocumentIdle`, once the document has loaded, or 200 ms after
   `DocumentEnd` as in Chromium), and whether it takes its world's bridge to the UI process, as patch 2's pages do.
   Only isolated worlds get a bridge. The UI process only takes messages from the worlds it gave one, and names their
   sender from its own records of the document's URL, origin, and navigable. `ScriptBridgeEndpoint`, patch 2's
   endpoint, names the world and the navigable too. `HTML::IsolatedWorld` is the realm
   `Tests/LibWeb/TestWindowProxyWorld.cpp` builds: `create_a_new_javascript_realm()` with
   `create_global_object_wrapper(realm, window)` and `window_proxy_for(world, realm)` on a `WrapperWorldType::Extension`
   world, a `HostDefined` with the window's principal realm, then `cache_global_object_wrapper()` and
   `initialize_window_web_interfaces()`. Its exceptions and unhandled rejections only go to the console, since the
   page's `error` and `unhandledrejection` handlers would get its objects, and its wrappers are detached when its
   document is destroyed. Patch 2's hook, at the start of `Document::set_ready_to_run_scripts()`, wouldn't do for user
   scripts: `create_a_new_browsing_context_and_document()` calls it for an initial `about:blank` before that document
   has its creator's base URL or a navigable, so it can't tell which site's scripts apply. Extension pages are never
   `about:blank`, so patch 2 is fine where it is.

5. **Running a user script on request** (`0017`, done). `ScriptBridgeEndpoint::run_user_script()` runs a script in a
   document's world once the document reaches the script's injection time, and gives back its completion value as
   JSON, once it settles if it's a promise, or that it didn't run because the document has gone.
   `ViewImplementation::frame_document_endpoints()` lists the documents in a view's frames, wherever site isolation
   hosts them. This is what `scripting.executeScript()` needs; a script that takes a bridge gives the world one, as at
   document start.

   Still to come: per-extension stylesheets the page can't see (for `insertCSS` and content scripts' `css`;
   `Page::set_user_style()` is one per page and has no IPC). For now, both are stylesheets the document adopts.
6. **Navigation events for the embedder** (`0018`, done). `Application::set_navigation_event_handler()` gives the
   embedder every navigation event WebDriver BiDi gets, by BiDi's names, with the navigable it's about: navigations
   starting and failing in the browser process, and committing, firing DOMContentLoaded, loading, failing, and being
   aborted in the processes hosting web content. Those processes only reported them during a BiDi session; while the
   embedder has a handler, `WebDriver::BiDi::reports_navigation_events()` is true for them too, and nothing else of
   BiDi runs. Xenon makes `webNavigation`'s events of them.
7. **Embedder schemes bypass Content Security Policy** (`0019`, done). Requests, responses, and navigations to URLs
   of the embedder's own schemes aren't checked against the page's policy, as Blink lets extension resources bypass
   it (`WebSecurityPolicy::RegisterURLSchemeAsBypassingContentSecurityPolicy`). Without it, a site with
   `frame-src 'self'` blocks Bitwarden's inline menu and its bar that offers to save a login. The embedder's handler
   still decides which pages may load which files (`web_accessible_resources`).
8. **Who may ask for a subresource** (`0020`, done). For a subresource, the scheme handler's `URLSchemeRequest` names
   the origins that may be asking: a worker's, or those of the documents the asking process hosts for its page. The
   UI process knows those documents from its own records but not which one asks, and site isolation keeps them to one
   site. Proton Pass draws its icon in login fields with a web accessible image as the field's background, which a
   web page could only load in a frame before.

The background didn't need a patch of its own. The plan was embedder-owned workers: workers the UI process starts with
no owning document. A hidden extension page that starts the background script as a dedicated worker does the same job
with what the engine already has (see [Background context](#background-context)). The one engine change it needed was
letting the WebWorker process load `chrome-extension://` URLs, which went into patch 1.

Site isolation already gives every `chrome-extension://<id>` origin processes of its own, as a site of its own, so
the "embedder-chosen processes" patch planned at first isn't needed.

Later patches, as the milestones need them: request interception for `webRequest` (in WebContent's
`ResourceLoader::load()`, next to the content blocker's `should_block_request()`, asking the UI process; RequestServer
has no interception IPC), and HTTP auth challenges for `webRequestAuthProvider`. Offscreen documents need no patch:
LibWebView's `HeadlessWebView` already hosts a page with no window.

## Architecture

```
 UI process (Xenon)                                     Processes for chrome-extension://<id>
 ┌───────────────────────────────────────────┐          ┌──────────────────────────────────────┐
 │ Extensions::Registry (extensions, files,  │          │ popup.html, options, tabs            │
 │   storage, hidden views)                  │  scheme  │  main world: chrome.* (full)         │
 │ Extensions::Router (calls, messages,      │◄────────►├──────────────────────────────────────┤
 │   ports, events, sender info)             │  JSON    │ hidden: _generated_background_page   │
 │  ├ Storage, Alarms                        │  over    │  └ dedicated Worker: the background  │
 │  └ Browser: tabs, windows, action,        │  IPC     │     script, chrome.* (full)          │
 │      commands                             │          │ hidden: offscreen document           │
 │ Toolbar: action buttons, popups           │          │  chrome.runtime                      │
 │ Extensions dialog (load, reload, remove)  │          └──────────────────────────────────────┘
 └───────────────────────────────────────────┘          ┌──────────────────────────────────────┐
                                                 ◄─────►│ WebContent for https://example.com   │
                                           user scripts │  main world: the page                │
                                                        │  isolated world per extension:       │
                                                        │   content scripts, chrome.* (subset) │
                                                        └──────────────────────────────────────┘
```

The UI process is the browser in Chrome's terms. It owns every extension's state and enforces every permission.
Renderers only carry messages.

### Loading extensions

**Unpacked first.** The Extensions dialog (**Extensions** in the main menu) has **Load Unpacked…**, **Reload**, and
**Remove**, and `--load-extension=<dir>[,<dir>...]` (Chrome's flag) loads extensions for a session. Loading:

1. Reads `manifest.json`. `manifest_version` has to be 3 (2 is refused, with a message saying so). Unknown keys are
   kept and ignored, as Chrome does, with a warning per key in the dialog.
2. Computes the ID the way Chrome does, so that IDs other software relies on (native messaging hosts'
   `allowed_origins`, sites that detect an extension) match Chrome's: the first 128 bits of a SHA-256, as 32 letters
   `a`–`p`. The hash is of the decoded `key` if the manifest has one, or of the directory's absolute path if not.
3. Loads `_locales/<default_locale>/messages.json` and the UI language's, and resolves `__MSG_name__` in `name`,
   `description`, `action.default_title`, and the other strings Chrome localizes.
4. Validates paths: every file the manifest names has to exist inside the directory.
5. Records it in the registry. Unpacked extensions load from their directory; nothing is copied. Those loaded from the
   Extensions dialog are remembered in the profile (`<profile data>/Extensions/extensions.json`, with each one's last
   version, which decides `onInstalled`'s `reason` and `onStartup`); those from `--load-extension` last for the
   session, as in Chrome.

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
- **Process:** site isolation gives each extension's origin processes of its own, as a site of its own.
- **Fetching from other sites:** an extension's pages and workers fetch from the hosts in its `host_permissions`
  without CORS, as in Chrome (patch 3). Bitwarden needs this: its server answers CORS requests from its web vault's
  origin only. Content scripts won't get it: their requests are the page's.

### Background context

Bitwarden's MV3 manifest declares `background.service_worker`: a classic (not module) script, built for webpack's
`webworker` target, which loads its chunks with `importScripts()`. Its Firefox build runs the same code as a background
page, so it needs a worker-like global rather than anything particular to service workers: it only uses
`ServiceWorkerGlobalScope` as a type. The one exception is Web Push through `self.registration`, which it skips when
`registration` is missing, falling back to a WebSocket. Ladybird registers service workers but can't run one yet, so
Xenon runs the background script the way Chrome ran MV2 background pages, with a worker inside:

- At startup, and whenever an extension loads, the registry opens `chrome-extension://<id>/_generated_background_page.html`
  in a `HeadlessWebView`: a page with no window, which reports itself visible, so its timers and its workers aren't
  throttled. It is an extension page, in the extension's process, with the extension's origin and storage.
- That page's bootstrap (`background-page.js`) starts `/_generated_background_worker.js` as a dedicated `Worker`, which
  loads `/_generated_chrome_api.js` and then the manifest's script, with `importScripts()`, or with static `import`s for
  `"type": "module"`. The worker gets a worker global (`self`, `importScripts()`, `fetch()`, IndexedDB, `crypto`,
  timers, no DOM) and has `chrome.*` before its first line runs.
- Workers have no script bridge of their own, so the page relays: the worker's `chrome.*` (`worker-bridge.js`) talks to
  its page over a `MessagePort`, and the page passes messages to and from the UI process unchanged. The router knows
  the background's messages from the page they come through, so `getContexts()` reports one `BACKGROUND` context.
- The `_generated_` files are only for the extension's own processes and the browser; a web page can't load them, even
  when an extension makes everything web accessible. They are served from unlinked temporary files, since the renderer
  reads responses from a file descriptor.
- `runtime.onInstalled` (`install`, or `update` with `previousVersion`) or `runtime.onStartup` fires once the worker's
  script has run, so its top-level listeners are in place, as in Chrome.
- Before that, the worker gets a service worker's `install` and `activate` events (with a `waitUntil()` that nothing
  waits for), and `skipWaiting()`, which install handlers call. The worker is a new one each time the browser starts,
  so it is installed each time. An MV3 service worker may only import a new script while it installs, so extensions
  load the rest of their code then: Proton Pass imports its chunks from `oninstall`. A script imported later, from a
  promise's reaction, never finishes loading. Ladybird spins the event loop for `importScripts()`'s fetch, and inside
  a microtask checkpoint the fetch's own microtasks can't run, so the worker hangs. An extension that imports scripts
  that way still hangs its background; Chrome would refuse such an import outright.
- Runtime messages and ports for the extension's pages and background wait until its script has run, or 10 seconds in
  case it never does, as Chrome holds them while it starts a service worker. Tabs that load as the browser starts
  would otherwise find no receiver for their content scripts' first messages.
- The worker gets no `fetch` events for the extension's own URLs, which a service worker gets. Proton Pass serves the
  item icons in its popup that way (`/api-proxy/...`), so they don't load. The scheme handler would have to wait for
  the worker's answer, which patch 1's synchronous `URLSchemeHandler` can't.
- It stays running, as an MV2 background page did. Chrome stops idle service workers after 30 seconds and restarts them
  on an event; an extension that survives being stopped also survives not being stopped. Stopping idle workers is a
  memory optimization for later.
- `chrome.extension.getBackgroundPage()` is left out, which is how Bitwarden tells MV3 from MV2.
- Once Ladybird runs real service workers, the background script can move to one; `chrome-api.js` stays as it is.

The background has to come early. Bitwarden's popup won't show its unlock screen until the background has run its
state migrations: it polls `storage.local` for the state version the worker writes, for up to about 16 seconds. It
also opens ports to the worker as it starts.

**Offscreen documents** (`chrome.offscreen`) are extension pages in another `HeadlessWebView`, one per extension, with
`chrome.runtime` alone, as in Chrome. Bitwarden uses one for the clipboard and for a `localStorage` backup of
device-trust keys. It picks those code paths by `manifest_version`, not by checking that `chrome.offscreen` exists, so
the API has to be there with the background.

### Content scripts and isolation

As each document becomes ready for scripts, the engine asks the UI process for its user scripts (patch 16), and
`ContentScripts` matches every extension's `content_scripts` against the document's URL, from the UI process's records
rather than the renderer's word: `matches` and `exclude_matches` (match patterns), `include_globs` and
`exclude_globs`, and `all_frames`. A matching extension's world gets its `chrome.*` at document start, whenever its
scripts run, then the stylesheets, then the scripts at their `run_at`: `document_start`, `document_end`, or
`document_idle` (the default). Not yet: `match_about_blank` and `match_origin_as_fallback`, for `about:blank`, `blob:`,
and `data:` frames, and `file:` documents, which Chrome only allows once the user turns it on for the extension.

- **Isolation:** each extension gets its own isolated world per document, named by its ID. The world shares the DOM
  with the page but nothing else: its own globals, its own prototypes, and its own wrappers, so expando properties and
  monkey-patched builtins in the page are invisible to it, and the other way round. Ladybird's wrapper worlds give each
  world its own wrappers for the same DOM object, which is the hard part. The world's exceptions and unhandled
  rejections stay out of the page's `error` and `unhandledrejection` handlers. One gap: a value the page posts with
  `postMessage`, like any `any`-typed attribute, reaches the world as the page's own object, where Chrome deserializes
  a copy for each world. Reading strings and numbers from it, as Bitwarden does, is safe; calling its methods runs the
  page's code.
- **`chrome.*` in content scripts:** the subset Chrome gives them: `runtime` (`id`, `getURL`, `getManifest`,
  `sendMessage`, `connect`, `onMessage`, `onConnect`, `lastError`), `storage`, and `i18n`. `storage.session` answers
  only once the extension has called `storage.setAccessLevel()` with `TRUSTED_AND_UNTRUSTED_CONTEXTS`. Nothing else is
  installed, and the router refuses anything else a content script's bridge asks for, since its renderer is the
  page's.
- **`world: "MAIN"`** scripts run in the page's own world, with the page's globals and no `chrome.*`.
- **Stylesheets** go in at document start as stylesheets the document adopts, so for now the page can see and remove
  them. Chrome adds them as user stylesheets, which needs per-extension user styles in the engine.
- **Content script ↔ page:** through the shared DOM, `window.postMessage`, and DOM events, which is what Bitwarden's
  autofill and inline menu use. A content script's `fetch()` goes out with the page's origin, as in Chrome MV3.
- **`chrome.scripting`:** `executeScript` runs files, or a function with its arguments (both travel as source and
  JSON, as Chrome serializes them), in the extension's isolated world by default, or the page's world with
  `world: "MAIN"`, in the top frame, some frames by ID or document, or all of them. A world that doesn't have
  `chrome.*` yet gets it first. It waits for document idle unless `injectImmediately` is set, and answers with each
  frame's result, awaiting promises. The extension needs host permission for each frame, from `host_permissions` or
  its manifest's content scripts. `insertCSS` and `removeCSS` add and take out stylesheets the document adopts, from
  the extension's world. `registerContentScripts` (and `unregister`, `update`, `getRegistered`) adds scripts that run
  with the manifest's in documents that load afterwards, kept across sessions when `persistAcrossSessions` is set and
  cleared when the extension reloads. Bitwarden injects its autofill scripts this way, at its content script's request.
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
  either end's context goes away (`onDisconnect`).
- **Web pages** (`externally_connectable`): a page whose URL an extension's `externally_connectable.matches` covers gets
  `chrome.runtime` in its own world, with `sendMessage` and `connect` to that extension by its ID, and `lastError`.
  Proton Pass needs this to sign in: Proton's account page hands it the new session with
  `chrome.runtime.sendMessage(extensionId, ...)`. Patch 16 gives only isolated worlds a bridge, so the page's
  `chrome.runtime` (`externally-connectable.js`) sends its messages as DOM events to a relay of Xenon's
  (`externally-connectable-relay.js`), in an isolated world of its own per extension, which has a bridge. The page can
  see and forge those events, as it could call `chrome.runtime` anyway; the UI process knows which document each relay
  is in and only lets it send messages and open ports to its extension, which get them through `runtime.onMessageExternal`
  and `onConnectExternal`, from a `sender` with no `id`. As in Chrome, a pattern for every host (`<all_urls>`,
  `*://*/*`) is ignored, with a warning. Other extensions can't message an extension yet (`ids`).

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
| **5. Installing from the store** | CRX3 install and updates, the Chrome Web Store, offering password managers in the welcome flow as Helium does. Then whatever 1Password uses beyond the above, found the way Bitwarden's and Proton Pass's needs were. | The setup flow Helium has |

Passkeys need more than Bitwarden's scripts: Ladybird's `navigator.credentials` rejects `get()`, `store()`, and
`create()` as not implemented, and there is no `PublicKeyCredential`. Bitwarden's passkey support replaces
`navigator.credentials` from a main-world script, and may require those to exist first. Nothing in Ladybird detects
login forms or autofills either (`:autofill` never matches), so autofill comes entirely from the extension, as it does
for Bitwarden in Chrome.

### Where each piece lives

| Piece | Engine patch | Xenon |
| --- | --- | --- |
| `chrome-extension://` origin, secure context, loading | 0001 | `UI/Extensions/Registry`: maps URLs to files, adds CSP, decides who may load what |
| Fetching from permitted hosts without CORS | 0003 | `UI/Extensions/Registry`: an allowlist entry per extension and `host_permissions` pattern |
| Manifest, ID, locales, `__MSG_`, match patterns | | `UI/Extensions/Manifest`, `Extension`, `Locales`, `MatchPattern` |
| `chrome.*` in extension pages | 0002 | `UI/Resources/extensions/chrome-api.js` (the API surface, built into the browser), `UI/Extensions/Router` (answers calls, routes messages, sends events) |
| `chrome.storage` | | `UI/Extensions/Storage`: a JSON file per extension and area under `<profile data>/Extensions/Storage/<id>/`; `session` and `managed` are in memory |
| Toolbar and popup | | `UI/Extensions/Toolbar` (a button per extension, in each tab's toolbar), `UI/Extensions/Popup` (the popup's popover and `WebContentView`) |
| Content scripts in isolated worlds | 0016 | `UI/Extensions/ContentScripts` (which scripts a document gets), `Manifest` (matching a document's URL), `Router` (their contexts, calls, and messages) |
| Web pages messaging an extension (`externally_connectable`) | | `UI/Extensions/ContentScripts` (the page's `chrome.runtime` and a relay world per extension), `Router` (their `ExternalPage` contexts), `UI/Resources/extensions/externally-connectable.js` and `externally-connectable-relay.js` |
| Web accessible resources as web pages' subresources | 0020 | `UI/Extensions/Registry` |
| `chrome.scripting`, `webNavigation` frames | 0017 | `UI/Extensions/Scripting.cpp` (the `Router`'s injections, registered content scripts, and Chrome's frame IDs), `Registry` (registered scripts that persist) |
| Background worker, offscreen documents | 0001 (workers' loads) | `UI/Extensions/Registry` (their hidden views and generated files), `UI/Resources/extensions/background-page.js` and `worker-bridge.js` |
| `tabs`, `windows`, `action`, `commands` | | `UI/Extensions/Browser` (Chrome's numeric IDs for Xenon's tabs and windows, their events, action state per tab, shortcuts) |
| `alarms` | | `UI/Extensions/Alarms` |
| Managing extensions | | `UI/Extensions/Dialog` (Qt), and `--load-extension`. An `about:extensions` page would need LibWebView's hardcoded WebUI list to grow, so it waits. |

### The toolbar and the popup

- Each enabled extension with an `action` gets a toolbar button after the address bar, with `action.default_icon`
  (the size closest to the toolbar's, for the screen's scale) and `default_title` as the tooltip. The badge
  (`setBadgeText`, `setBadgeBackgroundColor`, `setBadgeTextColor`) is drawn on the icon, in the extension's color if
  it set one and Xenon's rose otherwise. Every `action.set*` given a `tabId` applies to that tab alone.
- Clicking it opens `action.default_popup` in a `Popover` anchored to the button, as the content blocking popover is,
  hosting a `WebContentView` of the popup page. The popover sizes itself to the page's content, between 25×25 and
  800×600 as in Chrome. It closes when it loses focus, on Escape, or when the page calls `window.close()`.
- With no popup, a click fires `action.onClicked` in the background worker and grants `activeTab`.
- `commands` with a `suggested_key` become shortcuts in every window, with `_execute_action` opening the popup (or
  firing `action.onClicked`). As in Chrome, `Ctrl` means ⌘ on macOS and `MacCtrl` means Control. A shortcut that clashes
  with one of Xenon's (⌘⇧C copies the URL) or another extension's does nothing for now; Chrome drops the extension's
  instead, and lets the user pick another.
- The chrome uses Xenon's palette from `UI/BrandColors.h`; the popup page draws itself.

### Security

- **Permissions** are granted at load. Unpacked extensions are developer-loaded, so loading one grants its required
  `permissions` and `host_permissions`, as Chrome does. CRX installs (milestone 5) ask first, listing what the
  extension can do in plain words. `optional_permissions` need `permissions.request()` and a user gesture, and ask.
  Every API handler checks the calling extension's permissions in the UI process; a missing `chrome.foo` in the
  bootstrap is a convenience, not the check.
- **Host access** is checked at injection time and per call (`scripting`, `tabs` URL fields, `webRequest`), and
  decides which hosts an extension fetches from without CORS. Every process gets the whole allowlist, so a compromised
  renderer could learn which extensions are loaded; it can't use the list to reach more than it already could, since
  Ladybird enforces CORS in the renderer.
  `activeTab` grants the current tab's origin until it navigates.
- **Isolation:** content scripts in isolated worlds, extension pages in their own process with their own origin, and
  sender identity from the UI process. A compromised site's renderer can't reach extension pages, which live in
  another process, and can only send messages as the content scripts in its own frames.
- **CSP:** extension pages get MV3's policy and can't loosen it beyond what MV3 allows. The page's CSP doesn't stop
  content scripts from running, since the UI process injects them, but what they insert into the page (a `<script>`,
  an `<img>`) is still subject to it, as in Chrome, except the extension's own resources, which bypass it (patch 19).
- **Storage** is per extension ID, in the profile, and deleted with the extension.
- **Native messaging** (milestone 4) runs only hosts registered in Chrome's manifest locations whose
  `allowed_origins` list the extension, over stdin/stdout with Chrome's length-prefixed framing. It needs no engine
  change: the UI process spawns the host with `Core::Process` and pipes, as LibWebView spawns its helpers. Not with
  `QProcess`, whose exit status LibWebView's `ProcessMonitor` would reap first: it calls `waitpid(-1)` on every
  `SIGCHLD`.
- **No `file://` access** for content scripts unless the user turns it on per extension, as in Chrome.

## Testing

- Unit tests (`Tests/TestExtension*.cpp`, run by `Meta/xenon.py test`): manifest parsing and validation, IDs (against
  IDs Chrome computes for the same keys), locales and `__MSG_`, match patterns, URL-to-file resolution including path
  traversal, and storage.
- `Tests/Extensions/` holds small test extensions. `Meta/xenon.py test` loads one headlessly with
  `--load-extension`, opens its page, and checks what its scripts report through `chrome.*` and storage. It also
  serves `Tests/Extensions/content-scripts.html` from a local server, where the test extension's content scripts run,
  and checks them from both sides.
- Real extensions: Bitwarden's unpacked Chrome MV3 build (`npm run build:chrome` in `bitwarden/clients/apps/browser`)
  is the target for milestones 1 to 4, checked by hand against a test vault.

## Status

- [x] Engine strategy: a patch series in `Patches/ladybird`, applied by `Meta/patches.py`.
- [x] **Milestone 1: extension pages.**
  - Loading unpacked extensions with `--load-extension=<dir>[,<dir>...]`, with Chrome's validation, IDs, and locales.
  - `chrome-extension://` pages: their own origin, secure contexts, MV3's CSP, and storage of their own.
  - `chrome.runtime` (`id`, `getURL`, `getManifest`, `getPlatformInfo`, `sendMessage`, `onMessage`, `lastError`),
    `chrome.storage` (all four areas, with `onChanged`), and `chrome.i18n`.
  - A toolbar button per extension, and its popup, sized to its page.
  - `Meta/xenon.py test` checks the APIs from an extension page, and that a web page reaches neither `chrome.*` nor
    the extension's files.
- [x] **Milestone 2: background.**
  - The background script in a dedicated worker of a hidden background page, with `onInstalled` and `onStartup`.
  - `runtime` ports (`connect`, `onConnect`, `Port`), `getContexts`, `reload`, `openOptionsPage`, and `sender.tab` for
    pages in tabs.
  - `alarms`; `tabs` (`query`, `get`, `getCurrent`, `create`, `update`, `remove`, `reload`, `duplicate`, and
    `onCreated`, `onUpdated`, `onActivated`, `onRemoved`, `onAttached`); `windows` (`get`, `getAll`, `getCurrent`,
    `getLastFocused`, `create` with `type: "popup"`, `update`, `remove`, and their events). A tab's URL and title show
    only with the `tabs` permission or host permission for it.
  - `action`, per tab: badges, titles, icons from paths, popups, `enable`/`disable`, `openPopup`, and `onClicked`.
  - `commands` as shortcuts; `offscreen` documents; `management.getSelf`; `permissions.contains` and `getAll`.
  - `webNavigation.getFrame` and `getAllFrames` for a tab's top frame (every frame since milestone 3). Stubs, so
    Bitwarden's background gets the objects it uses without checking: `webNavigation` events, `scripting` (real since
    milestone 3), `webRequest` events, and `notifications`.
  - Fetching from the hosts in `host_permissions` without CORS (patch 3).
  - Optional permissions: `permissions.request()` asks with Chrome's warnings, over the popup when it asks from there,
    and needs a user gesture; granted ones last across sessions and reach open contexts at once, with `onAdded`;
    `permissions.remove()` takes them back. `chrome.privacy.services` password and autofill settings, which only
    record which extension controls them, since Xenon has neither of its own.
  - The Extensions dialog: load unpacked (remembered in the profile), reload, and remove (which deletes the
    extension's storage). Toolbars and shortcuts follow it.
  - `Meta/xenon.py test` runs the test page twice in one profile. It checks that the background answers messages and
    ports from a worker global, gets `onInstalled` and then `onStartup`, and that alarms, tabs, windows, and an
    offscreen document work, and that the page and the background fetch from a permitted host without CORS while a web
    page can't.
  - Bitwarden 2026.10.0's Chrome build loads. Its background runs its state migrations and fetches its server's
    config, and its popup page shows the welcome screen (seen in a headless render at popup size).

  Left from milestones 1 and 2:
  - `webRequest` events never fire, and notifications are never shown (milestone 4).
  - Optional host permissions (`origins` in `permissions.request`). `idle` always reports `active`.
  - The permission prompt has only been built, not clicked: headless tests have no user gestures.
  - `tabs.captureVisibleTab` and `action.setIcon` with `imageData` fail. `tabs.onDetached`, `onMoved`, `onReplaced`,
    and `onHighlighted`, and `permissions.onAdded` and `onRemoved`, exist but never fire.
  - A background page whose process crashes isn't restarted until the extension is reloaded.
  - `__MSG_` in extension CSS files, which Chrome localizes as it serves them.
  - History navigation (back and forward) to extension pages.
  - The popup's size is measured from its page's body, which suits pages that set their width; Chrome lays out the
    page at its preferred size instead.
  - Nothing has clicked through the popup, the Extensions dialog, or a command shortcut yet: no test drives the Qt
    side, and they've only been built and seen in the toolbar.
  - Bitwarden logs in and works from its popup (tried by hand). Making it the default password manager needed the
    optional `privacy` permission, which the prompt now grants; that path hasn't been tried by hand yet.
- [ ] **Milestone 3: content scripts** (under way).
  - Manifest `content_scripts` in an isolated world per extension and document (patch 16), at `document_start`,
    `document_end`, or `document_idle`, in the top frame or all frames, with globs, and in the MAIN world when asked.
    Their stylesheets, as adopted stylesheets.
  - `chrome.runtime` (`sendMessage`, `connect`, and their events), `storage`, and `i18n` in content scripts. Their
    messages and ports reach the extension's pages and background, with `sender.tab`, `frameId`, `url`, `origin`, and
    `documentId`. `tabs.sendMessage()` and `tabs.connect()` reach the content scripts in a tab, in all its frames or
    one.
  - `Meta/xenon.py test` checks 22 things on a page the test extension's content scripts match: when each runs, what
    each world can see of the other (globals, expandos, errors, rejections), the shared DOM, `postMessage` from the
    page, stylesheets, messages and ports to the background, and storage.
  - Bitwarden's content scripts run: on a page that asks, its message handler answers that Bitwarden is installed.
  - With the WebAuthn patches, Bitwarden's two-step login with a USB security key works (tried by hand). The popup
    opens the web vault's `webauthn-fallback-connector.html` in a tab, where the key answers. The page posts the
    result to Bitwarden's content script, which sends it to the background. The background then opens a popout window
    (`windows.create` with `type: "popup"`) that submits the token.

  - `chrome.scripting` (patch 17): `executeScript` with files or a function and its arguments, in the top frame, some
    frames, or all of them, in the extension's world or the page's, waiting for document idle unless asked not to,
    with each frame's result, promises awaited; `insertCSS` and `removeCSS`; and `registerContentScripts`,
    `unregisterContentScripts`, `updateContentScripts`, and `getRegisteredContentScripts`, kept across sessions when
    asked. `webNavigation.getFrame` and `getAllFrames` for every frame of a tab. A runtime message that no context
    has a listener for fails with "Receiving end does not exist", as in Chrome.
  - `webNavigation` events (patch 18), for every frame of a tab: `onBeforeNavigate`, `onCommitted`,
    `onDOMContentLoaded`, `onCompleted`, `onErrorOccurred` (`net::ERR_FAILED`, or `net::ERR_ABORTED`),
    `onReferenceFragmentUpdated`, and `onHistoryStateUpdated`, with the tab, frame, parent frame, document, and
    Chrome's other details. They're followed while any extension has the `webNavigation` permission.
  - In a headless browser, the page it loads stands for tab 1 of window 1, for `tabs.get`, `tabs.query`, `sender.tab`,
    `tabs.sendMessage`, and `scripting`, so that `Meta/xenon.py test` can check them. It checks 45 things on its
    content script page now, among them `executeScript` in each world and frame, results and promises, CSS, frames,
    `tabs.sendMessage` to content scripts, a registered script running in a frame that reloads, the
    `webNavigation` events of a frame's navigation and of one that fails, and a web accessible page in a frame of a
    page whose CSP allows only its own frames.
  - Bitwarden's autofill scripts go in at its content script's request: on a login page, its inline menu shows on the
    focused field (seen in a headless render with a locked vault: "Unlock your account to view autofill
    suggestions"). Its frames are `web_accessible_resources` pages, which load in the page. With the vault unlocked,
    autofill works, and so does its bar that offers to save a new login, on a site whose CSP allows only frames of
    its own too (tried by hand, with patch 19).

  Left:
  - `webNavigation`'s `onCreatedNavigationTarget` and `onTabReplaced`, and real error names (Chrome's
    `net::ERR_NAME_NOT_RESOLVED` and the like) and transition types.
  - Sandboxed extension pages (`sandbox.pages`).
  - `match_about_blank`, `match_origin_as_fallback`, and `file:` access. `activeTab`.
  - Content scripts' and `insertCSS`'s stylesheets as user stylesheets. Values the page posts arriving as copies in
    each world.
  - `action` calls for the headless browser's tab (Bitwarden's badge logs "No tab with id: 1" there).
- [x] **Proton Pass**, 1.43.0 from the Chrome Web Store (GPL-3.0; its source is `applications/pass-extension` in
  ProtonMail/WebClients), loaded unpacked from the CRX's zip. Its manifest's `key` gives it Chrome's ID,
  `ghmbeldphafepmbegfdlkpapadhbakde`, which Proton's account pages message. Tried by hand with a real account:
  - Its background starts, and its popup renders.
  - The popup's **Sign in with Proton** opens Proton's account page, which hands Pass the new session through
    `externally_connectable`. Pass then asks for the account's Pass extra password, checks it (SRP), and the vault
    shows in the popup.
  - On a local login page, Pass's icon shows in the fields, focusing one opens its dropdown with the matching login,
    and choosing it fills both fields. Signing in with a new login offers to save it, and saving works.

  What it needed beyond Bitwarden: web pages messaging an extension (`externally_connectable`), web accessible images
  as web pages' subresources (patch 20), `permissions.contains()` matching patterns as Chrome does (Pass asks about
  `https://account.proton.me/*`, which `https://*/*` covers), the service worker's install event (where it imports its
  chunks), and runtime messages that wait for the background as it starts. `Meta/xenon.py test` checks each of them
  with the test extension, on a page its `externally_connectable` matches too.

  Left:
  - The item icons in the popup, which Pass's service worker answers `fetch` events for (see
    [Background context](#background-context)).
  - `webRequest` events never fire (milestone 4). Pass uses them to follow logins sent with XHR and main-frame loads
    that fail; the save prompt after a plain form post doesn't need them.
  - Signing in from the onboarding tab Pass opens on install led to Proton's Pass web app, which doesn't sign the
    extension in; the popup's button does. Not compared with Chrome.
  - Not tried yet: passkeys, credit cards and identities, one-time codes, aliases, the settings page, importing and
    exporting, and copying to the clipboard (through its offscreen document).
- [ ] Milestone 4: the rest of Bitwarden
- [ ] Milestone 5: installing from the store
