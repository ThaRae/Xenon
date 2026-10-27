// The chrome.* API of Xenon's extension pages, background workers, and content scripts.
//
// This runs before an extension's own scripts, given `bridge`, the context's channel to Xenon's UI process
// (bridge.postMessage(string) and bridge.onmessage), and `config`, what it needs to answer synchronously: the
// extension's ID, manifest, and messages, and what kind of context this is. Everything else is a call to the UI
// process, which knows which extension and context sent it and checks what that extension may do. Content scripts run
// it in their extension's isolated world in a page, which the page's own scripts can't reach.
//
// https://developer.chrome.com/docs/extensions/reference/api

"use strict";

const pendingCalls = new Map();
let nextCallId = 1;

// Events the UI process sends, by name ("alarms.onAlarm").
const events = new Map();

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

// Extension messages and API arguments travel as JSON, as Chrome sends messages.
function toJSON(value) {
    const json = JSON.stringify(value);
    return json === undefined ? null : JSON.parse(json);
}

// A method the UI process answers, with the arguments it was given.
function method(name, transform = result => result) {
    return api((...args) => call(name, args.map(toJSON)).then(transform));
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

function event(name) {
    const extensionEvent = new ExtensionEvent();
    events.set(name, extensionEvent);
    return extensionEvent;
}

const hasPermission = permission => config.permissions.includes(permission);

// --- chrome.runtime ----------------------------------------------------------------------------------------------------

// A long-lived connection between two contexts of the extension (runtime.connect and runtime.onConnect).
const ports = new Map();

class Port {
    #id;
    #disconnected = false;

    constructor(id, name, sender) {
        this.#id = id;
        this.name = name;
        if (sender) this.sender = sender;
        this.onMessage = new ExtensionEvent();
        this.onDisconnect = new ExtensionEvent();
        ports.set(id, this);
    }

    postMessage(message) {
        if (this.#disconnected) throw new ExtensionError("Attempting to use a disconnected port object");
        post({ type: "port-message", port: this.#id, message: toJSON(message) });
    }

    disconnect() {
        if (this.#disconnected) return;
        this.#disconnected = true;
        ports.delete(this.#id);
        post({ type: "port-disconnect", port: this.#id });
    }

    // The other end went away, or never came.
    closeFromUIProcess(error) {
        if (this.#disconnected) return;
        this.#disconnected = true;
        ports.delete(this.#id);
        if (error) withLastError(new ExtensionError(error), () => this.onDisconnect.dispatch(this));
        else this.onDisconnect.dispatch(this);
    }
}

// What runtime.sendMessage() and tabs.sendMessage() give back: the first response, to a callback if there is one.
function messageReply(promise, callback) {
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
}

const runtimeOnMessage = event("runtime.onMessage");
const runtimeOnConnect = event("runtime.onConnect");

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
        return messageReply(promise, callback);
    },

    connect(...args) {
        // connect(extensionId?, connectInfo?)
        const extensionId = typeof args[0] === "string" ? args.shift() : undefined;
        const name = args[0]?.name ?? "";
        const id = crypto.randomUUID();
        const port = new Port(id, String(name));
        if (extensionId !== undefined && extensionId !== config.id)
            queueMicrotask(() => port.closeFromUIProcess("Messaging other extensions isn't supported yet."));
        else post({ type: "connect", port: id, name: String(name) });
        return port;
    },

    onMessage: runtimeOnMessage,
    onConnect: runtimeOnConnect,
    onMessageExternal: event("runtime.onMessageExternal"),
    onConnectExternal: event("runtime.onConnectExternal"),
    onInstalled: event("runtime.onInstalled"),
    onStartup: event("runtime.onStartup"),
    onSuspend: event("runtime.onSuspend"),
    onUpdateAvailable: event("runtime.onUpdateAvailable"),

    getContexts: method("runtime.getContexts"),
    openOptionsPage: method("runtime.openOptionsPage"),
    setUninstallURL: method("runtime.setUninstallURL"),
    requestUpdateCheck: api(() => ({ status: "no_update" })),

    reload() {
        post({ type: "call", id: nextCallId++, name: "runtime.reload", args: [] });
    },

    ContextType: {
        TAB: "TAB",
        POPUP: "POPUP",
        BACKGROUND: "BACKGROUND",
        OFFSCREEN_DOCUMENT: "OFFSCREEN_DOCUMENT",
        SIDE_PANEL: "SIDE_PANEL",
        DEVELOPER_TOOLS: "DEVELOPER_TOOLS",
    },
    OnInstalledReason: {
        INSTALL: "install",
        UPDATE: "update",
        CHROME_UPDATE: "chrome_update",
        SHARED_MODULE_UPDATE: "shared_module_update",
    },
};

// --- chrome.storage ----------------------------------------------------------------------------------------------------

const storageOnChanged = event("storage.onChanged");
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

// --- chrome.alarms -----------------------------------------------------------------------------------------------------

const alarms = {
    create: api((...args) => {
        // create(name?, alarmInfo)
        const name = typeof args[0] === "string" ? args.shift() : "";
        return call("alarms.create", [name, toJSON(args[0] ?? {})]).then(() => undefined);
    }),
    get: api(name => call("alarms.get", [name ?? ""])),
    getAll: method("alarms.getAll"),
    clear: api(name => call("alarms.clear", [name ?? ""])),
    clearAll: method("alarms.clearAll"),
    onAlarm: event("alarms.onAlarm"),
};

// --- chrome.action -----------------------------------------------------------------------------------------------------

const action = {
    setBadgeText: method("action.setBadgeText"),
    getBadgeText: method("action.getBadgeText"),
    setBadgeBackgroundColor: method("action.setBadgeBackgroundColor"),
    getBadgeBackgroundColor: method("action.getBadgeBackgroundColor"),
    setBadgeTextColor: method("action.setBadgeTextColor"),
    getBadgeTextColor: method("action.getBadgeTextColor"),
    setTitle: method("action.setTitle"),
    getTitle: method("action.getTitle"),
    setIcon: api(details => {
        // FIXME: Icons drawn as ImageData, which can't travel as JSON.
        if (details?.imageData && !details.path)
            throw new ExtensionError("action.setIcon() takes only a path for now.");
        return call("action.setIcon", [toJSON({ ...details, imageData: undefined })]);
    }),
    setPopup: method("action.setPopup"),
    getPopup: method("action.getPopup"),
    enable: method("action.enable"),
    disable: method("action.disable"),
    isEnabled: method("action.isEnabled"),
    openPopup: method("action.openPopup"),
    getUserSettings: api(() => ({ isOnToolbar: true })),
    onClicked: event("action.onClicked"),
};

// --- chrome.tabs and chrome.windows ------------------------------------------------------------------------------------

const tabs = {
    query: method("tabs.query"),
    get: method("tabs.get"),
    getCurrent: method("tabs.getCurrent", tab => tab ?? undefined),
    create: method("tabs.create"),
    update: api((...args) => {
        // update(tabId?, updateProperties)
        const tabId = typeof args[0] === "number" ? args.shift() : null;
        return call("tabs.update", [tabId, toJSON(args[0] ?? {})]);
    }),
    remove: api(tabIds => call("tabs.remove", [Array.isArray(tabIds) ? tabIds : [tabIds]]).then(() => undefined)),
    reload: api((...args) => {
        const tabId = typeof args[0] === "number" ? args.shift() : null;
        return call("tabs.reload", [tabId]).then(() => undefined);
    }),
    duplicate: method("tabs.duplicate"),
    // Messages and ports to a tab go to the extension's content scripts in it.
    sendMessage(tabId, message, ...args) {
        const callback = typeof args[args.length - 1] === "function" ? args.pop() : undefined;
        const options = args[0] ?? {};
        const promise = Promise.resolve().then(() =>
            call("tabs.sendMessage", [tabId, toJSON(message), toJSON(options)])
        );
        return messageReply(promise, callback);
    },
    connect(tabId, connectInfo) {
        const id = crypto.randomUUID();
        const name = String(connectInfo?.name ?? "");
        const port = new Port(id, name);
        post({
            type: "connect",
            port: id,
            name,
            tabId,
            frameId: connectInfo?.frameId,
            documentId: connectInfo?.documentId,
        });
        return port;
    },
    captureVisibleTab: method("tabs.captureVisibleTab"),
    onActivated: event("tabs.onActivated"),
    onAttached: event("tabs.onAttached"),
    onCreated: event("tabs.onCreated"),
    onDetached: event("tabs.onDetached"),
    onHighlighted: event("tabs.onHighlighted"),
    onMoved: event("tabs.onMoved"),
    onRemoved: event("tabs.onRemoved"),
    onReplaced: event("tabs.onReplaced"),
    onUpdated: event("tabs.onUpdated"),
    TAB_ID_NONE: -1,
};

const windows = {
    get: method("windows.get"),
    getAll: method("windows.getAll"),
    getCurrent: method("windows.getCurrent"),
    getLastFocused: method("windows.getLastFocused"),
    create: method("windows.create"),
    update: method("windows.update"),
    remove: api(windowId => call("windows.remove", [windowId]).then(() => undefined)),
    onCreated: event("windows.onCreated"),
    onRemoved: event("windows.onRemoved"),
    onFocusChanged: event("windows.onFocusChanged"),
    onBoundsChanged: event("windows.onBoundsChanged"),
    WINDOW_ID_NONE: -1,
    WINDOW_ID_CURRENT: -2,
};

// --- chrome.webNavigation and chrome.scripting -------------------------------------------------------------------------

// FIXME: Frames inside pages, and the navigation events, come with content scripts (milestone 3). For now the events
//        never fire, and a tab has its top-level frame alone.
const webNavigation = {
    getFrame: method("webNavigation.getFrame"),
    getAllFrames: method("webNavigation.getAllFrames"),
    onBeforeNavigate: event("webNavigation.onBeforeNavigate"),
    onCommitted: event("webNavigation.onCommitted"),
    onDOMContentLoaded: event("webNavigation.onDOMContentLoaded"),
    onCompleted: event("webNavigation.onCompleted"),
    onErrorOccurred: event("webNavigation.onErrorOccurred"),
    onCreatedNavigationTarget: event("webNavigation.onCreatedNavigationTarget"),
    onReferenceFragmentUpdated: event("webNavigation.onReferenceFragmentUpdated"),
    onTabReplaced: event("webNavigation.onTabReplaced"),
    onHistoryStateUpdated: event("webNavigation.onHistoryStateUpdated"),
};

// FIXME: Injecting into pages comes with content scripts (milestone 3). Until then, injecting fails, and registered
//        content scripts are accepted but never run.
const notSupportedYet = name =>
    api(() => {
        throw new ExtensionError(`${name}() isn't supported yet.`);
    });
const scripting = {
    executeScript: notSupportedYet("scripting.executeScript"),
    insertCSS: notSupportedYet("scripting.insertCSS"),
    removeCSS: notSupportedYet("scripting.removeCSS"),
    registerContentScripts: api(() => undefined),
    unregisterContentScripts: api(() => undefined),
    updateContentScripts: api(() => undefined),
    getRegisteredContentScripts: api(() => []),
    ExecutionWorld: { ISOLATED: "ISOLATED", MAIN: "MAIN" },
};

// --- chrome.webRequest and chrome.notifications ------------------------------------------------------------------------

// FIXME: Seeing requests needs the engine to ask the UI process about them (milestone 4). Until then, the events never
//        fire, and requests go ahead as if no extension were listening.
const webRequest = {
    onBeforeRequest: event("webRequest.onBeforeRequest"),
    onBeforeSendHeaders: event("webRequest.onBeforeSendHeaders"),
    onSendHeaders: event("webRequest.onSendHeaders"),
    onHeadersReceived: event("webRequest.onHeadersReceived"),
    onAuthRequired: event("webRequest.onAuthRequired"),
    onResponseStarted: event("webRequest.onResponseStarted"),
    onBeforeRedirect: event("webRequest.onBeforeRedirect"),
    onCompleted: event("webRequest.onCompleted"),
    onErrorOccurred: event("webRequest.onErrorOccurred"),
    handlerBehaviorChanged: api(() => undefined),
    MAX_HANDLER_BEHAVIOR_CHANGED_CALLS_PER_10_MINUTES: 20,
};

// FIXME: Xenon doesn't show extensions' notifications yet (milestone 4). They're accepted, and reported as not
//        permitted, as in Chrome when the user turns an extension's notifications off.
const notifications = {
    create: api((...args) => (typeof args[0] === "string" && args[0] ? args[0] : crypto.randomUUID())),
    update: api(() => false),
    clear: api(() => false),
    getAll: api(() => ({})),
    getPermissionLevel: api(() => "denied"),
    onClicked: event("notifications.onClicked"),
    onButtonClicked: event("notifications.onButtonClicked"),
    onClosed: event("notifications.onClosed"),
    onPermissionLevelChanged: event("notifications.onPermissionLevelChanged"),
    onShowSettings: event("notifications.onShowSettings"),
    TemplateType: { BASIC: "basic", IMAGE: "image", LIST: "list", PROGRESS: "progress" },
    PermissionLevel: { GRANTED: "granted", DENIED: "denied" },
};

// --- The rest ----------------------------------------------------------------------------------------------------------

const permissions = {
    contains: method("permissions.contains"),
    getAll: method("permissions.getAll"),
    request: api(details => {
        // As in Chrome, only in answer to the user's click or key press.
        if (!globalThis.navigator?.userActivation?.isActive)
            throw new ExtensionError("This function must be called during a user gesture");
        return call("permissions.request", [toJSON(details)]);
    }),
    remove: method("permissions.remove"),
    onAdded: event("permissions.onAdded"),
    onRemoved: event("permissions.onRemoved"),
};

const management = {
    getSelf: method("management.getSelf"),
};

const idle = {
    queryState: method("idle.queryState"),
    setDetectionInterval: method("idle.setDetectionInterval"),
    getAutoLockDelay: method("idle.getAutoLockDelay"),
    onStateChanged: event("idle.onStateChanged"),
};

const offscreen = {
    createDocument: method("offscreen.createDocument"),
    closeDocument: method("offscreen.closeDocument"),
    hasDocument: method("offscreen.hasDocument"),
    Reason: Object.fromEntries(
        [
            "TESTING",
            "AUDIO_PLAYBACK",
            "IFRAME_SCRIPTING",
            "DOM_SCRAPING",
            "BLOBS",
            "DOM_PARSER",
            "USER_MEDIA",
            "DISPLAY_MEDIA",
            "WEB_RTC",
            "CLIPBOARD",
            "LOCAL_STORAGE",
            "WORKERS",
            "BATTERY_STATUS",
            "MATCH_MEDIA",
            "GEOLOCATION",
        ].map(reason => [reason, reason])
    ),
};

const commands = {
    getAll: method("commands.getAll"),
    onCommand: event("commands.onCommand"),
};

const extension = {
    inIncognitoContext: false,
    isAllowedIncognitoAccess: api(() => false),
    isAllowedFileSchemeAccess: api(() => false),
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

function acceptConnection({ port: id, name, sender }) {
    if (!runtimeOnConnect.hasListeners()) {
        post({ type: "connected", port: id, accepted: false });
        return;
    }
    const port = new Port(id, name, sender);
    post({ type: "connected", port: id, accepted: true });
    runtimeOnConnect.dispatch(port);
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
        case "connect":
            acceptConnection(message);
            break;
        case "port-message":
            ports.get(message.port)?.onMessage.dispatch(message.message, ports.get(message.port));
            break;
        case "port-disconnect":
            ports.get(message.port)?.closeFromUIProcess(message.error);
            break;
        case "event":
            if (message.name === "permissions.onAdded" || message.name === "permissions.onRemoved") {
                const changed = message.args[0]?.permissions ?? [];
                config.permissions =
                    message.name === "permissions.onAdded"
                        ? [...new Set([...config.permissions, ...changed])]
                        : config.permissions.filter(permission => !changed.includes(permission));
                installPermittedAPIs();
            }
            if (message.name === "storage.onChanged") {
                const [changes, areaName] = message.args;
                storageAreas[areaName]?.onChanged.dispatch(changes);
            }
            events.get(message.name)?.dispatch(...message.args);
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

// A page that goes away says so, so that its ports close and messages stop waiting for it.
if (typeof document !== "undefined") addEventListener("pagehide", () => post({ type: "closing" }));

// The background worker's script runs right after this, and once it has, the UI process sends runtime.onInstalled or
// runtime.onStartup (see /_generated_background_worker.js).
if (config.isBackground) {
    const ready = Symbol.for("xenon.background.ready");
    Object.defineProperty(self, ready, {
        configurable: true,
        value() {
            delete self[ready];
            post({ type: "call", id: nextCallId++, name: "runtime.backgroundReady", args: [] });
        },
    });
}

// --- chrome.privacy ----------------------------------------------------------------------------------------------------

// A browser setting an extension may control (types.ChromeSetting).
function chromeSetting(name) {
    return {
        get: api(details => call("privacy.get", [name, toJSON(details ?? {})])),
        set: api(details => call("privacy.set", [name, toJSON(details)]).then(() => undefined)),
        clear: api(details => call("privacy.clear", [name, toJSON(details ?? {})]).then(() => undefined)),
        onChange: event(`${name}.onChange`),
    };
}

// FIXME: Xenon has no password manager or autofill of its own, so these are off, and an extension that sets one only
//        becomes the one controlling it. The network and websites settings come with the features they control.
const privacy = {
    services: {
        passwordSavingEnabled: chromeSetting("privacy.services.passwordSavingEnabled"),
        autofillEnabled: chromeSetting("privacy.services.autofillEnabled"),
        autofillAddressEnabled: chromeSetting("privacy.services.autofillAddressEnabled"),
        autofillCreditCardEnabled: chromeSetting("privacy.services.autofillCreditCardEnabled"),
    },
};

// --- Native messaging --------------------------------------------------------------------------------------------------

// FIXME: Native messaging hosts (milestone 4). Until then, every host is one that isn't installed.
const nativeHostNotFound = "Specified native messaging host not found.";
function connectNative(application) {
    const port = new Port(crypto.randomUUID(), String(application ?? ""));
    queueMicrotask(() => port.closeFromUIProcess(nativeHostNotFound));
    return port;
}
const sendNativeMessage = api(() => {
    throw new ExtensionError(nativeHostNotFound);
});

// --- chrome ------------------------------------------------------------------------------------------------------------

// Offscreen documents get chrome.runtime alone, and content scripts a few of its members, chrome.i18n, and
// chrome.storage, as in Chrome.
const isContentScript = config.contextType === "CONTENT_SCRIPT";
const contentScriptRuntime = {};
for (const name of ["id", "getURL", "getManifest", "sendMessage", "connect", "onMessage", "onConnect", "lastError"])
    Object.defineProperty(contentScriptRuntime, name, Object.getOwnPropertyDescriptor(runtime, name));

const chrome = { runtime: isContentScript ? contentScriptRuntime : runtime };
if (isContentScript) chrome.i18n = i18n;
else if (config.contextType !== "OFFSCREEN_DOCUMENT") {
    Object.assign(chrome, { i18n, tabs, windows, permissions, management, commands, extension });
    if (config.manifest.action) chrome.action = action;
}

// The APIs that come with permissions. An optional permission's API arrives in every open context when the user grants
// it, and goes when the extension gives it up, as in Chrome.
const permittedAPIs = {
    storage,
    alarms,
    idle,
    offscreen,
    webNavigation,
    scripting,
    webRequest,
    notifications,
    privacy,
};
function installPermittedAPIs() {
    if (config.contextType === "OFFSCREEN_DOCUMENT") return;
    if (isContentScript) {
        if (hasPermission("storage")) chrome.storage = storage;
        else delete chrome.storage;
        return;
    }
    for (const [permission, permittedAPI] of Object.entries(permittedAPIs)) {
        if (hasPermission(permission)) chrome[permission] = permittedAPI;
        else delete chrome[permission];
    }
    if (hasPermission("nativeMessaging")) Object.assign(runtime, { connectNative, sendNativeMessage });
    else {
        delete runtime.connectNative;
        delete runtime.sendNativeMessage;
    }
}
installPermittedAPIs();

Object.defineProperty(globalThis, "chrome", { value: chrome, writable: true, configurable: true, enumerable: false });
