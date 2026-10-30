const SCENE_WIDTH = 320;
const SCENE_HEIGHT = 180;

function drawScene(ctx, width, height) {
    ctx.fillStyle = "#1b3a4b";
    ctx.fillRect(0, 0, width, height);

    const sky = ctx.createLinearGradient(0, 0, width, height);
    sky.addColorStop(0, "#ff7a18");
    sky.addColorStop(0.45, "#af002d");
    sky.addColorStop(1, "#2575fc");
    ctx.fillStyle = sky;
    ctx.beginPath();
    ctx.moveTo(16, height - 28);
    ctx.lineTo(width * 0.35, 24);
    ctx.lineTo(width - 20, height - 36);
    ctx.closePath();
    ctx.fill();

    ctx.beginPath();
    ctx.arc(width * 0.72, height * 0.38, 44, 0.15 * Math.PI, 1.85 * Math.PI);
    ctx.strokeStyle = "rgba(255,255,255,0.92)";
    ctx.lineWidth = 7;
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(width * 0.28, height * 0.58, 26, 0, 2 * Math.PI);
    ctx.fillStyle = "rgba(40, 220, 190, 0.85)";
    ctx.fill();

    ctx.font = "22px sans-serif";
    ctx.fillStyle = "#111111";
    ctx.fillText("Xenon canvas 🦊", 22, height - 22);
    ctx.font = "16px serif";
    ctx.fillStyle = "#f4f1de";
    ctx.fillText("Aa Gg 123", 22, 32);
}

function bytesToBase64(bytes) {
    let binary = "";
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk)
        binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    return btoa(binary);
}

function sha256Hex(bytes) {
    // FNV-1a 64-bit. Ladybird's WebCrypto subtle is not exposed to these pages.
    let hash = 14695981039346656037n;
    for (let i = 0; i < bytes.length; i++) {
        hash ^= BigInt(bytes[i]);
        hash = (hash * 1099511628211n) & 0xffffffffffffffffn;
    }
    return hash.toString(16).padStart(16, "0");
}

function canvasToBlob(canvas, type) {
    return new Promise((resolve, reject) => {
        canvas.toBlob(blob => {
            if (blob)
                resolve(blob);
            else
                reject(new Error("toBlob returned null"));
        }, type || "image/png");
    });
}

async function blobBytes(blob) {
    return new Uint8Array(await blob.arrayBuffer());
}

function isAllZero(bytes) {
    for (let i = 0; i < bytes.length; i++) {
        if (bytes[i] !== 0)
            return false;
    }
    return true;
}

async function measureDrawnCanvas(canvas, includePixels) {
    const width = canvas.width;
    const height = canvas.height;
    const ctx = canvas.getContext("2d");
    const imageData = ctx.getImageData(0, 0, width, height);
    const pixels = new Uint8Array(imageData.data.buffer.slice(0));
    const dataURL = canvas.toDataURL("image/png");
    const blob = await canvasToBlob(canvas, "image/png");
    const blobData = await blobBytes(blob);
    const dataURLBytes = Uint8Array.from(atob(dataURL.split(",")[1]), c => c.charCodeAt(0));

    let offBytes = null;
    let offscreenError = null;
    try {
        const off = new OffscreenCanvas(width, height);
        const offCtx = off.getContext("2d");
        drawScene(offCtx, width, height);
        const offBlob = await off.convertToBlob({ type: "image/png" });
        offBytes = await blobBytes(offBlob);
    } catch (error) {
        offscreenError = String(error);
    }

    let createImageBitmapOk = false;
    try {
        const bitmap = await createImageBitmap(canvas);
        createImageBitmapOk = bitmap.width === width && bitmap.height === height;
        bitmap.close();
    } catch (error) {
        createImageBitmapOk = "error:" + String(error);
    }

    let transferOk = false;
    try {
        const transferSource = new OffscreenCanvas(width, height);
        drawScene(transferSource.getContext("2d"), width, height);
        const transferred = transferSource.transferToImageBitmap();
        transferOk = transferred.width === width && transferred.height === height;
        transferred.close();
    } catch (error) {
        transferOk = "error:" + String(error);
    }

    const blank = document.createElement("canvas");
    blank.width = width;
    blank.height = height;
    const blankPixels = blank.getContext("2d").getImageData(0, 0, width, height).data;
    const blankZero = isAllZero(blankPixels);

    const result = {
        width,
        height,
        getImageDataSha: sha256Hex(pixels),
        toDataURLSha: sha256Hex(dataURLBytes),
        toBlobSha: sha256Hex(blobData),
        convertToBlobMainSha: offBytes ? sha256Hex(offBytes) : null,
        offscreenError,
        pngBytesEqual: toEqual(dataURLBytes, blobData) && (!offBytes || toEqual(blobData, offBytes)),
        blankZero,
        createImageBitmapOk,
        transferToImageBitmapOk: transferOk,
        pixelCount: pixels.length,
    };
    if (includePixels) {
        result.pixelsB64 = bytesToBase64(pixels);
        result.pngB64 = bytesToBase64(blobData);
    }
    return result;
}

function toEqual(a, b) {
    if (a.length !== b.length)
        return false;
    for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i])
            return false;
    }
    return true;
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
