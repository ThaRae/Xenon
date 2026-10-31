self.onmessage = async () => {
    try {
        const nav = {
            userAgent: navigator.userAgent,
            appVersion: navigator.appVersion,
            platform: navigator.platform,
            vendor: "vendor" in navigator ? navigator.vendor : null,
            product: navigator.product,
        };
        const fetchHeaders = await (await fetch("/echo-headers")).json();
        postMessage({ ok: true, navigator: nav, fetch: fetchHeaders });
    } catch (error) {
        postMessage({ error: String(error), unsupported: true });
    }
};
