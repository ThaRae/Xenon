importScripts("measure.js");

self.onmessage = async () => {
    try {
        const audio = await measureAudio();
        postMessage({ seed: seedInfo(), audio });
    } catch (error) {
        postMessage({ error: String(error), unsupported: true });
    }
};
