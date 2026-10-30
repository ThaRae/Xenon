function info() {
    try {
        if (typeof internals === "undefined") return "no-internals";
        return internals.farblingSeedInfo();
    } catch (error) {
        return "error:" + error;
    }
}
postMessage(info());
