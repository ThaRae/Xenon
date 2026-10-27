// chrome.runtime in a web page that an extension's externally_connectable matches, as Chrome gives one: sendMessage()
// and connect() to an extension by its ID, and lastError.
//
// This runs in the page's own world before the page's scripts, as the body of a function given `config`: the IDs of
// the extensions the page may message, and the name of the DOM events that carry its messages. The page's world has no
// bridge to the UI process, so each of those extensions has a relay in an isolated world of its own in the page
// (externally-connectable-relay.js), which hears `${config.eventName}-request` events on the document and answers with
// `${config.eventName}-reply` events. The page could send those events itself, as it could call chrome.runtime: the UI
// process knows which document each relay is in, names the sender itself, and only lets a relay message its extension.
//
// https://developer.chrome.com/docs/extensions/develop/concepts/messaging#external-webpage

"use strict";

const NO_RECEIVER = "Could not establish connection. Receiving end does not exist.";

const pendingCalls = new Map();
let nextCallId = 1;
const ports = new Map();

function post(extensionId, message) {
    const detail = JSON.stringify({ extensionId, message });
    document.dispatchEvent(new CustomEvent(`${config.eventName}-request`, { detail }));
}

// Messages travel as JSON, as Chrome sends them.
function toJSON(value) {
    const json = JSON.stringify(value);
    return json === undefined ? null : JSON.parse(json);
}

// Port IDs only have to be unique. crypto.randomUUID() is only there in secure contexts, and these pages may not be.
function randomID() {
    return Array.from(crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, "0")).join("");
}

// chrome.runtime.lastError is set only while a callback runs, as in Chrome.
let lastError;
let lastErrorChecked = false;

function withLastError(message, callback) {
    lastError = { message };
    lastErrorChecked = false;
    try {
        callback();
    } finally {
        if (!lastErrorChecked) console.error(`Unchecked runtime.lastError: ${message}`);
        lastError = undefined;
    }
}

function reportListenerError(error) {
    queueMicrotask(() => {
        throw error;
    });
}

class ExtensionEvent {
    #listeners = [];

    addListener(listener) {
        if (typeof listener !== "function") throw new TypeError("The listener must be a function.");
        if (!this.hasListener(listener)) this.#listeners.push(listener);
    }

    removeListener(listener) {
        this.#listeners = this.#listeners.filter(existing => existing !== listener);
    }

    hasListener(listener) {
        return this.#listeners.includes(listener);
    }

    hasListeners() {
        return this.#listeners.length > 0;
    }

    dispatch(...args) {
        for (const listener of this.#listeners) {
            try {
                listener(...args);
            } catch (error) {
                reportListenerError(error);
            }
        }
    }
}

class Port {
    #extensionId;
    #id = randomID();
    #disconnected = false;

    constructor(extensionId, name) {
        this.#extensionId = extensionId;
        this.name = name;
        this.onMessage = new ExtensionEvent();
        this.onDisconnect = new ExtensionEvent();
        ports.set(this.#id, this);
    }

    open() {
        post(this.#extensionId, { type: "connect", port: this.#id, name: this.name });
    }

    postMessage(message) {
        if (this.#disconnected) throw new Error("Attempting to use a disconnected port object");
        post(this.#extensionId, { type: "port-message", port: this.#id, message: toJSON(message) });
    }

    disconnect() {
        if (this.#disconnected) return;
        this.#disconnected = true;
        ports.delete(this.#id);
        post(this.#extensionId, { type: "port-disconnect", port: this.#id });
    }

    // The extension's end went away, or never came.
    closeFromUIProcess(error) {
        if (this.#disconnected) return;
        this.#disconnected = true;
        ports.delete(this.#id);
        if (error) withLastError(error, () => this.onDisconnect.dispatch(this));
        else this.onDisconnect.dispatch(this);
    }
}

function sendMessage(extensionId, ...args) {
    if (typeof extensionId !== "string")
        throw new TypeError(
            "chrome.runtime.sendMessage() called from a webpage must specify an Extension ID (string) for its first argument."
        );
    const callback = typeof args[args.length - 1] === "function" ? args.pop() : undefined;
    const message = args[0];

    const promise = new Promise((resolve, reject) => {
        if (!config.extensionIds.includes(extensionId)) {
            reject(new Error(NO_RECEIVER));
            return;
        }
        const id = nextCallId++;
        pendingCalls.set(id, { resolve, reject });
        post(extensionId, { type: "call", id, name: "runtime.sendMessage", args: [toJSON(message)] });
    });

    if (!callback) return promise.then(reply => reply.response);
    promise.then(
        reply => {
            if (!reply.responded) {
                withLastError("The message port closed before a response was received.", () => callback());
                return;
            }
            try {
                callback(reply.response);
            } catch (error) {
                reportListenerError(error);
            }
        },
        error => withLastError(error.message, () => callback())
    );
    return undefined;
}

function connect(extensionId, connectInfo) {
    if (typeof extensionId !== "string")
        throw new TypeError(
            "chrome.runtime.connect() called from a webpage must specify an Extension ID (string) for its first argument."
        );
    const port = new Port(extensionId, String(connectInfo?.name ?? ""));
    if (config.extensionIds.includes(extensionId)) port.open();
    else queueMicrotask(() => port.closeFromUIProcess(NO_RECEIVER));
    return port;
}

document.addEventListener(`${config.eventName}-reply`, event => {
    if (typeof event.detail !== "string") return;
    const { message } = JSON.parse(event.detail);
    switch (message?.type) {
        case "reply": {
            const pending = pendingCalls.get(message.id);
            if (!pending) return;
            pendingCalls.delete(message.id);
            if ("error" in message) pending.reject(new Error(message.error));
            else pending.resolve(message.result);
            break;
        }
        case "port-message": {
            const port = ports.get(message.port);
            port?.onMessage.dispatch(message.message, port);
            break;
        }
        case "port-disconnect":
            ports.get(message.port)?.closeFromUIProcess(message.error);
            break;
    }
});

const runtime = {
    sendMessage,
    connect,
    get lastError() {
        lastErrorChecked = true;
        return lastError;
    },
};

if (typeof globalThis.chrome !== "object" || globalThis.chrome === null)
    Object.defineProperty(globalThis, "chrome", { value: {}, writable: true, configurable: true, enumerable: true });
globalThis.chrome.runtime = runtime;
