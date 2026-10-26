// The hidden page that hosts an extension's background service worker.
//
// Ladybird doesn't run service workers yet, so Xenon runs the extension's background script in a dedicated worker of
// this page, with the global a service worker's code expects: self, importScripts(), fetch(), and no DOM. Like
// chrome-api.js, this runs before anything else in the page, given `bridge`, the page's channel to the UI process, and
// `config`. The page relays between the worker and the UI process; the worker's chrome.* (see worker-bridge.js) talks
// to the UI process through it.

"use strict";

const worker = new Worker("/_generated_background_worker.js", {
    type: config.module ? "module" : "classic",
    name: "background",
});

const channel = new MessageChannel();
worker.postMessage({ xenonBridge: true }, [channel.port2]);
channel.port1.onmessage = event => bridge.postMessage(event.data);
bridge.onmessage = text => channel.port1.postMessage(text);

worker.addEventListener("error", event => {
    console.error(`Background script error: ${event.message} (${event.filename}:${event.lineno}:${event.colno})`);
});
