(() => {
    const test = (globalThis.xenonContentTest = { order: ["start"], results: {} });

    function check(name, condition) {
        test.results[name] = Boolean(condition);
    }

    check("document_start comes before the document has an element", document.documentElement === null);
    check("content scripts get runtime, i18n and storage", ["runtime", "i18n", "storage"].every(name => name in chrome));
    check("content scripts get no other APIs", !["tabs", "windows", "alarms", "permissions", "action"].some(name => name in chrome));
    check(
        "content scripts get part of runtime",
        /^[a-p]{32}$/.test(chrome.runtime.id) &&
            typeof chrome.runtime.sendMessage === "function" &&
            chrome.runtime.getPlatformInfo === undefined &&
            chrome.runtime.getURL("a.js") === `chrome-extension://${chrome.runtime.id}/a.js`
    );
    check("i18n", chrome.i18n.getMessage("name") === "Xenon test extension");

    test.earlyReply = chrome.runtime.sendMessage({ toBackground: "ping" }).catch(error => ({ error: error.message }));

    window.xenonContentSecret = "content";
    sessionStorage.setItem("xenonContentStart", "ran");
})();
