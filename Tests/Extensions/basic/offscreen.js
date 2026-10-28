// The test extension's offscreen document, which answers tests.js with what it can reach.
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    switch (message?.toOffscreen) {
        case "ping":
            localStorage.setItem("offscreen", "yes");
            sendResponse({
                offscreen: true,
                hasDOM: typeof document === "object",
                localStorage: localStorage.getItem("offscreen") === "yes",
                onlyRuntime: Object.keys(chrome).join() === "runtime",
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
