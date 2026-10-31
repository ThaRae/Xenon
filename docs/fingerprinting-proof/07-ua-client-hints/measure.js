function seedInfo() {
    try {
        if (typeof internals === "undefined")
            return "no-internals";
        return internals.farblingSeedInfo();
    } catch (error) {
        return "error:" + String(error);
    }
}

function navigatorSample() {
    return {
        seed: seedInfo(),
        userAgent: navigator.userAgent,
        appVersion: navigator.appVersion,
        platform: navigator.platform,
        vendor: navigator.vendor,
        product: navigator.product,
        productSub: navigator.productSub,
        oscpu: "oscpu" in navigator ? navigator.oscpu : null,
        userAgentData: "userAgentData" in navigator,
        origin: location.origin,
        host: location.host,
        href: location.href,
    };
}

function echoHeaders() {
    return fetch("/echo-headers").then(r => r.json());
}

function echoXHR() {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", "/echo-headers");
        xhr.onload = () => resolve(JSON.parse(xhr.responseText));
        xhr.onerror = () => reject(new Error("xhr"));
        xhr.send();
    });
}

function withTimeout(promise, ms) {
    return Promise.race([
        promise,
        new Promise((_, reject) => setTimeout(() => reject(new Error("timeout")), ms)),
    ]);
}

function withWorker() {
    return new Promise(resolve => {
        try {
            const worker = new Worker("worker.js");
            const timer = setTimeout(() => resolve({ error: "timeout", unsupported: true }), 8000);
            worker.onmessage = event => {
                clearTimeout(timer);
                resolve(event.data);
            };
            worker.onerror = event => {
                clearTimeout(timer);
                resolve({ error: event.message || "worker-error", unsupported: true });
            };
            worker.postMessage("sample");
        } catch (error) {
            resolve({ error: String(error), unsupported: true });
        }
    });
}

function withSharedWorker() {
    return new Promise(resolve => {
        try {
            if (typeof SharedWorker === "undefined") {
                resolve({ error: "SharedWorker missing", unsupported: true });
                return;
            }
            const worker = new SharedWorker("shared-worker.js");
            const timer = setTimeout(() => resolve({ error: "timeout", unsupported: true }), 8000);
            worker.onerror = event => {
                clearTimeout(timer);
                resolve({ error: event.message || "shared-error", unsupported: true });
            };
            worker.port.onmessage = event => {
                clearTimeout(timer);
                resolve(event.data);
            };
            worker.port.start();
            worker.port.postMessage("sample");
        } catch (error) {
            resolve({ error: String(error), unsupported: true });
        }
    });
}

function websocketUa() {
    return new Promise(resolve => {
        try {
            const ws = new WebSocket("ws://" + location.host + "/echo-headers");
            const timer = setTimeout(() => {
                try { ws.close(); } catch {}
                resolve({ error: "timeout" });
            }, 3000);
            ws.onopen = () => {
                clearTimeout(timer);
                ws.close();
                resolve({ opened: true });
            };
            ws.onerror = () => {
                clearTimeout(timer);
                resolve({ error: "ws-error" });
            };
        } catch (error) {
            resolve({ error: String(error) });
        }
    });
}
