// Reports what the page can see of its own origin and of the chrome.* APIs, for Meta/xenon.py test.
const results = {
    origin: location.origin,
    isSecureContext: window.isSecureContext,
    hasSubtleCrypto: typeof crypto.subtle === "object",
    hasChrome: typeof chrome === "object",
};

try {
    localStorage.setItem("xenon-test", "1");
    results.localStorage = localStorage.getItem("xenon-test") === "1";
} catch (error) {
    results.localStorage = String(error);
}

document.getElementById("status").textContent = "The popup's script ran.";
document.getElementById("results").textContent = JSON.stringify(results, null, 4);
