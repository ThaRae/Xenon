// The test extension's background service worker, which tests.js talks to.
const events = [];

chrome.runtime.onInstalled.addListener(details => events.push(`installed:${details.reason}`));
chrome.runtime.onStartup.addListener(() => events.push("startup"));
chrome.alarms.onAlarm.addListener(alarm => events.push(`alarm:${alarm.name}`));

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    switch (message?.toBackground) {
        case "ping":
            sendResponse({
                pong: true,
                isWorker: typeof window === "undefined" && typeof importScripts === "function",
                senderUrl: sender.url,
                location: location.pathname,
            });
            return undefined;
        case "events":
            sendResponse(events);
            return undefined;
        case "set-alarm":
            chrome.alarms.create("test", { when: Date.now() + 100 }).then(() => sendResponse(true));
            return true;
        default:
            return undefined;
    }
});

chrome.runtime.onConnect.addListener(port => {
    port.onMessage.addListener(message => port.postMessage({ echo: message, name: port.name }));
});
