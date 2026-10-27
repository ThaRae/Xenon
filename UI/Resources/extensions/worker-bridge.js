// The background worker's end of the script bridge (see background-page.js): the same interface an extension page's
// bridge has, over a MessagePort its page sends first. Messages wait until the port arrives. Xenon puts this and
// chrome-api.js inside one function in the worker, so neither adds anything to the worker's global scope.

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
        // The extension's own code never sees this message.
        event.stopImmediatePropagation();
        self.removeEventListener("message", receivePort);

        port = event.ports[0];
        port.onmessage = message => bridge.onmessage?.(message.data);
        for (const text of queue.splice(0)) port.postMessage(text);
    });

    return bridge;
}
