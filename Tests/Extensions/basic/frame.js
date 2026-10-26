chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message?.ping !== undefined) sendResponse({ pong: message.ping, senderUrl: sender.url, senderId: sender.id });
    if (message?.later !== undefined) {
        setTimeout(() => sendResponse({ later: message.later }), 10);
        return true;
    }
    return undefined;
});
parent.postMessage("frame-ready", location.origin);
