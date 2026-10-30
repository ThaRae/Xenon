importScripts("scene.js");

self.onmessage = async event => {
    const includePixels = !!(event.data && event.data.includePixels);
    try {
        const canvas = new OffscreenCanvas(SCENE_WIDTH, SCENE_HEIGHT);
        const ctx = canvas.getContext("2d");
        drawScene(ctx, SCENE_WIDTH, SCENE_HEIGHT);
        const blob = await canvas.convertToBlob({ type: "image/png" });
        const bytes = await blobBytes(blob);
        const payload = {
            sha: sha256Hex(bytes),
            seed: seedInfo(),
        };
        if (includePixels)
            payload.pngB64 = bytesToBase64(bytes);
        postMessage(payload);
    } catch (error) {
        postMessage({ error: String(error) });
    }
};
