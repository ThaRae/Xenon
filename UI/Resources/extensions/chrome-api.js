// The chrome.* API of Xenon's extension pages.
//
// This runs before an extension page's own scripts, as the body of a function given `bridge`, the page's channel to
// Xenon's UI process (bridge.postMessage(string) and bridge.onmessage), and `config`, what the page needs to answer
// synchronously: the extension's ID, manifest, and messages. Everything else is a call to the UI process, which knows
// which extension and page sent it and checks what that extension may do.
//
// https://developer.chrome.com/docs/extensions/reference/api

"use strict";

const pendingCalls = new Map();
let nextCallId = 1;

function post(message) {
    bridge.postMessage(JSON.stringify(message));
}

class ExtensionError extends Error {}

// Calls into the UI process. The promise rejects with the error the UI process gives.
function call(name, args = []) {
    return new Promise((resolve, reject) => {
        const id = nextCallId++;
        pendingCalls.set(id, { resolve, reject });
        post({ type: "call", id, name, args });
    });
}

// chrome.runtime.lastError is set only while a callback runs, as in Chrome.
let lastError;
let lastErrorChecked = false;

function withLastError(error, callback) {
    lastError = { message: error.message };
    lastErrorChecked = false;
    try {
        callback();
    } finally {
        if (!lastErrorChecked) console.error(`Unchecked runtime.lastError: ${lastError.message}`);
        lastError = undefined;
    }
}

function reportListenerError(error) {
    queueMicrotask(() => {
        throw error;
    });
}

// MV3 APIs return a promise, unless the last argument is a callback, which gets the result instead (and
// chrome.runtime.lastError on failure).
function promiseOrCallback(args, operation) {
    const callback = typeof args[args.length - 1] === "function" ? args.pop() : undefined;
    const promise = Promise.resolve().then(() => operation(...args));
    if (!callback) return promise;

    promise.then(
        result => {
            try {
                callback(result);
            } catch (error) {
                reportListenerError(error);
            }
        },
        error => withLastError(error, () => callback())
    );
    return undefined;
}

function api(operation) {
    return (...args) => promiseOrCallback(args, operation);
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

    // Returns what each listener returned.
    dispatch(...args) {
        return this.#listeners.map(listener => {
            try {
                return listener(...args);
            } catch (error) {
                reportListenerError(error);
                return undefined;
            }
        });
    }
}

// Extension messages travel as JSON, as Chrome sends them.
function toJSON(value) {
    const json = JSON.stringify(value);
    return json === undefined ? null : JSON.parse(json);
}

// --- chrome.runtime ----------------------------------------------------------------------------------------------------

const runtimeOnMessage = new ExtensionEvent();

const runtime = {
    id: config.id,

    get lastError() {
        lastErrorChecked = true;
        return lastError;
    },

    getURL(path) {
        return `chrome-extension://${config.id}/${String(path).replace(/^\/+/, "")}`;
    },

    getManifest() {
        return structuredClone(config.manifest);
    },

    getPlatformInfo: api(() => ({ ...config.platform })),

    sendMessage(...args) {
        const callback = typeof args[args.length - 1] === "function" ? args.pop() : undefined;

        // sendMessage(extensionId?, message, options?)
        let extensionId;
        if (args.length >= 3 || (args.length === 2 && (typeof args[0] === "string" || args[0] == null)))
            extensionId = args.shift();
        const message = args[0];

        const promise = Promise.resolve().then(() => {
            if (extensionId != null && extensionId !== config.id)
                throw new ExtensionError("Messaging other extensions isn't supported yet.");
            return call("runtime.sendMessage", [toJSON(message)]);
        });

        if (!callback) return promise.then(reply => reply.response);
        promise.then(
            reply => {
                if (!reply.responded) {
                    withLastError(new ExtensionError("The message port closed before a response was received."), () =>
                        callback()
                    );
                    return;
                }
                try {
                    callback(reply.response);
                } catch (error) {
                    reportListenerError(error);
                }
            },
            error => withLastError(error, () => callback())
        );
        return undefined;
    },

    onMessage: runtimeOnMessage,
    onMessageExternal: new ExtensionEvent(),
    onConnect: new ExtensionEvent(),
    onConnectExternal: new ExtensionEvent(),
    onInstalled: new ExtensionEvent(),
    onStartup: new ExtensionEvent(),
    onSuspend: new ExtensionEvent(),
    onUpdateAvailable: new ExtensionEvent(),

    reload() {
        post({ type: "call", id: nextCallId++, name: "runtime.reload", args: [] });
    },

    openOptionsPage: api(() => call("runtime.openOptionsPage")),
    setUninstallURL: api(() => undefined),
};

// --- chrome.storage ----------------------------------------------------------------------------------------------------

const storageOnChanged = new ExtensionEvent();
const storageAreas = {};

class StorageArea {
    #name;

    constructor(name, quotas) {
        this.#name = name;
        Object.assign(this, quotas);
        this.onChanged = new ExtensionEvent();

        this.get = api(keys => {
            let names = null;
            let defaults = {};
            if (typeof keys === "string") names = [keys];
            else if (Array.isArray(keys)) names = keys.map(String);
            else if (keys && typeof keys === "object") {
                names = Object.keys(keys);
                defaults = keys;
            } else if (keys != null) throw new TypeError("storage.get() takes a key, a list of keys, or an object.");
            return call("storage.get", [this.#name, names]).then(items => ({ ...toJSON(defaults), ...items }));
        });

        this.getKeys = api(() => call("storage.getKeys", [this.#name]));

        this.set = api(items => {
            if (!items || typeof items !== "object" || Array.isArray(items))
                throw new TypeError("storage.set() takes an object of items.");
            return call("storage.set", [this.#name, toJSON(items)]).then(() => undefined);
        });

        this.remove = api(keys => {
            const names = Array.isArray(keys) ? keys.map(String) : [String(keys)];
            return call("storage.remove", [this.#name, names]).then(() => undefined);
        });

        this.clear = api(() => call("storage.clear", [this.#name]).then(() => undefined));

        this.getBytesInUse = api(keys => {
            const names = keys == null ? null : Array.isArray(keys) ? keys.map(String) : [String(keys)];
            return call("storage.getBytesInUse", [this.#name, names]);
        });

        if (name === "session") this.setAccessLevel = api(options => call("storage.setAccessLevel", [toJSON(options)]));
    }
}

const storage = {
    local: new StorageArea("local", { QUOTA_BYTES: 10485760 }),
    session: new StorageArea("session", { QUOTA_BYTES: 10485760 }),
    sync: new StorageArea("sync", {
        QUOTA_BYTES: 102400,
        QUOTA_BYTES_PER_ITEM: 8192,
        MAX_ITEMS: 512,
        MAX_WRITE_OPERATIONS_PER_HOUR: 1800,
        MAX_WRITE_OPERATIONS_PER_MINUTE: 120,
    }),
    managed: new StorageArea("managed", {}),
    onChanged: storageOnChanged,
};
for (const name of ["local", "session", "sync", "managed"]) storageAreas[name] = storage[name];

// --- chrome.i18n -------------------------------------------------------------------------------------------------------

function substitute(message, substitutions) {
    return message.replace(/\$(\$|[1-9])/g, (_, which) => {
        if (which === "$") return "$";
        return substitutions[Number(which) - 1] ?? "";
    });
}

const i18n = {
    getMessage(name, substitutions) {
        const message = config.messages[String(name).toLowerCase()];
        if (message === undefined) return "";
        if (substitutions === undefined) substitutions = [];
        else if (!Array.isArray(substitutions)) substitutions = [substitutions];
        return substitute(message, substitutions.slice(0, 9).map(String));
    },

    getUILanguage() {
        return config.uiLanguage;
    },

    getAcceptLanguages: api(() => [...config.acceptLanguages]),

    detectLanguage: api(() => ({ isReliable: false, languages: [] })),
};

// --- Messages from the UI process --------------------------------------------------------------------------------------

function deliverMessage({ id, message, sender }) {
    let responded = false;
    let keepsChannelOpen = false;
    const sendResponse = response => {
        if (responded) return;
        responded = true;
        post({ type: "response", id, handled: true, response: toJSON(response) });
    };

    for (const result of runtimeOnMessage.dispatch(message, sender, sendResponse)) {
        if (result === true) keepsChannelOpen = true;
        // A listener may also answer with a promise, as in Firefox and with Chrome's polyfill.
        else if (result && typeof result.then === "function") {
            keepsChannelOpen = true;
            result.then(sendResponse, error => sendResponse({ error: String(error) }));
        }
    }

    if (!responded && !keepsChannelOpen) post({ type: "response", id, handled: false });
}

bridge.onmessage = text => {
    const message = JSON.parse(text);
    switch (message.type) {
        case "reply": {
            const pending = pendingCalls.get(message.id);
            if (!pending) return;
            pendingCalls.delete(message.id);
            if ("error" in message) pending.reject(new ExtensionError(message.error));
            else pending.resolve(message.result);
            break;
        }
        case "message":
            deliverMessage(message);
            break;
        case "event":
            if (message.name === "storage.onChanged") {
                const [changes, areaName] = message.args;
                storageAreas[areaName]?.onChanged.dispatch(changes);
                storageOnChanged.dispatch(changes, areaName);
            }
            break;
    }
};

// --- The popup's size --------------------------------------------------------------------------------------------------

// Chrome sizes an action popup to its page. The page's body sets the width when it has one narrower than the popup, and
// anything wider than the popup widens it; its content sets the height.
if (config.isPopup) {
    const reportSize = () => {
        const body = document.body;
        if (!body) return;
        const style = getComputedStyle(body);
        const rect = body.getBoundingClientRect();
        const bodyWidth = rect.width + parseFloat(style.marginLeft) + parseFloat(style.marginRight);
        const width = bodyWidth < innerWidth ? bodyWidth : document.documentElement.scrollWidth;
        const height = Math.max(rect.bottom + parseFloat(style.marginBottom), body.scrollHeight);
        post({ type: "popup-size", width: Math.ceil(width), height: Math.ceil(height) });
    };
    document.addEventListener("DOMContentLoaded", () => {
        reportSize();
        new ResizeObserver(reportSize).observe(document.body);
    });
}

// --- chrome ------------------------------------------------------------------------------------------------------------

const chrome = { runtime, i18n };
if (config.permissions.includes("storage")) chrome.storage = storage;

Object.defineProperty(globalThis, "chrome", { value: chrome, writable: true, configurable: true, enumerable: false });
