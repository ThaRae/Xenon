function seedInfo() {
    try {
        if (typeof internals === "undefined")
            return "no-internals";
        return internals.farblingSeedInfo();
    } catch (error) {
        return "error:" + String(error);
    }
}

function sampleNavigator() {
    const exposed = "deviceMemory" in navigator;
    return {
        seed: seedInfo(),
        hardwareConcurrency: navigator.hardwareConcurrency,
        deviceMemoryExposed: exposed,
        deviceMemory: exposed ? navigator.deviceMemory : null,
        origin: location.origin,
        host: location.host,
        href: location.href,
    };
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
                resolve({ error: event.message || "shared-worker-error", unsupported: true });
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
