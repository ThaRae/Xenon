// A content script at document_start in the page's own world ("world": "MAIN"), with the page's globals and no chrome.*.
window.xenonMainWorld = typeof chrome === "undefined" || !chrome.runtime;
