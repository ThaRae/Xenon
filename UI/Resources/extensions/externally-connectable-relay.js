// Carries one extension's messages and ports between a web page that its externally_connectable matches and the UI
// process (see externally-connectable.js, the page's chrome.runtime).
//
// This runs in an isolated world of its own in the page, given `bridge`, its channel to the UI process, and `config`:
// the extension's ID and the name of the DOM events to hear and answer. Messages pass through as JSON text. The UI
// process checks what each one asks for, and knows from the bridge which document sent it.

document.addEventListener(`${config.eventName}-request`, event => {
    // NB: The page made the event, so only a string is taken from it.
    if (typeof event.detail !== "string") return;
    let request;
    try {
        request = JSON.parse(event.detail);
    } catch {
        return;
    }
    if (request?.extensionId !== config.extensionId) return;
    bridge.postMessage(JSON.stringify(request.message ?? null));
});

bridge.onmessage = text => {
    const detail = JSON.stringify({ extensionId: config.extensionId, message: JSON.parse(text) });
    document.dispatchEvent(new CustomEvent(`${config.eventName}-reply`, { detail }));
};

// A page that goes away says so, so that its ports close.
addEventListener("pagehide", () => bridge.postMessage(JSON.stringify({ type: "closing" })));
