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

        const early = await test.earlyReply;
        check("a message sent as the browser starts waits for the background", early?.pong === true);

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

        Object.assign(test.results, await chrome.runtime.sendMessage({ toBackground: "scripting" }));

        const accessibleLoaded = new Promise(resolve => {
            addEventListener("message", event => {
                if (event.origin === `chrome-extension://${chrome.runtime.id}` && event.data === "accessible-loaded")
                    resolve(true);
            });
            setTimeout(() => resolve(false), 5000);
        });
        const frame = document.createElement("iframe");
        frame.src = chrome.runtime.getURL("accessible.html");
        document.body.append(frame);
        check("web accessible pages load in frames, whatever the page's CSP", await accessibleLoaded);
    }

    run()
        .catch(error => check(`no exception: ${error}`, false))
        .finally(() => {
            document.documentElement.dataset.contentResults = JSON.stringify(test.results);
        });
})();
