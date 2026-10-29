// The test extension's offscreen document, which answers tests.js with what it can reach.

// Clears the clipboard as Proton Pass's offscreen document does once its timer runs out, with no user gesture: it pastes
// to see whether the clipboard still holds what it copied, and if so, copies a space over it.
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
            // An image the bundled lists block on web pages.
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
