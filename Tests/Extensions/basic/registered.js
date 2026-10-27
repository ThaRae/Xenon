// A content script that background.js registers with scripting.registerContentScripts(), for the frame of Meta/xenon.py's
// content script page.
chrome.runtime.sendMessage({ toBackground: "registered-ran", path: location.pathname });
