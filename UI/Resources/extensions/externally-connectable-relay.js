document.addEventListener(`${config.eventName}-request`, event => {
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

addEventListener("pagehide", () => bridge.postMessage(JSON.stringify({ type: "closing" })));
