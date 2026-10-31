function createWorkerBridge() {
    const bridge = { onmessage: null };
    const queue = [];
    let port = null;

    bridge.postMessage = text => {
        if (port) port.postMessage(text);
        else queue.push(text);
    };

    self.addEventListener("message", function receivePort(event) {
        if (event.data?.xenonBridge !== true || !event.ports[0]) return;
        event.stopImmediatePropagation();
        self.removeEventListener("message", receivePort);

        port = event.ports[0];
        port.onmessage = message => bridge.onmessage?.(message.data);
        for (const text of queue.splice(0)) port.postMessage(text);
    });

    return bridge;
}
