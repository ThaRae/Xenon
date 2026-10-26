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
    check("no chrome.tabs yet", chrome.tabs === undefined);

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

    console.log(`XENON-EXTENSION-TEST ${JSON.stringify(results)}`);
    window.close();
}

run().catch(error => {
    results.error = String(error?.stack ?? error);
    console.log(`XENON-EXTENSION-TEST ${JSON.stringify(results)}`);
    window.close();
});
