self.onconnect = event => {
    const port = event.ports[0];
    port.onmessage = async () => {
        try {
            port.postMessage({
                ok: true,
                navigator: {
                    userAgent: navigator.userAgent,
                    appVersion: navigator.appVersion,
                    platform: navigator.platform,
                },
            });
        } catch (error) {
            port.postMessage({ error: String(error), unsupported: true });
        }
    };
};
