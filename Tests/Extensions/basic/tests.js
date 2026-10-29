// Runs in Meta/xenon.py test: checks the chrome.* APIs of an extension page, prints one line of results, and closes the
// page, which ends the headless browser.
const results = {};

function check(name, condition) {
    results[name] = Boolean(condition);
}

async function expectRejection(name, promise, text) {
    try {
        await promise;
        check(name, false);
    } catch (error) {
        check(name, String(error.message).includes(text));
    }
}

const frameReady = new Promise(resolve =>
    window.addEventListener("message", event => {
        if (event.data === "frame-ready") resolve();
    })
);

async function run() {
    const id = chrome.runtime.id;
    check("origin", location.origin === `chrome-extension://${id}`);
    check("secure context", isSecureContext && typeof crypto.subtle === "object");
    check("runtime.getURL", chrome.runtime.getURL("/a/b.html") === `chrome-extension://${id}/a/b.html`);
    check("runtime.getManifest", chrome.runtime.getManifest().name === "Xenon test extension");
    check("runtime.getPlatformInfo", ["mac", "linux", "win"].includes((await chrome.runtime.getPlatformInfo()).os));

    check("i18n.getMessage", chrome.i18n.getMessage("greeting", "Xenon") === "Hello, Xenon!");
    check("i18n.getMessage missing", chrome.i18n.getMessage("missing") === "");
    check("i18n @@extension_id", chrome.i18n.getMessage("@@extension_id") === id);
    check("i18n.getUILanguage", /^[a-z]{2,3}(-|$)/i.test(chrome.i18n.getUILanguage()));

    const changes = [];
    chrome.storage.onChanged.addListener((change, area) => changes.push({ change, area }));
    const localChanges = [];
    chrome.storage.local.onChanged.addListener(change => localChanges.push(change));

    await chrome.storage.local.clear();
    await chrome.storage.local.set({ a: 1, b: { c: [1, 2] } });
    const items = await chrome.storage.local.get(["a", "b", "missing"]);
    check("storage.local set and get", items.a === 1 && items.b.c[1] === 2 && !("missing" in items));
    const withDefaults = await chrome.storage.local.get({ a: 0, z: "default" });
    check("storage.get with defaults", withDefaults.a === 1 && withDefaults.z === "default");
    check("storage.getBytesInUse", (await chrome.storage.local.getBytesInUse("a")) === 2);
    check("storage.getKeys", (await chrome.storage.local.getKeys()).sort().join() === "a,b");
    await chrome.storage.local.remove("a");
    check("storage.remove", !("a" in (await chrome.storage.local.get(null))));
    await chrome.storage.session.set({ s: true });
    check("storage.session", (await chrome.storage.session.get("s")).s === true);
    await expectRejection(
        "storage.sync quota",
        chrome.storage.sync.set({ big: "x".repeat(9000) }),
        "QUOTA_BYTES_PER_ITEM"
    );
    await expectRejection("storage.managed is read-only", chrome.storage.managed.set({ a: 1 }), "read-only");

    await new Promise(resolve =>
        chrome.storage.local.get("b", items => {
            check("callbacks", items.b.c.length === 2 && chrome.runtime.lastError === undefined);
            resolve();
        })
    );

    await new Promise(resolve => setTimeout(resolve, 50));
    check(
        "storage.onChanged",
        changes.some(({ change, area }) => area === "local" && change.a?.newValue === 1) &&
            changes.some(({ change, area }) => area === "session" && change.s?.newValue === true)
    );
    check(
        "storage.local.onChanged",
        localChanges.some(change => change.a?.oldValue === 1 && !("newValue" in change.a))
    );

    await frameReady;
    const reply = await chrome.runtime.sendMessage({ ping: 7 });
    check(
        "runtime.sendMessage",
        reply?.pong === 7 && reply.senderId === id && reply.senderUrl === location.href.split("#")[0]
    );
    check("runtime.sendMessage async response", (await chrome.runtime.sendMessage({ later: 3 }))?.later === 3);

    await new Promise(resolve =>
        chrome.runtime.sendMessage({ unanswered: true }, response => {
            check(
                "runtime.lastError when nothing answers",
                response === undefined && chrome.runtime.lastError?.message.includes("message port closed")
            );
            resolve();
        })
    );

    // The background service worker starts with the browser, so it may still be starting.
    let pong;
    for (let attempt = 0; attempt < 50 && !pong; ++attempt) {
        pong = await chrome.runtime.sendMessage({ toBackground: "ping" }).catch(() => undefined);
        if (!pong) await new Promise(resolve => setTimeout(resolve, 100));
    }
    check("background worker answers", pong?.pong === true && pong.senderUrl === location.href.split("#")[0]);
    check("background is a worker", pong?.isWorker === true);
    // Meta/xenon.py runs this page twice in one profile, and checks the first run installs and the second starts up.
    const backgroundEvents = await chrome.runtime.sendMessage({ toBackground: "events" });
    results["info: background events"] = backgroundEvents;
    check("background gets onInstalled or onStartup", backgroundEvents?.includes("installed:install") || backgroundEvents?.includes("startup"));
    check(
        "background is installed and activated first, and imports scripts while installing",
        backgroundEvents?.[0] === "install:true" && backgroundEvents[1] === "activate:function"
    );

    const port = chrome.runtime.connect({ name: "test-port" });
    const echo = await new Promise(resolve => {
        port.onMessage.addListener(resolve);
        port.postMessage({ hello: "background" });
        setTimeout(() => resolve(undefined), 2000);
    });
    check("runtime.connect ports", echo?.echo?.hello === "background" && echo.name === "test-port");
    port.disconnect();

    await chrome.runtime.sendMessage({ toBackground: "set-alarm" });
    await new Promise(resolve => setTimeout(resolve, 400));
    check("alarms fire in the background", (await chrome.runtime.sendMessage({ toBackground: "events" }))?.includes("alarm:test"));
    check("alarms.getAll after firing", (await chrome.alarms.getAll()).length === 0);

    const contexts = await chrome.runtime.getContexts({});
    check(
        "runtime.getContexts",
        contexts.some(context => context.contextType === "BACKGROUND") &&
            contexts.some(context => context.contextType === "TAB" && context.documentUrl === location.href)
    );
    const self = await chrome.management.getSelf();
    check("management.getSelf", self.id === id && self.installType === "development" && self.version === "1.0");
    check(
        "permissions.contains",
        (await chrome.permissions.contains({ permissions: ["storage", "alarms"] })) &&
            !(await chrome.permissions.contains({ permissions: ["cookies"] }))
    );
    check("tabs.query", Array.isArray(await chrome.tabs.query({})));
    const tabPortError = await new Promise(resolve => {
        const tabPort = chrome.tabs.connect(0);
        tabPort.onDisconnect.addListener(() => resolve(chrome.runtime.lastError?.message));
    });
    check("tabs.connect disconnects without content scripts", tabPortError?.includes("Receiving end does not exist"));
    check("windows.getAll", Array.isArray(await chrome.windows.getAll({ populate: true })));

    // clipboardWrite and clipboardRead let an extension's pages copy and paste from script, with no user gesture.
    const clipboardText = "Xenon clipboard test";
    const textarea = document.createElement("textarea");
    document.body.append(textarea);
    textarea.focus();
    textarea.value = clipboardText;
    textarea.select();
    check("execCommand copy without a user gesture", document.execCommand("copy"));
    textarea.value = "";
    check("execCommand paste", document.execCommand("paste") && textarea.value === clipboardText);
    textarea.remove();

    // Optional permissions need the user. Meta/xenon.py exposes internals, which stands in for the user's gesture, but a
    // headless browser has no one to ask, so it denies.
    check("permissions.contains an optional one", (await chrome.permissions.contains({ permissions: ["bookmarks"] })) === false);
    check("permissions.request needs a user gesture", await chrome.permissions.request({ permissions: ["bookmarks"] }).then(() => false, error => /user gesture/.test(error.message)));
    internals.grantTransientActivation();
    check("permissions.request asks", (await chrome.permissions.request({ permissions: ["bookmarks"] })) === false);
    check("permissions.request denied", (await chrome.permissions.contains({ permissions: ["bookmarks"] })) === false);
    check("permissions.remove of a required one", await chrome.permissions.remove({ permissions: ["storage"] }).then(() => false, () => true));
    check("permissions.getAll", (await chrome.permissions.getAll()).permissions.includes("privacy"));

    // An earlier run may have left the setting controlled.
    const passwordSaving = chrome.privacy.services.passwordSavingEnabled;
    await passwordSaving.clear({});
    check("privacy setting starts controllable", (await passwordSaving.get({})).levelOfControl === "controllable_by_this_extension");
    const settingChanged = new Promise(resolve => passwordSaving.onChange.addListener(resolve));
    await passwordSaving.set({ value: false });
    const controlled = await passwordSaving.get({});
    check("privacy setting controlled", controlled.levelOfControl === "controlled_by_this_extension" && controlled.value === false);
    check("privacy setting onChange", (await settingChanged).levelOfControl === "controlled_by_this_extension");
    check("privacy setting takes booleans", await passwordSaving.set({ value: "no" }).then(() => false, () => true));
    await passwordSaving.clear({});
    check("privacy setting cleared", (await passwordSaving.get({})).levelOfControl === "controllable_by_this_extension");
    check("no native messaging without its permission", chrome.runtime.connectNative === undefined);

    // Meta/xenon.py serves JSON with no CORS headers from a host the extension has permission for, which its pages and
    // its background reach as Chrome lets them: without CORS. The header would otherwise need a preflight.
    const dataURL = new URLSearchParams(location.search).get("data");
    if (dataURL) {
        const fetched = await fetch(dataURL, { headers: { "X-Xenon-Test": "1" } })
            .then(response => response.json())
            .catch(error => ({ error: String(error) }));
        check("fetch from a permitted host, without CORS", fetched?.ok === true);
        const fetchedInBackground = await chrome.runtime.sendMessage({ toBackground: "fetch", url: dataURL });
        check("the background fetches from a permitted host", fetchedInBackground?.ok === true);
    }

    // Headless runs have no windows, and so no tabs. A tab that doesn't exist has no frames.
    check("webNavigation.getFrame of no tab", (await chrome.webNavigation.getFrame({ tabId: 987654, frameId: 0 })) === null);
    check("webNavigation.getAllFrames of no tab", (await chrome.webNavigation.getAllFrames({ tabId: 987654 })) === null);
    check("webNavigation events", typeof chrome.webNavigation.onCommitted.addListener === "function");
    check("webRequest events", typeof chrome.webRequest?.onAuthRequired?.addListener === "function");
    check("notifications.create", typeof (await chrome.notifications.create("test", { type: "basic" })) === "string");
    check("scripting.executeScript fails", await chrome.scripting.executeScript({ target: { tabId: 987654 }, func: () => 1 }).then(() => false, () => true));

    check("offscreen.hasDocument before", (await chrome.offscreen.hasDocument()) === false);
    await chrome.offscreen.createDocument({ url: "offscreen.html", reasons: ["CLIPBOARD"], justification: "test" });
    let offscreen;
    for (let attempt = 0; attempt < 50 && !offscreen; ++attempt) {
        offscreen = await chrome.runtime.sendMessage({ toOffscreen: "ping", copied: clipboardText }).catch(() => undefined);
        if (!offscreen) await new Promise(resolve => setTimeout(resolve, 100));
    }
    check(
        "offscreen document",
        offscreen?.offscreen && offscreen.hasDOM && offscreen.localStorage && offscreen.onlyRuntime
    );
    check("offscreen document clears the clipboard", offscreen?.clearedClipboard);
    check(
        "offscreen in getContexts",
        (await chrome.runtime.getContexts({ contextTypes: ["OFFSCREEN_DOCUMENT"] })).length === 1
    );
    // Meta/xenon.py turns content blocking on, and serves an image the bundled lists block on web pages. Extensions'
    // hidden pages go without the content blocker, as uBlock Origin doesn't see other extensions' requests in Chrome.
    if (dataURL) {
        const image = await chrome.runtime.sendMessage({
            toOffscreen: "image",
            url: new URL("/site-tracking-pixel.gif", dataURL).href,
        });
        check("the content blocker leaves offscreen documents alone", image?.loaded === true);
    }
    await chrome.offscreen.closeDocument();
    check("offscreen.hasDocument after", (await chrome.offscreen.hasDocument()) === false);

    console.log(`XENON-EXTENSION-TEST ${JSON.stringify(results)}`);
    window.close();
}

run().catch(error => {
    results.error = String(error?.stack ?? error);
    console.log(`XENON-EXTENSION-TEST ${JSON.stringify(results)}`);
    window.close();
});
