importScripts("measure.js");

self.onmessage = () => {
    try {
        const webglCanvas = new OffscreenCanvas(SCENE_SIZE, SCENE_SIZE);
        const webgl2Canvas = new OffscreenCanvas(SCENE_SIZE, SCENE_SIZE);
        postMessage({
            seed: seedInfo(),
            webgl: measureGL(webglCanvas.getContext("webgl"), "webgl"),
            webgl2: measureGL(webgl2Canvas.getContext("webgl2"), "webgl2"),
        });
    } catch (error) {
        postMessage({ error: String(error) });
    }
};
