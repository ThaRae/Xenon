# Filter list updates

Xenon ships uBlock Origin's filter lists, converted for Ladybird by [xenon-ublock](../../xenon-ublock), in
`UI/Resources/filter-lists`. Upstream lists change every day, so Xenon fetches them every day between releases.
The lists come from a public repository on git.avx512.dev, which rebuilds them daily and signs them.

## The published lists

[tharae/xenon-filter-lists](https://git.avx512.dev/tharae/xenon-filter-lists) is public, so that Xenon can fetch from it
without an account. Xenon and xenon-ublock stay private. Its workflow (`.gitea/workflows/update.yml`, copied from
`Meta/ci/filter-lists-workflow.yml` here) runs at 05:17 UTC every day on slab, and can be run by hand. It:

1. clones Xenon and xenon-ublock with a read-only token;
2. runs `Meta/ci/publish-filter-lists.sh`, which runs `Meta/update-filter-lists.py --output` into the checkout. That is
   the same build, exclusions and license checks as the lists Xenon bundles;
3. signs `manifest.json` with Ed25519 into `manifest.json.sig`, and checks the signature against the public key in
   `UI/FilterListUpdates.cpp` before going on;
4. commits the result to `main`, one commit a day.

The repository's root is a bundle, laid out like `UI/Resources/filter-lists`:

| File | What it is |
| --- | --- |
| `manifest.json` | The bundle's `format`, `generated_at`, and every list (`lists[]`) and the resources (`resources`), each with its path, size and SHA-256 |
| `manifest.json.sig` | The base64 Ed25519 signature of `manifest.json`'s bytes |
| `lists/*.txt` | Lists Xenon passes as `--content-blocker-list` |
| `trusted-lists/*.txt` | uBlock Origin's own lists, passed as `--trusted-content-blocker-list`, which may use the scriptlets uBO reserves for its lists |
| `resources/ublock-resources.json` | uBO's scriptlets and redirect surrogates, passed as `--content-blocker-resources` |
| `NOTICE.txt`, `LICENSE-GPL-3.0.txt` | Each list's source and license |

Xenon fetches each file from `https://git.avx512.dev/tharae/xenon-filter-lists/raw/branch/main/<path>`.

### Secrets

On tharae/xenon-filter-lists, under Settings → Actions → Secrets:

- `XENON_READ_TOKEN`: a Gitea access token with `read:repository`, to clone Xenon and xenon-ublock. The job's own token
  pushes the lists.
- `FILTER_LISTS_SIGNING_KEY`: the Ed25519 private key, in PEM (`openssl genpkey -algorithm ed25519`). Keep a copy
  somewhere safe. Without it, new lists need a new key, and a Xenon release that has it.

To change the key, generate a new one and put its public half in `PUBLIC_KEY` in `UI/FilterListUpdates.cpp`:

```bash
openssl pkey -in key.pem -pubout -outform DER | tail -c 32 | base64
```

Builds with the old key stop taking updates. They keep the newest lists they had, which are still newer than their
bundled ones.

### Formats

The manifest's `format` is `FORMAT` in `Meta/update-filter-lists.py` and `BUNDLE_FORMAT` in `UI/FilterListBundle.h`.
Bump both together whenever a Xenon built before the change couldn't use the new output: a new kind of file, a new
Ladybird option, or lists that need engine patches it lacks. A Xenon that finds a format it doesn't read keeps its lists
until it's updated.

## In Xenon

`UI/FilterListUpdates.cpp` does the updating, and `UI/FilterListBundle.cpp` checks bundles. Once a day it:

1. fetches `manifest.json` and `manifest.json.sig`, checks the signature, and checks that every file stays in the
   layout above, within Ladybird's size limits (64 MiB a file, 256 MiB in all);
2. stops there unless the manifest is in its format and newer (`generated_at`) than the lists in use;
3. copies the files whose hash hasn't changed from the lists it has, downloads the rest, and checks each against its
   size and hash, in `ContentBlocking/Xenon/staging` in the profile's data directory;
4. renames the staging directory after the lists' time (`ContentBlocking/Xenon/202610270517`), records it in
   `ContentBlocking/Xenon/state.json`, and hands the new paths to Ladybird. Ladybird reapplies them to every page
   (engine patch `0020`, `Application::set_explicit_content_blockers()`). Then it removes the older lists.

At startup, Xenon takes the bundled lists first, as before. Once the profile is known, it switches to the downloaded
ones if their signature still checks out, every file is there at its size, and they're newer than the bundled ones. A
Xenon update with newer bundled lists goes back to those.

Anything that fails, such as the network, a signature, a hash, or the disk, leaves the lists in use as they were, and
Xenon tries again an hour later. A file whose hash doesn't match can also mean the repository moved on between the
manifest and the file, which the next try sorts out. Gitea marks raw files cacheable for six hours, so a cache in
between can delay an update by up to that long.

Downloads go through RequestServer, with no cookies and no HTTP cache, and follow up to five redirects (HTTPS only).

### Settings

Updates run while Ladybird's own **Filter list updates** and **Allow automatic service connections** switches are on
(Settings → Services), and content blocking is on. Ladybird's switch also updates the lists in its own catalog, which
Xenon's welcome flow turns off. The welcome flow has a **Get new lists every day** checkbox, on by default. Turning
updates off keeps the newest lists Xenon has.

Profiles set up before this feature have updates off, since the welcome flow used to turn them off. Turn them on in
Settings → Services.

## Testing

`./Meta/xenon.py test` runs a local server that publishes the bundles in `Tests/FilterListUpdates`, signed with a test
key, and points a headless Xenon at it with `XENON_FILTER_LIST_UPDATES_URL` and
`XENON_FILTER_LIST_UPDATES_PUBLIC_KEY`. One profile goes through four runs:

1. it takes the first bundle while it runs;
2. it uses that bundle at the next start without fetching anything;
3. a day later it takes the next bundle, downloading only the list that changed;
4. it turns down a bundle whose list doesn't match its manifest.

`Tests/TestFilterLists.cpp` covers the manifest and signature checks. `Tests/FilterListUpdates/make-bundles.py`
regenerates the test bundles and key, and needs OpenSSL 3 or later rather than macOS's LibreSSL.
