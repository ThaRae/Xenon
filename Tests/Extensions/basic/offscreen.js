// The test extension's offscreen document, which answers tests.js with what it can reach.
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message?.toOffscreen !== "ping") return undefined;
    localStorage.setItem("offscreen", "yes");
    sendResponse({
        offscreen: true,
        hasDOM: typeof document === "object",
        localStorage: localStorage.getItem("offscreen") === "yes",
        onlyRuntime: Object.keys(chrome).join() === "runtime",
    });
    return undefined;
});
