const FAMILIES = [
    "DejaVu Sans", "DejaVu Sans Mono", "DejaVu Serif", "FontAwesome",
    "Lato", "Lato Black", "Lato Hairline", "Lato Heavy", "Lato Light",
    "Lato Medium", "Lato Semibold", "Lato Thin",
    "Liberation Mono", "Liberation Sans", "Liberation Serif",
    "SerenitySans", "Noto Emoji",
    "Arial", "Helvetica", "Times New Roman", "Times", "Courier New", "Courier",
    "Georgia", "Verdana", "Trebuchet MS", "Comic Sans MS", "Impact", "Papyrus",
    "Palatino", "Garamond", "Bookman", "Calibri", "Cambria", "Segoe UI",
    "Tahoma", "Lucida Console", "Menlo", "Monaco", "San Francisco",
    "Apple Color Emoji", "MS Gothic", "SimSun", "Malgun Gothic",
];
const SAMPLE = "mmmmmmmmmmlliWWWi1l";

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

function spanWidth(fontFamily) {
    const span = document.createElement("span");
    span.style.cssText = "position:absolute;left:-9999px;font-size:72px;line-height:normal;white-space:nowrap";
    span.style.fontFamily = fontFamily;
    span.textContent = SAMPLE;
    (document.body || document.documentElement).appendChild(span);
    const width = span.getBoundingClientRect().width;
    span.remove();
    return width;
}

function detectWidth(family) {
    const mono = spanWidth("monospace");
    const serif = spanWidth("serif");
    const withMono = spanWidth(`"${family}", monospace`);
    const withSerif = spanWidth(`"${family}", serif`);
    return withMono !== mono || withSerif !== serif;
}

function canvasWidth(fontFamily) {
    const canvas = document.createElement("canvas");
    const ctx = canvas.getContext("2d");
    ctx.font = `72px ${fontFamily}`;
    return ctx.measureText(SAMPLE).width;
}

function detectCanvas(family) {
    const mono = canvasWidth("monospace");
    const serif = canvasWidth("serif");
    const withMono = canvasWidth(`"${family}", monospace`);
    const withSerif = canvasWidth(`"${family}", serif`);
    return withMono !== mono || withSerif !== serif;
}

function detectCheck(family) {
    try {
        return document.fonts.check(`72px "${family}"`);
    } catch {
        return null;
    }
}

function detectFamily(family) {
    const width = detectWidth(family);
    const canvas = detectCanvas(family);
    const check = detectCheck(family);
    return { family, width, canvas, check };
}

function withTimeout(promise, ms) {
    return Promise.race([
        promise,
        new Promise((_, reject) => setTimeout(() => reject(new Error("timeout")), ms)),
    ]);
}

async function detectWebFont() {
    const face = new FontFace("ProofWebFont", "url(HashSans.woff)");
    try {
        await withTimeout(face.load(), 3000);
        document.fonts.add(face);
        const web = spanWidth("ProofWebFont");
        const fallback = spanWidth("serif");
        document.fonts.delete(face);
        return { ok: true, renders: web !== fallback, web, fallback };
    } catch (error) {
        return { ok: false, error: String(error) };
    }
}

async function detectLocal(family) {
    const face = new FontFace("ProofLocalProbe", `local('${family}')`);
    try {
        document.fonts.add(face);
        await withTimeout(face.load(), 1500);
        const probe = spanWidth("ProofLocalProbe");
        const fallback = spanWidth("serif");
        const used = probe !== fallback;
        document.fonts.delete(face);
        return { family, used, probe, fallback };
    } catch (error) {
        try { document.fonts.delete(face); } catch {}
        return { family, used: false, error: String(error) };
    }
}

function measureAll() {
    const detections = FAMILIES.map(detectFamily);
    const present = detections.filter(item => item.width).map(item => item.family);
    present.sort();
    return {
        seed: seedInfo(),
        detections,
        present,
        hash: "fnv1a64:" + fnv1a64(present.join("|")),
    };
}

async function attachWebFontRow(sample) {
    const face = new FontFace("ProofWebFont", "url(HashSans.woff)");
    try {
        await withTimeout(face.load(), 3000);
        document.fonts.add(face);
        const row = detectFamily("ProofWebFont");
        const web = spanWidth("ProofWebFont");
        const fallback = spanWidth("serif");
        document.fonts.delete(face);
        sample.webFontRow = { ...row, class: "url_web_font" };
        sample.webFont = { ok: true, renders: row.width === true, web, fallback };
    } catch (error) {
        sample.webFontRow = { family: "ProofWebFont", width: false, canvas: false, check: null, class: "url_web_font" };
        sample.webFont = { ok: false, error: String(error), renders: false };
    }
    return sample;
}

async function measureFonts() {
    const sample = measureAll();
    sample.webFont = await detectWebFont();
    const localCandidates = ["FontAwesome", "Lato", "DejaVu Sans", "Liberation Sans"];
    sample.localProbes = [];
    for (const family of localCandidates)
        sample.localProbes.push(await detectLocal(family));
    return sample;
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
            worker.postMessage({ families: FAMILIES, sample: SAMPLE });
        } catch (error) {
            resolve({ error: String(error), unsupported: true });
        }
    });
}
