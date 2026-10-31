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

function bytesOf(arr) {
    return new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
}

function float32ToB64(arr) {
    const bytes = bytesOf(arr);
    const chunk = 0x8000;
    let binary = "";
    for (let i = 0; i < bytes.length; i += chunk)
        binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    return btoa(binary);
}

async function digestHex(bytes) {
    if (globalThis.crypto && crypto.subtle) {
        try {
            const hash = await crypto.subtle.digest("SHA-256", bytes);
            return "sha256:" + [...new Uint8Array(hash)].map(b => b.toString(16).padStart(2, "0")).join("");
        } catch {
        }
    }
    return "fnv1a64:" + fnv1a64(bytes);
}

function arraysEqual(a, b) {
    if (a.length !== b.length)
        return false;
    for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i])
            return false;
    }
    return true;
}

function fingerprintSum(data) {
    let sum = 0;
    for (let i = 4500; i < 5000; i++)
        sum += Math.abs(data[i]);
    return sum;
}

function probes(data) {
    const out = [];
    for (let i = 4500; i < 5000; i += 8)
        out.push({ i, v: data[i] });
    return out;
}

async function hashFloat32(data) {
    return digestHex(bytesOf(Float32Array.from(data)));
}

async function renderTriangleCompressor() {
    if (typeof OfflineAudioContext === "undefined")
        throw new Error("OfflineAudioContext missing");
    const ctx = new OfflineAudioContext(1, 44100, 44100);
    const osc = ctx.createOscillator();
    osc.type = "triangle";
    osc.frequency.value = 10000;
    const comp = ctx.createDynamicsCompressor();
    osc.connect(comp);
    comp.connect(ctx.destination);
    osc.start(0);
    return ctx.startRendering();
}

async function measureAnalyser() {
    try {
        const ctx = new OfflineAudioContext(1, 44100, 44100);
        const osc = ctx.createOscillator();
        osc.type = "triangle";
        osc.frequency.value = 10000;
        const analyser = ctx.createAnalyser();
        analyser.fftSize = 2048;
        osc.connect(analyser);
        analyser.connect(ctx.destination);
        osc.start(0);
        await ctx.startRendering();
        const floatFreq = new Float32Array(analyser.frequencyBinCount);
        const byteFreq = new Uint8Array(analyser.frequencyBinCount);
        const floatTime = new Float32Array(analyser.fftSize);
        const byteTime = new Uint8Array(analyser.fftSize);
        analyser.getFloatFrequencyData(floatFreq);
        analyser.getByteFrequencyData(byteFreq);
        analyser.getFloatTimeDomainData(floatTime);
        analyser.getByteTimeDomainData(byteTime);
        let floatTimeAbs = 0;
        let byteTimeChanged = false;
        let byteFreqChanged = false;
        for (let i = 0; i < floatTime.length; i++)
            floatTimeAbs += Math.abs(floatTime[i]);
        for (let i = 0; i < byteTime.length; i++) {
            if (byteTime[i] !== 128)
                byteTimeChanged = true;
        }
        for (let i = 0; i < byteFreq.length; i++) {
            if (byteFreq[i] !== 0)
                byteFreqChanged = true;
        }
        return {
            ok: true,
            floatFreqHash: await digestHex(bytesOf(floatFreq)),
            byteFreqHash: await digestHex(byteFreq),
            floatTimeHash: await digestHex(bytesOf(floatTime)),
            byteTimeHash: await digestHex(byteTime),
            floatFreqB64: float32ToB64(floatFreq),
            floatTimeAbs,
            byteTimeChanged,
            byteFreqChanged,
        };
    } catch (error) {
        return { ok: false, error: String(error) };
    }
}

function measureWriteNoDrift() {
    const buffer = new AudioBuffer({ numberOfChannels: 1, length: 8, sampleRate: 44100 });
    const source = new Float32Array([0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]);
    buffer.copyToChannel(source, 0);
    const first = Float32Array.from(buffer.getChannelData(0));
    const second = Float32Array.from(buffer.getChannelData(0));
    return {
        first: Array.from(first),
        second: Array.from(second),
        equal: arraysEqual(first, second),
        matchesWrite: arraysEqual(first, source),
    };
}

async function measureAudio() {
    const result = {
        seed: seedInfo(),
        ok: false,
        cryptoSubtle: !!(globalThis.crypto && crypto.subtle),
    };
    try {
        const buffer = await renderTriangleCompressor();
        const first = buffer.getChannelData(0);
        const second = buffer.getChannelData(0);
        const copy = new Float32Array(first.length);
        buffer.copyFromChannel(copy, 0);
        const firstCopy = Float32Array.from(first);
        result.sum = fingerprintSum(first);
        result.probes = probes(first);
        result.samplesB64 = float32ToB64(firstCopy);
        result.getChannelDataHash = await hashFloat32(firstCopy);
        result.getChannelDataHashAgain = await hashFloat32(second);
        result.copyFromChannelHash = await hashFloat32(copy);
        result.sameView = first === second;
        result.twiceIdentical = arraysEqual(first, second);
        result.equalsCopyFromChannel = arraysEqual(first, copy);
        result.writeNoDrift = measureWriteNoDrift();
        result.analyser = await measureAnalyser();
        result.ok = true;
        result.length = first.length;
        result.sampleRate = buffer.sampleRate;
    } catch (error) {
        result.error = String(error);
    }
    return result;
}

function withWorker() {
    return new Promise(resolve => {
        try {
            const worker = new Worker("worker.js");
            const timer = setTimeout(() => {
                resolve({ error: "timeout", unsupported: true });
            }, 8000);
            worker.onmessage = event => {
                clearTimeout(timer);
                resolve(event.data);
            };
            worker.onerror = event => {
                clearTimeout(timer);
                resolve({ error: event.message || "worker-error", unsupported: true });
            };
            worker.postMessage({});
        } catch (error) {
            resolve({ error: String(error), unsupported: true });
        }
    });
}
