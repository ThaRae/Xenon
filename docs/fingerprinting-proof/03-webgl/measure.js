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
const SCENE_SIZE = 32;

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

function compileShader(gl, type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    return shader;
}

function linkProgram(gl, isWebgl2) {
    const vsSource = isWebgl2
        ? `#version 300 es
in vec2 a; out vec2 v;
void main() { v = a * 0.5 + 0.5; gl_Position = vec4(a, 0.0, 1.0); }`
        : `attribute vec2 a; varying vec2 v;
void main() { v = a * 0.5 + 0.5; gl_Position = vec4(a, 0.0, 1.0); }`;
    const fsSource = isWebgl2
        ? `#version 300 es
precision mediump float; in vec2 v; out vec4 o;
void main() { o = vec4(v.x, v.y, 0.5, 1.0); }`
        : `precision mediump float; varying vec2 v;
void main() { gl_FragColor = vec4(v.x, v.y, 0.5, 1.0); }`;
    const program = gl.createProgram();
    gl.attachShader(program, compileShader(gl, gl.VERTEX_SHADER, vsSource));
    gl.attachShader(program, compileShader(gl, gl.FRAGMENT_SHADER, fsSource));
    gl.linkProgram(program);
    return program;
}

function drawGradientScene(gl, isWebgl2) {
    const program = linkProgram(gl, isWebgl2);
    gl.useProgram(program);
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, 0, 1, -1, 1, 1, 1, 1, -1]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(program, "a");
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    gl.viewport(0, 0, SCENE_SIZE, SCENE_SIZE);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
    return program;
}

function measureGL(gl, kind) {
    if (!gl)
        return { kind, ok: false, error: "no-context" };
    const isWebgl2 = kind === "webgl2";
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
    if (isWebgl2 && typeof maxes.MAX_UNIFORM_BUFFER_BINDINGS === "number" && maxes.MAX_UNIFORM_BUFFER_BINDINGS > 0) {
        const index = maxes.MAX_UNIFORM_BUFFER_BINDINGS - 1;
        const buf = gl.createBuffer();
        gl.bindBuffer(gl.UNIFORM_BUFFER, buf);
        gl.bufferData(gl.UNIFORM_BUFFER, 16, gl.STATIC_DRAW);
        gl.bindBufferBase(gl.UNIFORM_BUFFER, index, buf);
        allocOk = gl.getError() === gl.NO_ERROR;
        allocError = allocOk ? null : "bindBufferBase failed at reported limit";
    }
    drawGradientScene(gl, isWebgl2);
    const pixels = new Uint8Array(SCENE_SIZE * SCENE_SIZE * 4);
    gl.readPixels(0, 0, SCENE_SIZE, SCENE_SIZE, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    const readPixelsHash = fnv1a64(pixels);
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
        readPixelsHash,
        pixelCount: pixels.length,
    };
    payload.hash = fnv1a64(JSON.stringify({
        vendor, renderer, maxes, extensions, readPixelsHash,
    }));
    return payload;
}

function measureDocumentCanvases() {
    const webglCanvas = document.createElement("canvas");
    webglCanvas.width = SCENE_SIZE;
    webglCanvas.height = SCENE_SIZE;
    const webgl2Canvas = document.createElement("canvas");
    webgl2Canvas.width = SCENE_SIZE;
    webgl2Canvas.height = SCENE_SIZE;
    return {
        seed: seedInfo(),
        origin: typeof location !== "undefined" ? location.origin : "worker",
        webgl: measureGL(webglCanvas.getContext("webgl"), "webgl"),
        webgl2: measureGL(webgl2Canvas.getContext("webgl2"), "webgl2"),
    };
}

function measureStrictWebGL2() {
    const canvas = document.createElement("canvas");
    canvas.width = SCENE_SIZE;
    canvas.height = SCENE_SIZE;
    const gl = canvas.getContext("webgl2");
    if (!gl)
        return { ok: false, error: "no-webgl2" };
    const program = linkProgram(gl, true);
    gl.useProgram(program);
    if (typeof internals === "undefined")
        return { ok: false, error: "no-internals" };
    internals.setFarblingLevel("strict");
    const dbg = gl.getExtension("WEBGL_debug_renderer_info");
    const maxes = {};
    for (const item of FARBLED_PNAMES)
        maxes[item.name] = gl.getParameter(item.pname);
    const buf = new Uint8Array(SCENE_SIZE * SCENE_SIZE * 4);
    buf.fill(0xaa);
    gl.readPixels(0, 0, SCENE_SIZE, SCENE_SIZE, gl.RGBA, gl.UNSIGNED_BYTE, buf);
    let aaUntouched = true;
    for (let i = 0; i < buf.length; i++) {
        if (buf[i] !== 0xaa) {
            aaUntouched = false;
            break;
        }
    }
    return {
        ok: true,
        seed: seedInfo(),
        vendor: dbg ? gl.getParameter(dbg.UNMASKED_VENDOR_WEBGL) : null,
        renderer: dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : null,
        maxes,
        shaderPrecision: gl.getShaderPrecisionFormat(gl.VERTEX_SHADER, gl.HIGH_FLOAT),
        activeAttrib: gl.getActiveAttrib(program, 0),
        activeUniform: gl.getActiveUniform(program, 0),
        readPixelsAaUntouched: aaUntouched,
    };
}

function withWorker() {
    return new Promise(resolve => {
        const worker = new Worker("worker.js");
        const timer = setTimeout(() => resolve({ error: "timeout" }), 8000);
        worker.onmessage = event => {
            clearTimeout(timer);
            resolve(event.data);
        };
        worker.onerror = event => {
            clearTimeout(timer);
            resolve({ error: event.message });
        };
        worker.postMessage({});
    });
}
