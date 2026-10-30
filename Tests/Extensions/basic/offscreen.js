function clearClipboard(copied) {
    const text = document.createElement("textarea");
    document.body.append(text);
    text.focus();
    text.value = "";
    document.execCommand("paste");
    const pasted = text.value;
    text.value = " ";
    text.select();
    const cleared = pasted === copied && document.execCommand("copy");
    text.value = "";
    document.execCommand("paste");
    const after = text.value;
    text.remove();
    return cleared && after === " ";
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    switch (message?.toOffscreen) {
        case "ping":
            localStorage.setItem("offscreen", "yes");
            sendResponse({
                offscreen: true,
                hasDOM: typeof document === "object",
                localStorage: localStorage.getItem("offscreen") === "yes",
                onlyRuntime: Object.keys(chrome).join() === "runtime",
                clearedClipboard: clearClipboard(message.copied),
            });
            return undefined;
        case "image": {
            const image = new Image();
            image.onload = () => sendResponse({ loaded: true });
            image.onerror = () => sendResponse({ loaded: false });
            image.src = message.url;
            return true;
        }
        default:
            return undefined;
    }
});
