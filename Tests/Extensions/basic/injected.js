// A file scripting.executeScript() runs in the test extension's isolated world (see background.js). Its result is its
// last expression: what it finds of chrome.* and of the content scripts that ran in the same world.
`${typeof chrome.runtime.id === "string" ? "isolated" : "main"}:${globalThis.xenonContentTest?.order.join()}`;
