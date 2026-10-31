self.onconnect = event => {
    const port = event.ports[0];
    port.onmessage = () => {
        try {
            const exposed = "deviceMemory" in navigator;
            port.postMessage({
                ok: true,
                hardwareConcurrency: navigator.hardwareConcurrency,
                deviceMemoryExposed: exposed,
                deviceMemory: exposed ? navigator.deviceMemory : null,
            });
        } catch (error) {
            port.postMessage({ error: String(error), unsupported: true });
        }
    };
};
