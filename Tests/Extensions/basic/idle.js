// Runs in Meta/xenon.py test, with --extension-idle-timeout=2: the background worker stops once it has been idle that
// long, as Chrome stops a service worker, and starts again for a message, or for an event it has a listener for. Prints
// one line of results, and closes the page, which ends the headless browser.
const results = {};

function check(name, condition) {
    results[name] = Boolean(condition);
}

const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

// NB: These calls are the page's, which keep nothing else running.
async function backgroundIsRunning() {
    return (await chrome.runtime.getContexts({ contextTypes: ["BACKGROUND"] })).length > 0;
}

async function backgroundStops() {
    for (const end = Date.now() + 10000; Date.now() < end; await sleep(100)) {
        if (!(await backgroundIsRunning())) return true;
    }
    return false;
}

async function run() {
    // The background starts with the browser, so it may still be starting.
    let first;
    for (let attempt = 0; attempt < 50 && !first; ++attempt) {
        first = await chrome.runtime.sendMessage({ toBackground: "ping" }).catch(() => undefined);
        if (!first) await sleep(100);
    }
    check("background answers", first?.pong === true);

    const port = chrome.runtime.connect({ name: "idle" });
    const disconnected = new Promise(resolve => port.onDisconnect.addListener(() => resolve(true)));

    check("background stops when idle", await backgroundStops());
    check("its ports close as it stops", await Promise.race([disconnected, sleep(1000).then(() => false)]));

    const second = await chrome.runtime.sendMessage({ toBackground: "ping" });
    check("a message starts it again", second?.pong === true && second.instance !== first?.instance);
    const events = await chrome.runtime.sendMessage({ toBackground: "events" });
    results["info: events after starting again"] = events;
    check(
        "starting again is no new session",
        Array.isArray(events) && !events.includes("startup") && !events.some(event => event.startsWith("installed:"))
    );

    check("it stops again", await backgroundStops());
    // The background has no storage.onChanged listener.
    await chrome.storage.local.set({ idle: Date.now() });
    await sleep(500);
    check("an event it has no listener for leaves it stopped", !(await backgroundIsRunning()));

    const woken = new Promise(resolve =>
        chrome.storage.onChanged.addListener(changes => {
            if (changes.wokenByAlarm) resolve(changes.wokenByAlarm.newValue);
        })
    );
    await chrome.alarms.create("wake", { when: Date.now() + 100 });
    const wokenBy = await Promise.race([woken, sleep(5000).then(() => undefined)]);
    check("an event it has a listener for starts it again", typeof wokenBy === "string" && wokenBy !== second?.instance);

    console.log(`XENON-EXTENSION-TEST ${JSON.stringify(results)}`);
    window.close();
}

run().catch(error => {
    results.error = String(error?.stack ?? error);
    console.log(`XENON-EXTENSION-TEST ${JSON.stringify(results)}`);
    window.close();
});
