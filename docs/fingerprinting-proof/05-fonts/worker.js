self.onmessage = event => {
    const families = event.data.families || [];
    const sample = event.data.sample || "mmmmmmmmmmlliWWWi1l";
    try {
        if (typeof OffscreenCanvas === "undefined") {
            postMessage({ error: "OffscreenCanvas missing", unsupported: true });
            return;
        }
        const canvas = new OffscreenCanvas(400, 50);
        const ctx = canvas.getContext("2d");
        if (!ctx) {
            postMessage({ error: "2d context missing", unsupported: true });
            return;
        }
        function width(fontFamily) {
            ctx.font = `72px ${fontFamily}`;
            return ctx.measureText(sample).width;
        }
        const mono = width("monospace");
        const serif = width("serif");
        const present = [];
        for (const family of families) {
            const withMono = width(`"${family}", monospace`);
            const withSerif = width(`"${family}", serif`);
            if (withMono !== mono || withSerif !== serif)
                present.push(family);
        }
        present.sort();
        postMessage({ present, ok: true });
    } catch (error) {
        postMessage({ error: String(error), unsupported: true });
    }
};
