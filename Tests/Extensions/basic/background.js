// The test extension's background service worker, which tests.js and idle.js talk to.
const events = [];
// Which run of the worker this is: idle.js checks that it starts again once it has stopped.
const instance = crypto.randomUUID();

chrome.runtime.onInstalled.addListener(details => events.push(`installed:${details.reason}`));

// A service worker's lifecycle events, before onInstalled and onStartup. Proton Pass imports its chunks from oninstall.
self.oninstall = () => {
    importScripts("installed-chunk.js");
    events.push(`install:${self.xenonInstalledChunk === true}`);
    return self.skipWaiting();
};
self.addEventListener("activate", event => events.push(`activate:${typeof event.waitUntil}`));
chrome.runtime.onStartup.addListener(() => events.push("startup"));
chrome.alarms.onAlarm.addListener(alarm => {
    events.push(`alarm:${alarm.name}`);
    // idle.js sets this one once the worker has stopped, and hears of it from storage.onChanged.
    if (alarm.name === "wake") chrome.storage.session.set({ wokenByAlarm: instance });
});

// What runScriptingTests() checks of webNavigation's events.
const navigationEvents = [];
for (const name of ["onBeforeNavigate", "onCommitted", "onDOMContentLoaded", "onCompleted", "onErrorOccurred"])
    chrome.webNavigation[name].addListener(details => navigationEvents.push({ name, ...details }));

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (sender.id === undefined) events.push("onMessage got a web page's message");
    switch (message?.toBackground) {
        case "ping":
            sendResponse({
                pong: true,
                isWorker: typeof window === "undefined" && typeof importScripts === "function",
                senderUrl: sender.url,
                location: location.pathname,
                instance,
            });
            return undefined;
        case "events":
            sendResponse(events);
            return undefined;
        case "sender":
            sendResponse({ id: sender.id, url: sender.url, origin: sender.origin });
            return undefined;
        case "scripting":
            runScriptingTests(sender).then(sendResponse, error => sendResponse({ [`no exception: ${error}`]: false }));
            return true;
        case "registered-ran":
            registeredScriptRan?.(message.path);
            return undefined;
        case "set-alarm":
            chrome.alarms.create("test", { when: Date.now() + 100 }).then(() => sendResponse(true));
            return true;
        case "fetch":
            fetch(message.url, { headers: { "X-Xenon-Test": "1" } })
                .then(response => response.json())
                .then(sendResponse, error => sendResponse({ error: String(error) }));
            return true;
        default:
            return undefined;
    }
});

chrome.runtime.onConnect.addListener(port => {
    port.onMessage.addListener(message => port.postMessage({ echo: message, name: port.name }));
});

// Meta/xenon.py's externally connectable page (externally-connectable.html), a web page that messages the extension.
chrome.runtime.onMessageExternal.addListener((message, sender, sendResponse) => {
    switch (message?.toBackground) {
        case "ping": {
            const { id, url, origin, frameId } = sender;
            sendResponse({ pong: true, sender: { id, url, origin, frameId, tabId: sender.tab?.id } });
            return undefined;
        }
        case "later":
            setTimeout(() => sendResponse({ later: true }), 10);
            return true;
        case "events":
            sendResponse(events);
            return undefined;
        default:
            return undefined;
    }
});

chrome.runtime.onConnectExternal.addListener(port => {
    port.onMessage.addListener(message =>
        port.postMessage({ echo: message, name: port.name, hasSenderId: "id" in port.sender })
    );
});

// chrome.scripting and chrome.webNavigation, run against the tab of the content script that asks (content-idle.js), on
// Meta/xenon.py's content script page, which has a frame at /frame.
let registeredScriptRan;

async function runScriptingTests(sender) {
    const results = {};
    const check = (name, condition) => (results[name] = Boolean(condition));
    const expectRejection = async (name, promise, text) => {
        try {
            await promise;
            check(name, false);
        } catch (error) {
            check(name, String(error.message).includes(text));
        }
    };

    const tabId = sender.tab?.id;
    check(
        "content scripts' messages come from their tab and frame",
        typeof tabId === "number" && sender.frameId === 0 && typeof sender.documentId === "string"
    );
    const target = { tabId };

    const [sum] = await chrome.scripting.executeScript({ target, func: (a, b) => a + b, args: [2, 3] });
    check(
        "executeScript runs a function with its arguments",
        sum?.result === 5 && sum.frameId === 0 && sum.documentId === sender.documentId
    );

    const [fromFile] = await chrome.scripting.executeScript({ target, files: ["injected.js"] });
    check("executeScript runs files in the content scripts' world", fromFile?.result === "isolated:start,end,idle");

    const [mainWorld] = await chrome.scripting.executeScript({
        target,
        world: "MAIN",
        func: () => `${typeof window.xenonPageSecret}:${typeof chrome}`,
    });
    check("executeScript runs in the page's world when asked", mainWorld?.result === "string:undefined");

    const [promised] = await chrome.scripting.executeScript({
        target,
        func: () => new Promise(resolve => setTimeout(() => resolve({ later: true }), 10)),
    });
    check("executeScript waits for a promise", promised?.result?.later === true);

    const [thrown] = await chrome.scripting.executeScript({
        target,
        func: () => {
            throw new Error("An injected function's error");
        },
    });
    check("an injected function's exception gives no result", thrown !== undefined && !("result" in thrown));

    const frames = await chrome.webNavigation.getAllFrames({ tabId });
    const child = frames?.find(frame => frame.frameId !== 0);
    check(
        "webNavigation.getAllFrames",
        frames?.length === 2 && child?.parentFrameId === 0 && new URL(child.url).pathname === "/frame"
    );
    const frame = await chrome.webNavigation.getFrame({ tabId, frameId: child?.frameId });
    check("webNavigation.getFrame", frame?.parentFrameId === 0 && frame.documentId === child?.documentId);

    const everywhere = await chrome.scripting.executeScript({
        target: { tabId, allFrames: true },
        func: () => location.pathname,
    });
    check(
        "executeScript in all frames",
        everywhere.length === 2 && everywhere.some(result => result.frameId === child?.frameId && result.result === "/frame")
    );
    const [inFrame] = await chrome.scripting.executeScript({
        target: { tabId, frameIds: [child?.frameId] },
        func: () => location.pathname,
    });
    check("executeScript in one frame", inFrame?.result === "/frame");

    const css = "#marker { outline: 3px solid rgb(4, 5, 6); }";
    const outline = () => getComputedStyle(document.getElementById("marker")).outlineColor;
    await chrome.scripting.insertCSS({ target, css });
    const [styled] = await chrome.scripting.executeScript({ target, func: outline });
    await chrome.scripting.removeCSS({ target, css });
    const [unstyled] = await chrome.scripting.executeScript({ target, func: outline });
    check("insertCSS and removeCSS", styled?.result === "rgb(4, 5, 6)" && unstyled?.result !== "rgb(4, 5, 6)");

    await expectRejection("executeScript needs a function or files", chrome.scripting.executeScript({ target }), "Exactly one of");
    await expectRejection(
        "executeScript needs a tab that exists",
        chrome.scripting.executeScript({ target: { tabId: 987654 }, func: () => 1 }),
        "No tab with id"
    );

    const reply = await chrome.tabs.sendMessage(tabId, { toContentScript: "ping" });
    check("tabs.sendMessage reaches content scripts", reply?.pong === "/content-scripts");
    await expectRejection(
        "tabs.sendMessage to a frame without content scripts",
        chrome.tabs.sendMessage(tabId, { toContentScript: "ping" }, { frameId: child?.frameId }),
        "Receiving end does not exist"
    );

    const registration = {
        id: "registered",
        matches: ["http://127.0.0.1/frame*"],
        js: ["registered.js"],
        allFrames: true,
        runAt: "document_start",
        persistAcrossSessions: false,
    };
    await chrome.scripting.registerContentScripts([registration]);
    const registered = await chrome.scripting.getRegisteredContentScripts();
    check(
        "registerContentScripts",
        registered.length === 1 && registered[0].id === "registered" && registered[0].world === "ISOLATED"
    );
    await expectRejection(
        "registerContentScripts refuses an ID it has",
        chrome.scripting.registerContentScripts([registration]),
        "Duplicate script ID"
    );

    // A registered script runs in the documents that load after it, such as the frame's once it reloads.
    const ran = new Promise(resolve => (registeredScriptRan = resolve));
    await chrome.scripting.executeScript({ target: { tabId, frameIds: [child?.frameId] }, func: () => location.reload() });
    const timedOut = new Promise(resolve => setTimeout(() => resolve(undefined), 5000));
    check("registered content scripts run", (await Promise.race([ran, timedOut])) === "/frame");

    await chrome.scripting.unregisterContentScripts({ ids: ["registered"] });
    check("unregisterContentScripts", (await chrome.scripting.getRegisteredContentScripts()).length === 0);

    // webNavigation's events follow the frame as it navigates, and when a navigation fails.
    const waitFor = async condition => {
        for (let tries = 0; tries < 100 && !condition(); ++tries) await new Promise(resolve => setTimeout(resolve, 50));
        return condition();
    };
    const navigate = url =>
        chrome.scripting.executeScript({
            target: { tabId, frameIds: [child?.frameId] },
            func: url => {
                location.href = url;
            },
            args: [url],
        });
    const eventsFor = suffix => navigationEvents.filter(event => event.frameId === child?.frameId && event.url.endsWith(suffix));

    await navigate("/frame?navigated");
    await waitFor(() => eventsFor("?navigated").some(event => event.name === "onCompleted"));
    const sequence = eventsFor("?navigated");
    results["info: navigation events"] = sequence.map(event => event.name);
    check(
        "webNavigation events come in order",
        sequence.map(event => event.name).join() === "onBeforeNavigate,onCommitted,onDOMContentLoaded,onCompleted"
    );
    const committed = sequence.find(event => event.name === "onCommitted");
    const completed = sequence.find(event => event.name === "onCompleted");
    check(
        "webNavigation events' details",
        sequence.every(event => event.tabId === tabId && event.parentFrameId === 0 && event.frameType === "sub_frame") &&
            committed?.transitionType === "auto_subframe" &&
            typeof committed.documentId === "string" &&
            committed.documentId === completed?.documentId
    );

    await navigate("http://127.0.0.1:1/");
    check(
        "webNavigation.onErrorOccurred",
        await waitFor(() => eventsFor("127.0.0.1:1/").some(event => event.name === "onErrorOccurred" && event.error))
    );
    results["info: failed navigation events"] = eventsFor("127.0.0.1:1/").map(event => event.name);
    return results;
}
