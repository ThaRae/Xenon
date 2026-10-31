# WebAuthn

This is the design for Web Authentication in Xenon: `navigator.credentials.create()` and `get()` with `publicKey`,
and `PublicKeyCredential`. Security keys over USB come first, on macOS. The status section at the end says what exists
so far.

Every site that offers security keys or passkeys needs this, and so do password managers. Bitwarden's extension is
what turned it up: an account whose only second factor is a security key or a passkey can't sign in. The extension
offers WebAuthn two-step login only when `typeof PublicKeyCredential !== "undefined"`, and without it says "none of the
configured two-step providers are supported by this web browser". It then runs the ceremony by calling
`navigator.credentials.get()` in an iframe of the web vault (`https://vault.bitwarden.com/webauthn-connector.html`),
which it gives `allow="publickey-credentials-get https://vault.bitwarden.com"`. The connector calls `get()` as soon as
it loads, with no click first (it waits for a click only in browsers whose user agent says Safari and not Chrome, and
Ladybird's says both). It passes the server's options through unchanged, `extensions` included, which can hold
`appid` for keys registered back when Bitwarden used U2F.

## What Ladybird has

| Ladybird has | Where | What WebAuthn gets from it |
| --- | --- | --- |
| `navigator.credentials`, with `get()`, `create()` and `store()` as stubs that reject with "not implemented" | `LibWeb/CredentialManagement/CredentialsContainer.cpp` | The entry points. Credential Management's own algorithms (which interface handles a request, `sameOriginWithAncestors`, the permissions policy check) aren't there either. |
| `Credential`, `PasswordCredential` and `FederatedCredential`, as wrappables | `LibWeb/CredentialManagement` | The base class and the pattern for `PublicKeyCredential` |
| Permissions policy, including the `allow` attribute, with each document's policy replicated to the UI process | `LibWebCommon/PermissionsPolicy`, `HTML/NavigableContainer.cpp`, `LibWebView/CanonicalDocument.h` | The two features WebAuthn defines, which only need adding to the list |
| The UI process's own record of every document: its origin, permissions policy, navigable, ancestors, and which WebContent process hosts it | `LibWebView/CanonicalDocument.h`, `CanonicalNavigable.h`, `WebContentPage::did_completely_finish_loading()` | A way to check a request's origin and RP ID without trusting the renderer |
| Requests that go out to the UI process and come back by ID: geolocation, permission prompts, the file picker | `Page::request_geolocation_position()`, `WebContentClient.ipc`, `WebContentPage::did_request_geolocation_position()`, `ViewImplementation::on_request_*` | The shape of the IPC and of the hooks a frontend implements |
| `is_a_registrable_domain_suffix_of_or_is_equal_to()` and the public suffix list | `LibWeb/DOM/Document.h`, `LibURL/PublicSuffixData.h` | RP ID validation. The function is in LibWeb, which the UI process doesn't link, so it moves to LibURL. |
| base64url, SHA-256, ECDSA (OpenSSL underneath) | `AK/Base64.h`, `LibCrypto` | Client data, the JSON methods, and a virtual authenticator |
| `testdriver.js` with `add_virtual_authenticator()`, which nothing implements yet; WPT imports served over `http://test-web-<uuid>.localhost` | `Tests/LibWeb/Text/input/wpt-import/resources`, `Tests/LibWeb/test-web/main.cpp` | In-tree tests, once a virtual authenticator exists. `*.localhost` is a secure context with a real domain, so WebAuthn can run there. |
| WebDriver, with routes in LibWebCommon and a browser connection in the UI process | `LibWebCommon/WebDriver/Client.cpp`, `LibWebView/WebDriverBrowserConnection.cpp` | WebAuthn's WebDriver extension (virtual authenticators), which WPT's runner uses |
| `libfido2` 1.17.0 (BSD-2-Clause) and `libcbor` in the pinned vcpkg baseline; OpenSSL and zlib, which it needs, are already dependencies | vcpkg `ports/libfido2` | A complete CTAP client: USB HID on macOS, Linux and Windows, CTAP2.0/2.1 with a CTAP1/U2F fallback, both PIN protocols |

There is no `PublicKeyCredential` anywhere, nothing that talks to authenticators, and no CBOR code.

## Scope

In, in this order:

- **Roaming security keys over USB HID**, on macOS first: YubiKeys, Feitian, SoloKeys, Google Titan, and anything else
  that speaks CTAP2 or U2F. Registration and sign-in, PINs, keys with their own fingerprint reader, and keys that only
  speak U2F.
- **Discoverable credentials on security keys** (passkeys stored on the key): sign-in without a username, choosing
  among accounts, and managing the key (PIN setup, listing and deleting passkeys).
- **Linux**, through the same code. It needs `libudev` headers at build time, which Ladybird's CI image lacks, so
  Xenon's Linux job would install `libudev-dev`, and the usual udev rules for FIDO devices at run time.

Only with Apple's entitlement, which hasn't been decided on (see [Platform passkeys](#platform-passkeys-milestone-3)):

- **Platform passkeys** (iCloud Keychain, Touch ID, passkeys from a phone, third-party credential providers) through
  Apple's AuthenticationServices, which needs a managed entitlement from Apple.

Out for now: NFC and Bluetooth security keys, the hybrid (phone) transport without Apple's API, conditional mediation
(passkeys offered in autofill), and related origin requests (`/.well-known/webauthn`). Each is a self-contained
addition later.

## Architecture

```
 WebContent (sandboxed, one per site)          UI process (Xenon)
┌──────────────────────────────────────┐      ┌──────────────────────────────────────────────────────────┐
│ LibWeb                               │      │ LibWebView                                               │
│  navigator.credentials.create/get    │      │  WebContentPage: is the document hosted here, active,    │
│   → Credential Management algorithms │ IPC  │    allowed by its permissions policy? What's its origin, │
│   → PublicKeyCredential [[Create]] / │─────►│    top-level origin, sameOriginWithAncestors?            │
│     [[DiscoverFromExternalSource]]:  │      │  WebAuthnCoordinator: one ceremony per tab               │
│     arguments, RP ID, timeouts,      │      │   ├ builds clientDataJSON and its hash                   │
│     extensions                       │◄─────│   ├ timeout, cancellation, navigation away               │
│   ← PublicKeyCredential with its     │      │   ├ drives the prompt (ViewImplementation callbacks)     │
│     AuthenticatorResponse            │      │   └ Authenticator implementations:                       │
│                                      │      │       ├ HIDAuthenticators (libfido2, worker threads)     │
│ no device access, no clientDataJSON  │      │       └ VirtualAuthenticator (WebDriver, tests)          │
└──────────────────────────────────────┘      ├──────────────────────────────────────────────────────────┤
                                              │ Xenon UI/: WebAuthnDialog (touch, PIN, accounts, errors) │
                                              └──────────────────────────────────────────────────────────┘
                                                                     │ USB HID (CTAPHID)
                                                                     ▼
                                                               security keys
```

The renderer is sandboxed and can't open HID devices, so everything that touches an authenticator runs in the UI
process, which also shows the prompts. The renderer runs the parts of the algorithms that throw exceptions a page can
see. The UI process checks every decision that matters for security again, from its own records, and builds the
client data itself. A compromised renderer can then ask for nothing it couldn't ask for honestly.

### In the renderer (LibWeb)

A new `Libraries/LibWeb/WebAuthn/` directory, named after the spec as Ladybird's other directories are:

- **IDL**: `PublicKeyCredential` (`rawId`, `response`, `authenticatorAttachment`, `getClientExtensionResults()`,
  `toJSON()`, and the statics `isConditionalMediationAvailable()`, `isUserVerifyingPlatformAuthenticatorAvailable()`,
  `getClientCapabilities()`, `parseCreationOptionsFromJSON()`, `parseRequestOptionsFromJSON()`),
  `AuthenticatorResponse`, `AuthenticatorAttestationResponse` (`attestationObject`, `getTransports()`,
  `getAuthenticatorData()`, `getPublicKey()`, `getPublicKeyAlgorithm()`), `AuthenticatorAssertionResponse`
  (`authenticatorData`, `signature`, `userHandle`), and the option dictionaries, as partial dictionaries of
  `CredentialCreationOptions` and `CredentialRequestOptions`, the way `PasswordCredential.idl` adds its own.
  `getPublicKey()` and the rest are there because sites use them instead of parsing CBOR. The UI process works their
  values out and sends them along, so LibWeb never parses CBOR.
- **Credential Management**: `CredentialsContainer::get()` and `create()` run the spec's "request a credential" and
  "create a credential" algorithms for real: the relevant credential interfaces, the settings object's active
  credential types, `sameOriginWithAncestors`, and each interface's permissions policy feature. `PasswordCredential`
  and `FederatedCredential` come along: `create()` returns a new object, and `get()` finds no stored credentials,
  since Ladybird has no store.
- **`[[Create]]` and `[[DiscoverFromExternalSource]]`**, step by step as the spec has them, as far as the client data:
  snapshotting `BufferSource`s, transient activation (consumed) for a cross-origin `create()`, the timeout clamped to
  the spec's recommended 300 to 600 seconds, the user ID's length, opaque origins, the effective domain, RP ID
  validation, `pubKeyCredParams` (ES256 and RS256 when empty, `NotSupportedError` when nothing is supported), and
  client extension inputs. The remaining steps, the authenticator part, become one request to the UI process through
  `Page`, as geolocation's do. `AbortSignal` cancels it.
- **Client extensions**: `appid` and `credProps` in milestone 1; `prf`, `largeBlob`, `credProtect`
  (`credentialProtectionPolicy`), `minPinLength` and `appidExclude` in milestone 2. Unknown extensions are ignored,
  as the spec says.

### Across the process boundary

Shared types go in `Libraries/LibWebCommon/WebAuthn/`, as `.ipc` structs, since both LibWeb and LibWebView use them:

```
// WebContentClient.ipc (WebContent → UI)
did_request_web_authn_create(Web::PageId, u64 request_id, Web::HTML::CrossProcessId navigable_id,
    Web::HTML::CrossProcessId document_id, Web::WebAuthn::CreateRequest) =|
did_request_web_authn_get(Web::PageId, u64 request_id, Web::HTML::CrossProcessId navigable_id,
    Web::HTML::CrossProcessId document_id, Web::WebAuthn::GetRequest) =|
did_cancel_web_authn_request(Web::PageId, u64 request_id) =|

// WebContentServer.ipc (UI → WebContent)
web_authn_request_completed(Web::PageId, u64 request_id, Web::WebAuthn::Result) =|
```

- `CreateRequest` carries the RP ID and name, the user entity, the challenge, the algorithms, the timeout,
  `excludeCredentials`, `authenticatorSelection`, `attestation` and `attestationFormats`, `hints`, the mediation, and
  the extension inputs. `GetRequest` carries the RP ID, the challenge, the timeout, `allowCredentials`,
  `userVerification`, `hints`, the mediation, and the extension inputs. No origin: the UI process uses its own.
- `Result` is a registration (credential ID, `clientDataJSON`, `attestationObject`, `authenticatorData`, the public key
  as SPKI DER when the algorithm is one the spec lists, its COSE algorithm, transports, attachment, extension outputs),
  an assertion (credential ID, `clientDataJSON`, `authenticatorData`, `signature`, `userHandle`, attachment, extension
  outputs), or an error: a `DOMException` name and message.

### In the UI process (LibWebView)

`WebContentPage::did_request_web_authn_create()` and `_get()` check the request against the UI process's records,
the way `did_completely_finish_loading()` does:

1. The navigable exists, the document is its active document, and this `WebContentPage` hosts it. Otherwise the
   renderer is misbehaving.
2. The document's origin isn't opaque, its effective domain is a domain (not an IP address), it's a secure context,
   and the RP ID is equal to it or a registrable domain suffix of it, using LibURL's public suffix list.
3. The document's permissions policy allows `publickey-credentials-create` or `publickey-credentials-get`.
4. `sameOriginWithAncestors` comes from the canonical ancestors' active documents, and the top-level origin from the
   top-level document.
5. The view is visible and focused, as the frontend reports to the traversable (Chromium has the same rule), and its
   tab has no other ceremony that the page is still waiting on. One that has answered the page, and keeps its prompt
   up only to tell the user, makes way for the new one.

A failed check rejects with the exception the spec gives for it (`NotAllowedError` or `SecurityError`), so honest
pages see the same error as from the renderer's own checks. Then `WebAuthnCoordinator` runs the ceremony:

- It builds `clientDataJSON` from the canonical origin, `crossOrigin` and `topOrigin`, with the spec's
  JSON-compatible serialization of client data, and hashes it. The authenticator only ever sees the hash.
- It hands the request to every authenticator that's present, and to keys plugged in while it waits. The first one
  the user touches wins, and the rest are cancelled (CTAPHID_CANCEL).
- It enforces the timeout, and cancels when the page asks to (`AbortSignal`), when the document stops being active
  (navigation, the back/forward cache, unload), or when the view closes.
- It turns authenticator results into `Result`s. For `attestation: "none"` (the default) and `"indirect"`, it
  replaces any attestation statement other than self attestation with a `none` statement, as the spec says, so the
  site gets no verifiable proof of the key's make and model. Level 3 keeps the AAGUID, which names the model without
  proving it. For `"direct"`, the prompt says the site will see the key's make and model, and offers to send `none`
  instead. `"enterprise"` is treated as `"direct"`, since no RP is configured for enterprise attestation.
- It reports errors the way the spec's privacy rules want: when a key holds no credential for the site, the user is
  told and can try another key, and the page hears nothing until the user cancels or the timeout ends. A page can't
  use the timing to find out whether a credential exists. A key that is already registered (`excludeCredentials`)
  says so only once the user has touched it, so the page gets its `InvalidStateError` right then, as the spec says,
  and the prompt stays up to tell the user until they close it or the page makes its next request.

`Authenticator` is the interface to an authenticator: `make_credential()`, `get_assertion()`, `wait_for_touch()`,
`get_pin_retries()`, `cancel()`, and what it supports (from `authenticatorGetInfo`). An `AuthenticatorSource` reports
the authenticators a ceremony may use, for as long as the ceremony holds the discovery it got. There are two sources:
USB security keys (`HIDAuthenticators`) and virtual authenticators (`VirtualAuthenticators`).

The prompt is the embedder's, through `ViewImplementation`, as with permission prompts:

```
Function<void(WebAuthnPrompt const&)> on_web_authn_prompt;   // show, or update the shown one
Function<void()> on_web_authn_prompt_closed;
void web_authn_cancel();
void web_authn_provide_pin(String);
void web_authn_choose_account(size_t index);
void web_authn_allow_attestation(bool);
```

`WebAuthnPrompt` says what the ceremony is waiting for (a key to be inserted or touched, a PIN with the retries left, a
choice of account, consent to share the key's make and model, or an error with what the user can do about it), for
which RP ID, origin, and top-level origin. A view without a prompt has no authenticators: its requests fail with
`NotAllowedError`, as headless views' do.
The PIN goes from the prompt to the coordinator and the authenticator, is never sent to WebContent or logged, and is
cleared after use.

### Talking to security keys

`HIDAuthenticator`, in LibWebView, wraps `libfido2`:

- Discovery with `fido_dev_info_manifest()`, polled while a ceremony waits, so a key plugged in late is used.
- Each operation runs on a thread of its own, since `fido_dev_make_cred()` and `fido_dev_get_assert()` block until the
  key is touched. The thread opens the key, makes the request, and closes the key again, so other programs can use it
  in between. The result goes back to the main event loop, and `fido_dev_cancel()` stops an operation from there.
- `fido_dev_get_cbor_info()` says what the key supports: `clientPin`, built-in `uv`, `rk`, `makeCredUvNotRqd`, and its
  algorithms. Keys that only speak U2F go through libfido2's U2F path, with `fido-u2f` attestation.
- User verification follows Chromium's rules. A key with a fingerprint reader verifies on the key. When UV is
  `required` or `preferred` and the key has a PIN, the prompt asks for it. When UV is `required` and the key has no PIN
  and no reader, the prompt says to set a PIN first (milestone 2 adds setting one). With several keys and a PIN to
  enter, the user first touches the key to use (`fido_dev_get_touch_begin()`), so the PIN goes to the right one.
- `appid`: when `allowCredentials` isn't found under the RP ID, it tries again with the `appid` URL as the RP. A
  credential registered over U2F was made with `SHA-256(appid)` as its application parameter, and CTAP2 keys accept
  it through `getAssertion` the same way. The `appid` extension output then says which one was used.
- The `attestationObject` is put back together from what libfido2 returns (`fmt`, the raw `attStmt`, and the raw
  authenticator data), since libfido2 doesn't keep the original bytes. LibWebView has a little CBOR of its own for
  that, for reading credential public keys, and for the virtual authenticators.

**Why libfido2.** It's the CTAP client OpenSSH and systemd use, maintained by Yubico, under Ladybird's license, and
it already handles CTAPHID framing and keep-alives, both PIN protocols, U2F fallback, and years of quirks in real
keys. Writing it from scratch, as WebKit and Chromium did, would be about 2,500 lines (CBOR, CTAPHID, IOKit and
hidraw, CTAP2, PIN protocols, U2F) before any of the quirks. The cost is a C library parsing what a USB device sends,
in the UI process, and a blocking API, which the threads handle. A key is local hardware the user plugged in, so this
is the same trust Chromium gives it in its browser process. If that ever looks too generous, `HIDAuthenticator`
can move into a helper process behind the same interface. Upstream might prefer its own CTAP code: everything above
`Authenticator` would stay the same.

No entitlement is needed on macOS. Xenon isn't sandboxed, and FIDO devices (usage page `0xF1D0`) don't need the
Input Monitoring permission that keyboards do.

### Prompts (Xenon)

`UI/WebAuthnPopover.{h,cpp}`: a popover over the view whose page asked, centered near its top, in Xenon's rose
palette. `Tab.cpp` and the extension popup each attach it to their view with one line. It shows:

- **Insert and touch your security key**: "Sign in to vault.bitwarden.com with a security key", or "Register a
  security key with ..." with the account being registered. For a cross-origin frame, also the origin asking and the
  site in the address bar ("Asked by https://vault.bitwarden.com, in a frame on chrome-extension://..."), which the
  spec asks for when creating.
- **Enter the key's PIN**, with the attempts left, and whether the last one was wrong; then **touch the key** to
  finish.
- **Choose an account**, for a key holding several discoverable credentials for the site.
- **Share the key's make and model?**, when the site asks for direct attestation.
- **Errors**: this key isn't registered with the site (try another), this key is already registered, set a PIN
  first, too many wrong PINs, the key can't do what the site asked, and no built-in authenticator.

Leaving the popover, by clicking elsewhere or pressing Escape, cancels the ceremony, as Cancel does.

A popover that closes drops its object name before it's deleted later, since the view finds its popover by that name.
A prompt can open in the same turn as the last one closes, when the page's next request takes over from a ceremony
that has answered it, and that prompt then gets a popover of its own rather than the one on its way out.

It is a popover rather than a sheet because of extension popups. An action popup is a `Qt::Popup`, which closes when
another window takes focus, and its page goes with it; a sheet on the browser window would end Bitwarden's two-step
login the moment it appeared. A popover opened over the popup stacks on it, the way Qt nests popups, and the popup
stays open underneath.

### Where each piece lives

| Piece | Engine patch | Xenon |
| --- | --- | --- |
| Credential Management's algorithms in `get()` and `create()` | 0010 | |
| `publickey-credentials-create` and `-get` features | 0011 | |
| `PublicKeyCredential`, the responses, the options, the JSON methods, `[[Create]]` and `[[DiscoverFromExternalSource]]` up to the client data | 0012 | |
| IPC, the UI process's checks, client data, `WebAuthnCoordinator`, `Authenticator`, the prompt callbacks | 0013 | |
| Virtual authenticators and WebAuthn's WebDriver commands, the tests | 0014 | `Meta/xenon.py test` checks that requests reach the UI process in Xenon's build |
| USB security keys through libfido2, and the vcpkg dependency | 0015 | |
| The prompts | | `UI/WebAuthnPopover`, attached in `UI/Tab.cpp` and `UI/Extensions/Popup.cpp` (a line each) |

## The patch series

Each patch is generic and could go upstream as it is: none mentions Xenon, Bitwarden, or extensions. They are
`Patches/ladybird/0010` to `0015`, after the extensions branch's `0001`–`0003` and xenon-ublock's `0004`–`0009`, and
none depends on those.

1. **LibWeb: Implement Credential Management's request and create algorithms.** `get()` and `create()` stop rejecting
   as not implemented. Testable on its own with WPT's `credential-management` tests.
2. **LibWebCommon: Add the publickey-credentials-create and -get permissions policy features.** Default allowlist
   `'self'`, so `<iframe allow="publickey-credentials-get">` means something.
3. **LibWeb+LibWebCommon: Add PublicKeyCredential and the WebAuthn API.** The bindings, the shared request and result
   types, and the renderer's half of the algorithms, ending in `PageClient::page_did_request_web_authn_*()`. With no
   embedder support, requests reject with `NotAllowedError`, which is what a browser with no authenticators does.
4. **LibURL+LibWeb+LibWebView+WebContent: Run WebAuthn in the UI process.** The IPC, the checks against
   canonical documents, client data, `Coordinator`, `Authenticator`, and the `ViewImplementation` callbacks. Moves
   `is_a_registrable_domain_suffix_of_or_is_equal_to()` into LibURL.
5. **LibWeb+LibWebView+WebDriver: Add WebAuthn virtual authenticators.** Software CTAP1/U2F and CTAP2
   authenticators (ES256, discoverable credentials, UV on demand), WebAuthn's WebDriver extension (§11: add and remove
   authenticators, credentials, user verification, credential properties), the `testdriver-vendor.js` and `Internals`
   hookup, and the tests. A tab with virtual authenticators uses only those, and runs its ceremonies without a
   prompt. Authenticators a test adds through `Internals` are its tab's alone, since test-web runs tests side by side.
6. **LibWebView: Use USB security keys through libfido2.** Adds `libfido2` to `vcpkg.json`, on macOS for now.

## Milestones

| Milestone | What | Done when |
| --- | --- | --- |
| **1. Security keys** | Patches `0010`–`0015` and the Xenon prompt. On macOS: `create()`, and `get()` with or without `allowCredentials`, choosing among a key's accounts. CTAP2 and U2F keys, PIN entry when a key needs one, keys with fingerprint readers, several keys at once. `residentKey` as Chromium treats it (`"required"` makes a discoverable credential; `"preferred"` makes one when the user is entering the key's PIN anyway). `appid`, `credProps`, `none` and `direct` attestation, `toJSON()` and the `parse*FromJSON()` methods, cross-origin iframes with `allow`. `isUserVerifyingPlatformAuthenticatorAvailable()` and `isConditionalMediationAvailable()` resolve `false`. | A USB security key registers and signs in on webauthn.io in Xenon. Bitwarden's WebAuthn two-step login completes in its popup. WPT's `webauthn` tests pass with a virtual authenticator, apart from those for later milestones. |
| **2. Passkeys on security keys** | Setting and changing a PIN. A **Security keys** section in settings: reset a key, list and delete its passkeys (`credMgmt`). `prf`/`hmac-secret`, `largeBlob`, `credProtect`, `minPinLength`, `appidExclude`. `getClientCapabilities()` reporting all of it. Linux. | Sign in to a site with a passkey on a YubiKey without typing a username. The same on Linux. |
| **3. Platform passkeys** (only with Apple's entitlement) | See below. | Sign in with an iCloud Keychain passkey, and with a phone. |

PIN entry is in milestone 1 rather than with discoverable credentials. Any key that has held a passkey has a PIN
set, since creating one asks for it. A CTAP2.0 key with a PIN refuses to register without it, and only CTAP2.1 keys
that offer `makeCredUvNotRqd` make an exception. webauthn.io also asks for UV `preferred` by default. Without PIN
entry, registering on webauthn.io would fail with many people's keys, and with libfido2 the PIN costs one more prompt
state.

### Platform passkeys (milestone 3)

Passkeys in iCloud Keychain, Touch ID, passkeys on a nearby phone (the hybrid transport), and passkeys in third-party
credential providers on macOS (1Password, Bitwarden's desktop app) all go through AuthenticationServices. A browser
asking on behalf of any site uses `ASAuthorizationWebBrowserPublicKeyCredentialManager` and the browser variants of the
platform and security key providers, which need Apple's managed `com.apple.developer.web-browser.public-key-credential`
entitlement. Getting it means:

- an Apple Developer Program membership, whose Account Holder files Apple's dedicated request form for it;
- signing Xenon with a Developer ID and a provisioning profile that has the entitlement, instead of today's ad-hoc
  signature (`codesign -s -` in `UI/Xenon.cmake`);
- Apple granting it. Developers of other browsers report long waits and entitlements that didn't show up in profiles
  after approval.

With it, a third `Authenticator`, `PlatformAuthenticator` (macOS only, in Objective-C++), gives the system the client
data hash, and the system shows its own UI. The same API can also reach security keys, but USB keys stay on libfido2,
which works without the entitlement and on Linux.

The alternatives without Apple: implement the hybrid transport (a QR code, Bluetooth advertisements, and a tunnel
service such as Google's) to reach passkeys on a phone, or keep a Xenon-only passkey store protected by Touch ID,
which needs at least a Developer ID for keychain access and syncs nowhere. Both are large, and neither reaches iCloud
Keychain on the Mac itself.

## Security and privacy

- **Origin binding.** The origin, `crossOrigin` and `topOrigin` in `clientDataJSON` come from the UI process's
  canonical documents, never from the renderer, which only names its document. A compromised renderer for one site
  can't get an assertion for another, and it can't claim to be same-origin with its ancestors.
- **RP IDs.** Checked twice: in LibWeb, for the spec's exceptions, and in the UI process, which decides. IP addresses
  and opaque origins are refused. `localhost` and `*.localhost` work, as in other browsers, for development. Related
  origin requests aren't supported, so an RP ID must be the origin's own domain or a registrable suffix of it.
- **Secure contexts.** The API is `[SecureContext]`, and the UI process checks again. An embedder scheme's pages (the
  extensions branch's `chrome-extension://`) are secure contexts, so they get the API. The spec's rules then allow
  only their own host (the extension ID) as their RP ID, which no website can use.
- **Cross-origin iframes.** Denied unless the embedder grants `publickey-credentials-get` or `-create` with `allow`.
  The UI process reads the document's policy from its own replicated copy. A cross-origin `create()` also needs
  transient activation, which it consumes. The renderer checks activation, since the UI process doesn't track it yet;
  the permissions policy check is what doesn't rest on the renderer. The prompt names the top-level site too, so a
  frame can't pass itself off as the page the user sees.
- **Focus and visibility.** Only a focused, visible view starts a ceremony. A background tab's request rejects with
  `NotAllowedError`, as in Chromium. Hiding or closing the tab, or navigating away, cancels the ceremony on the key.
  One ceremony per tab at a time, so a page can't stack prompts.
- **Timeouts.** Clamped to the spec's recommended 5 to 10 minutes, 5 by default.
- **No probing for credentials.** "Not registered here" goes to the user, not the page, until the user ends the
  ceremony, so the page can't tell from the result or its timing whether a credential exists. "Already registered"
  reaches the page only once the user has touched the key.
  Nothing about which keys are plugged in reaches a page.
- **Attestation.** `none` by default. A verifiable statement of the key's make and model (its attestation certificate)
  goes to a site only when it asks for `direct` attestation and the user agrees. The AAGUID always goes through, as
  Level 3 says; it names a model shared by many keys and proves nothing.
- **PINs** stay in the UI process: from the prompt to libfido2, then cleared. They're never logged or sent to a
  renderer. Challenges and credential IDs stay out of release logs too.
- **Device input.** libfido2 parses what USB devices send, in the UI process (see
  [Why libfido2](#talking-to-security-keys)).
- **Extension popups.** A request from a popup is shown in the popup, so it can't be answered by a page the user
  isn't looking at, and the popup can't close under it.

## Testing

- **Unit tests** (`Tests/LibWebView/TestWebAuthn.cpp`): `clientDataJSON` serialization and the attestation object
  against the spec's §16 test vectors, COSE to SPKI conversion for `getPublicKey()`, self attestation, malformed CBOR,
  and RP ID validation against the public suffix list (IP addresses, `localhost`, and suffixes like `github.io`).
- **Web tests with a virtual authenticator** (`Tests/LibWeb/Text/input/WebAuthn`, served through test-web's echo
  server): registering and signing in, with the assertion's signature checked through WebCrypto; the exclude list;
  an unknown credential; a cross-origin iframe with and without `allow="publickey-credentials-get"`, whose client data
  says `crossOrigin` and names the top origin; and the argument errors.
- **WPT**: eight of WPT's `webauthn` tests, imported, with `testdriver-vendor.js` implementing
  `add_virtual_authenticator()` and the rest through `Internals`. All 124 subtests pass. Ladybird's full WPT runs reach
  the same authenticators through the WebDriver extension.
- **WebDriver** (`Tests/LibWebView/test-webdriver-webauthn.py`): adds a virtual authenticator over WebDriver,
  registers a discoverable credential and signs in with it, lists it, adds one with its private key, and checks user
  verification and removal.
- **Xenon**: `Meta/xenon.py test` loads a page in headless Xenon that checks the API is there, that a request reaches
  the UI process (which answers `NotAllowedError`, a headless view having no prompt), and that another site's RP ID is
  refused.
- **By hand, with real keys** (a YubiKey 5 with and without a PIN, a U2F-only key, and two at once):
  - webauthn.io: register and authenticate with the defaults; then with UV required; then with discoverable credentials
    (milestone 2).
  - Bitwarden: two-step login with a security key, in the action popup (needs the extensions branch) and in a tab on
    vault.bitwarden.com.
  - Negative checks: a request from a background tab, an iframe without `allow`, an RP ID belonging to another site,
    pulling the key out mid-ceremony, a wrong PIN, cancelling, and navigating away while the prompt is up.

## Open decisions

- **Go-ahead for milestone 1** as planned here, including libfido2 as a new dependency.
- **Which authenticator the Bitwarden account uses.** A USB security key works with milestone 1. A passkey in iCloud
  Keychain or on a phone needs milestone 3, which needs Apple's entitlement, or a different second factor on the
  account.
- **Platform passkeys**: whether to apply for the entitlement (milestone 3), and when.

## Status

- [x] Design (this document).
- [x] Milestone 1, built: patches `0010`–`0015` and `UI/WebAuthnPopover`. Its tests pass with virtual authenticators.
  Discoverable sign-in with the account chooser came along with it. Not yet verified: a real security key on
  webauthn.io, and Bitwarden's two-step login in its popup, both by hand.
- [ ] Milestone 2: setting and changing a PIN, managing a key's passkeys, the remaining extensions, and Linux.
- [ ] Milestone 3: platform passkeys (undecided).

This branch is based on the `xenon-ublock` branch, so that the WebAuthn patches come after its `0009`.
