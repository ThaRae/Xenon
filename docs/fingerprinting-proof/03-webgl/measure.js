const FARBLED_PNAMES = [
    { name: "MAX_VERTEX_UNIFORM_COMPONENTS", pname: 0x8b4a },
    { name: "MAX_VERTEX_UNIFORM_BLOCKS", pname: 0x8a2b },
    { name: "MAX_VERTEX_OUTPUT_COMPONENTS", pname: 0x9122 },
    { name: "MAX_VARYING_COMPONENTS", pname: 0x8b4b },
    { name: "MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS", pname: 0x8c8a },
    { name: "MAX_FRAGMENT_UNIFORM_COMPONENTS", pname: 0x8b49 },
    { name: "MAX_FRAGMENT_UNIFORM_BLOCKS", pname: 0x8a2d },
    { name: "MAX_FRAGMENT_INPUT_COMPONENTS", pname: 0x9125 },
    { name: "MAX_UNIFORM_BUFFER_BINDINGS", pname: 0x8a2f },
    { name: "MAX_COMBINED_UNIFORM_BLOCKS", pname: 0x8a2e },
    { name: "MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS", pname: 0x8a31 },
    { name: "MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS", pname: 0x8a33 },
];

function fnv1a64(text) {
    const bytes = typeof text === "string" ? new TextEncoder().encode(text) : text;
    let hash = 14695981039346656037n;
    for (let i = 0; i < bytes.length; i++) {
        hash ^= BigInt(bytes[i]);
        hash = (hash * 1099511628211n) & 0xffffffffffffffffn;
    }
    return hash.toString(16).padStart(16, "0");
}

function seedInfo() {
    try {
        if (typeof internals === "undefined")
            return "no-internals";
        return internals.farblingSeedInfo();
    } catch (error) {
        return "error:" + String(error);
    }
}

function measureGL(gl, kind) {
    if (!gl)
        return { kind, ok: false, error: "no-context" };
    const dbg = gl.getExtension("WEBGL_debug_renderer_info");
    const vendor = dbg ? gl.getParameter(dbg.UNMASKED_VENDOR_WEBGL) : null;
    const renderer = dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : null;
    const maxes = {};
    for (const item of FARBLED_PNAMES)
        maxes[item.name] = gl.getParameter(item.pname);
    const extensions = gl.getSupportedExtensions() || [];
    let extensionOk = true;
    for (let i = 0; i < extensions.length; i++) {
        if (gl.getExtension(extensions[i]) == null)
            extensionOk = false;
    }
    let allocOk = null;
    let allocError = null;
    if (kind === "webgl2" && typeof maxes.MAX_UNIFORM_BUFFER_BINDINGS === "number" && maxes.MAX_UNIFORM_BUFFER_BINDINGS > 0) {
        const index = maxes.MAX_UNIFORM_BUFFER_BINDINGS - 1;
        const buf = gl.createBuffer();
        gl.bindBuffer(gl.UNIFORM_BUFFER, buf);
        gl.bufferData(gl.UNIFORM_BUFFER, 16, gl.STATIC_DRAW);
        gl.bindBufferBase(gl.UNIFORM_BUFFER, index, buf);
        allocOk = gl.getError() === gl.NO_ERROR;
        allocError = allocOk ? null : "bindBufferBase failed at reported limit";
    }
    gl.clearColor(0.25, 0.5, 0.75, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    const pixels = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    const payload = {
        kind,
        ok: true,
        vendor,
        renderer,
        maskedVendor: gl.getParameter(gl.VENDOR),
        maskedRenderer: gl.getParameter(gl.RENDERER),
        maxes,
        extensions,
        extensionOk,
        allocOk,
        allocError,
        readPixels: Array.from(pixels),
    };
    payload.hash = fnv1a64(JSON.stringify(payload));
    return payload;
}

function measureDocumentCanvases() {
    const webglCanvas = document.createElement("canvas");
    webglCanvas.width = 8;
    webglCanvas.height = 8;
    const webgl2Canvas = document.createElement("canvas");
    webgl2Canvas.width = 8;
    webgl2Canvas.height = 8;
    return {
        seed: seedInfo(),
        origin: typeof location !== "undefined" ? location.origin : "worker",
        webgl: measureGL(webglCanvas.getContext("webgl"), "webgl"),
        webgl2: measureGL(webgl2Canvas.getContext("webgl2"), "webgl2"),
    };
}
