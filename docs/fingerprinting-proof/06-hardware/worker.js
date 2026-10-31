self.onmessage = () => {
    try {
        const exposed = "deviceMemory" in navigator;
        postMessage({
            ok: true,
            hardwareConcurrency: navigator.hardwareConcurrency,
            deviceMemoryExposed: exposed,
            deviceMemory: exposed ? navigator.deviceMemory : null,
        });
    } catch (error) {
        postMessage({ error: String(error), unsupported: true });
    }
};
