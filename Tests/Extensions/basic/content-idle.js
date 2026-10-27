// A content script at document_idle, the default, in the same world as content-start.js. It checks what needs the UI
// process, then leaves every content script's results on the page for its own script to report.
// NB: Content scripts share their world's global scope, so each keeps its own names in a function.
(() => {
    const test = globalThis.xenonContentTest;
    test.order.push("idle");

    function check(name, condition) {
        test.results[name] = Boolean(condition);
    }

    async function expectRejection(name, promise, text) {
        try {
            await promise;
            check(name, false);
        } catch (error) {
            check(name, String(error.message).includes(text));
        }
    }

    async function run() {
        check("document_idle comes after document_end", test.order.join() === "start,end,idle");

        const sender = await chrome.runtime.sendMessage({ toBackground: "sender" });
        check(
            "runtime.sendMessage reaches the background, which knows the sender",
            sender?.url === location.href && sender.origin === location.origin && sender.id === chrome.runtime.id
        );

        const port = chrome.runtime.connect({ name: "content" });
        const echo = await new Promise(resolve => {
            port.onMessage.addListener(resolve);
            port.postMessage("hello");
        });
        check("runtime.connect reaches the background", echo?.echo === "hello" && echo.name === "content");
        port.disconnect();

        await chrome.storage.local.set({ fromContentScript: 1 });
        check("storage.local", (await chrome.storage.local.get("fromContentScript")).fromContentScript === 1);
        await expectRejection("storage.session is for trusted contexts", chrome.storage.session.get(null), "not allowed");

        // The background checks chrome.scripting against this page's tab.
        Object.assign(test.results, await chrome.runtime.sendMessage({ toBackground: "scripting" }));
    }

    run()
        .catch(error => check(`no exception: ${error}`, false))
        .finally(() => {
            document.documentElement.dataset.contentResults = JSON.stringify(test.results);
        });
})();
