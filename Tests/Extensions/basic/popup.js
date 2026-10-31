document.getElementById("greeting").textContent = chrome.i18n.getMessage("greeting", "Xenon");
document.getElementById("origin").textContent = location.origin;
document.getElementById("close").addEventListener("click", () => window.close());

chrome.storage.local.get({ opened: 0 }).then(async ({ opened }) => {
    await chrome.storage.local.set({ opened: opened + 1 });
    document.getElementById("count").textContent = `Opened ${opened + 1} ${opened === 0 ? "time" : "times"}.`;
});
