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
    if (event instanceof ErrorEvent)
        console.error(`Background script error: ${event.message} (${event.filename}:${event.lineno}:${event.colno})`);
    else console.error("The background script couldn't load, or its worker stopped.");
});
