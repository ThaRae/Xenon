// A content script at document_end, in the same world as content-start.js.
// NB: Content scripts share their world's global scope, so each keeps its own names in a function.
(() => {
    const test = globalThis.xenonContentTest;
    test.order.push("end");

    function check(name, condition) {
        test.results[name] = Boolean(condition);
    }

    const marker = document.getElementById("marker");
    check("content scripts share their extension's world", test.order[0] === "start");
    check("document_end comes once the document is parsed", document.readyState !== "loading" && marker !== null);
    check("content scripts share the page's DOM", marker?.textContent === "marker");
    check(
        "the page's globals stay out of the content scripts' world",
        window.xenonPageSecret === undefined && window.xenonMainWorld === undefined
    );
    check("the page's expandos stay out of the content scripts' world", marker.pageExpando === undefined);
    marker.contentExpando = "content";

    // The page posts a message once DOMContentLoaded has fired, which comes after this.
    window.addEventListener("message", event => {
        if (event.source !== window || event.data?.toContentScript !== "ping") return;
        document.documentElement.dataset.contentPostMessage = event.isTrusted ? "trusted" : "untrusted";
    });

    // Neither of these may reach the page's error handlers.
    setTimeout(() => {
        throw new Error("A content script's error");
    });
    Promise.reject(new Error("A content script's rejection"));
})();
