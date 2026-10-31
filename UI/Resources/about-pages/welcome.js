/*
 * Copyright (c) 2026-present, the Ladybird developers.
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

const form = document.querySelector("#welcome-form");
const steps = [...document.querySelectorAll(".step")];
const railSteps = [...document.querySelectorAll("#rail li")];
const useDefaults = document.querySelector("#use-defaults");
const privateEngines = document.querySelector("#private-engines");
const mainstreamEngines = document.querySelector("#mainstream-engines");
const searchPreview = document.querySelector("#search-preview");
const searchSuggestions = document.querySelector("#search-suggestions-enabled");
const searchSuggestionsLabel = document.querySelector("#search-suggestions-label");
const filterListUpdates = document.querySelector("#filter-list-updates");
const filterListUpdatesEnabled = document.querySelector("#filter-list-updates-enabled");
const passwordManagerList = document.querySelector("#password-managers");
const passwordManagerNote = document.querySelector("#password-manager-note");
const passwordManagerPreset = document.querySelector("#password-manager-preset");
const startBrowsing = document.querySelector("#start-browsing");

const isMac = navigator.platform.startsWith("Mac");

const DEFAULT_ENGINE = "DuckDuckGo";
const PREVIEW_QUERY = "ladybird browser";

const layoutInfo = {
    classic: { name: "Classic", note: "Tabs above the toolbar" },
    compact: { name: "Compact", note: "Tabs and toolbar in one row" },
    vertical: { name: "Vertical", note: "Tabs in a sidebar" },
};

// Built-in engines, as Xenon sorts them. Anything else (a custom engine) is listed as mainstream, at the end.
const engineInfo = {
    DuckDuckGo: { group: "private", note: "No tracking", host: "duckduckgo.com/?q=" },
    Brave: { group: "private", note: "Its own index", host: "search.brave.com/search?q=" },
    Startpage: { group: "private", note: "Google's results, anonymized", host: "startpage.com/search?q=" },
    Mojeek: { group: "private", note: "Its own index", host: "mojeek.com/search?q=" },
    Kagi: { group: "private", note: "Paid, no ads", host: "kagi.com/search?q=" },
    Ecosia: { group: "private", note: "Plants trees with its profits", host: "ecosia.org/search?q=" },
    Google: { group: "mainstream", note: "The biggest index", host: "google.com/search?q=" },
    Bing: { group: "mainstream", note: "Microsoft's index", host: "bing.com/search?q=" },
    Yahoo: { group: "mainstream", note: "Bing's results", host: "search.yahoo.com/search?p=" },
    Yandex: { group: "mainstream", note: "Russia's own index", host: "yandex.com/search/?text=" },
};

// The password managers Xenon offers (UI/PasswordManagers.h), and who makes each one.
const passwordManagerInfo = {
    bitwarden: { note: "Open source, can be self-hosted", developer: "Bitwarden" },
    "proton-pass": { note: "Open source, with email aliases", developer: "Proton" },
    "1password": { note: "Paid", developer: "1Password" },
    "icloud-passwords": { note: "Your iCloud Keychain, on this Mac", developer: "Apple" },
};

let settings;
let xenonSettings;
let engines;
let passwordManagers;
// The password manager finishing waits to have installed (or removed, as ""), if it waits for one.
let awaitedPasswordManager;
let initialized = false;

for (const shortcut of document.querySelectorAll("kbd[data-other]")) {
    if (!isMac) shortcut.textContent = shortcut.dataset.other;
}

function showStep(name, { focus = true } = {}) {
    const index = Math.max(
        0,
        steps.findIndex(step => step.dataset.step === name)
    );
    steps.forEach((step, i) => {
        step.hidden = i !== index;
    });

    const current = steps[index].dataset.step;
    document.body.dataset.step = current;

    // The rail counts off the steps after the first screen: the ones before this one are done.
    const railIndex = railSteps.findIndex(step => step.dataset.for === current);
    railSteps.forEach((step, i) => {
        step.classList.toggle("done", i < railIndex);
        step.classList.toggle("current", i === railIndex);
        if (i === railIndex) step.setAttribute("aria-current", "step");
        else step.removeAttribute("aria-current");
    });

    if (location.hash.slice(1) !== current) history.replaceState(null, "", `#${current}`);
    if (focus) steps[index].querySelector("h1").focus({ preventScroll: true });
    window.scrollTo(0, 0);
}

function visibleStatus() {
    return steps.find(step => !step.hidden)?.querySelector("[data-status]");
}

function showSaveStatus(text, { inProgress = false } = {}) {
    for (const status of document.querySelectorAll("[data-status]")) status.textContent = "";
    const status = visibleStatus();
    if (!status) return;
    status.textContent = text;
    status.classList.toggle("in-progress", inProgress);
}

function setButtonsDisabled(disabled) {
    for (const button of document.querySelectorAll("button")) button.disabled = disabled;
}

// Layout

function selectedLayout() {
    return document.querySelector('input[name="tab-layout"]:checked')?.value ?? "classic";
}

function selectedSidebarSide() {
    return document.querySelector('input[name="sidebar-side"]:checked')?.value ?? "left";
}

// Search

function selectedEngine() {
    return document.querySelector('input[name="engine"]:checked');
}

function updateSearchDetails() {
    const engine = selectedEngine();
    const name = engine?.value ?? "";
    const host = engineInfo[name]?.host;

    const query = document.createElement("b");
    query.textContent = PREVIEW_QUERY;
    searchPreview.replaceChildren();
    if (!name) {
        searchPreview.append("Typing ", query, " will be treated as a web address.");
    } else if (host) {
        const url = document.createElement("span");
        url.className = "preview-url";
        url.textContent = host + PREVIEW_QUERY.replaceAll(" ", "+");
        searchPreview.append("A search for ", query, " goes to ", url);
    } else {
        searchPreview.append("A search for ", query, ` goes to ${name}.`);
    }

    const supportsSuggestions = engine?.dataset.supportsSuggestions === "true";
    searchSuggestions.disabled = !supportsSuggestions;
    if (!supportsSuggestions) searchSuggestions.checked = false;
    searchSuggestionsLabel.textContent = supportsSuggestions
        ? `Suggest searches as you type (sends what you type to ${name})`
        : `Suggestions aren't available${name ? ` for ${name}` : ""}`;

    updateSummaries();
}

function addEngine(container, name, note, supportsSuggestions, checked) {
    const row = document.createElement("label");
    row.className = "pick";

    const radio = document.createElement("input");
    radio.type = "radio";
    radio.name = "engine";
    radio.value = name;
    radio.checked = checked;
    radio.dataset.supportsSuggestions = supportsSuggestions;
    radio.addEventListener("change", updateSearchDetails);

    const monogram = document.createElement("span");
    monogram.className = container === privateEngines ? "monogram" : "monogram soft";
    monogram.setAttribute("aria-hidden", "true");
    monogram.textContent = name ? name.charAt(0) : "–";

    const text = document.createElement("span");
    text.className = "pick-text";
    const label = document.createElement("span");
    label.className = "pick-name";
    label.textContent = name || "None";
    const detail = document.createElement("span");
    detail.className = "pick-note";
    detail.textContent = note;
    text.append(label, detail);

    const dot = document.createElement("span");
    dot.className = "dot";
    dot.setAttribute("aria-hidden", "true");

    row.append(radio, monogram, text, dot);
    container.append(row);
}

function populateEngines() {
    const order = Object.keys(engineInfo);
    const rank = name => (order.includes(name) ? order.indexOf(name) : order.length);
    const available = [...engines.search, ...(settings.searchEngine.custom ?? [])].sort(
        (a, b) => rank(a.name) - rank(b.name)
    );

    // Ladybird starts with no search engine at all, which would leave the address bar unable to search.
    const current = settings.searchEngine.engine || DEFAULT_ENGINE;
    for (const engine of available) {
        const info = engineInfo[engine.name];
        const container = info?.group === "private" ? privateEngines : mainstreamEngines;
        const supportsSuggestions = Boolean(engine.supportsSuggestions || engine.suggestionsUrl);
        addEngine(container, engine.name, info?.note ?? "", supportsSuggestions, engine.name === current);
    }
    addEngine(mainstreamEngines, "", "Addresses only", false, false);
}

// Blocking

function blockingEnabled() {
    return document.querySelector('input[name="ad-blocking"]:checked').value === "on";
}

function updateBlockingDetails() {
    filterListUpdates.classList.toggle("hidden-updates", !blockingEnabled());
    updateSummaries();
}

// Password managers

function selectedPasswordManager() {
    return document.querySelector('input[name="password-manager"]:checked')?.value ?? "";
}

function passwordManagerName(id) {
    return passwordManagers?.choices.find(choice => choice.id === id)?.name ?? id;
}

function populatePasswordManagers() {
    const current = passwordManagers.current ?? "";
    const rows = [...passwordManagers.choices.map(choice => [choice.id, choice.name]), ["", "None for now"]];
    for (const [id, name] of rows) {
        const row = document.createElement("label");
        row.className = id ? "choice" : "choice none";

        const radio = document.createElement("input");
        radio.className = "choice-input";
        radio.type = "radio";
        radio.name = "password-manager";
        radio.value = id;
        radio.checked = id === current;
        radio.addEventListener("change", updatePasswordManagerDetails);
        row.append(radio);

        if (id) {
            const picture = document.createElement("span");
            picture.className = "choice-picture";
            picture.setAttribute("aria-hidden", "true");
            picture.textContent = name.charAt(0);
            row.append(picture);
        }

        const label = document.createElement("span");
        label.className = "choice-name";
        label.textContent = name;
        const lamp = document.createElement("span");
        lamp.className = "lamp";

        const detail = document.createElement("span");
        detail.className = "choice-note";
        detail.textContent = id ? (passwordManagerInfo[id]?.note ?? "") : "You can pick one later in Settings.";

        // A manager's lamp sits by its name. None is one long row, so its lamp goes at the far end.
        if (id) {
            label.append(lamp);
            row.append(label, detail);
        } else {
            row.append(label, detail, lamp);
        }
        passwordManagerList.append(row);
    }
}

function updatePasswordManagerDetails() {
    const id = selectedPasswordManager();
    const current = passwordManagers.current ?? "";

    const notes = [];
    if (id && id !== current) {
        const name = passwordManagerName(id);
        const developer = passwordManagerInfo[id]?.developer ?? name;
        notes.push(
            `Xenon installs ${name} when you finish: from the Chrome Web Store, after checking that ${developer} signed ` +
                "it. Xenon looks for updates once a day. To fill in your logins, it can read and change the sites you visit."
        );
        if (id === "icloud-passwords") {
            notes.push(
                "It talks to Apple's Password Manager on this Mac, the same helper Chrome uses. macOS Sonoma or later, " +
                    "and iCloud Keychain, are required."
            );
        }
    }
    if (id && id === current) notes.push(`${passwordManagerName(id)} is installed, and Xenon keeps it up to date.`);
    if (current && id !== current) notes.push(`${passwordManagerName(current)} is removed, with its data in Xenon.`);
    passwordManagerNote.textContent = notes.join(" ");

    updateSummaries();
}

// What the first screen and Done show: what's chosen, or kept.

function setSummary(key, text) {
    for (const element of document.querySelectorAll(`[data-summary="${key}"]`)) element.textContent = text;
}

function updateSummaries() {
    const layout = layoutInfo[selectedLayout()] ?? layoutInfo.classic;
    setSummary("layout", layout.name);
    setSummary("layout-note", layout.note);

    const engine = selectedEngine()?.value ?? "";
    setSummary("engine", engine || "None");
    if (!engine) setSummary("engine-note", "Typing goes to addresses only");
    else if (engineInfo[engine]?.group === "private")
        setSummary("engine-note", "It doesn't keep a profile of what you search");
    else setSummary("engine-note", "It uses what you search for ads");

    const blocking = document.querySelector('input[name="ad-blocking"]:checked')?.value !== "off";
    setSummary("blocking", blocking ? "Blocked" : "Shown");
    for (const lamp of document.querySelectorAll('[data-summary="blocking-lamp"]'))
        lamp.classList.toggle("on", blocking);

    const passwordManager = passwordManagers ? selectedPasswordManager() : "";
    setSummary("password-manager", passwordManager ? passwordManagerName(passwordManager) : "None for now");
    setSummary(
        "password-manager-note",
        passwordManager ? "Installed, and kept up to date" : "Pick one whenever you like"
    );
}

function initialize() {
    if (initialized || !settings || !xenonSettings || !engines || !passwordManagers) return;
    initialized = true;

    const layout = document.querySelector(`input[name="tab-layout"][value="${xenonSettings.layout}"]`);
    if (layout) layout.checked = true;
    const side = settings.tabs?.verticalTabsPosition === "right" ? "right" : "left";
    document.querySelector(`input[name="sidebar-side"][value="${side}"]`).checked = true;

    populateEngines();
    searchSuggestions.checked = settings.searchEngine.suggestions;
    updateSearchDetails();

    updateBlockingDetails();

    populatePasswordManagers();
    updatePasswordManagerDetails();

    setButtonsDisabled(false);
}

// Nothing is saved until the user is done, so the choices shown here only take effect with their go-ahead. A new
// password manager goes in first, since it's the one choice that can fail, and the rest is saved once it's in.
function finish() {
    setButtonsDisabled(true);
    showSaveStatus("");

    const passwordManager = selectedPasswordManager();
    if (passwordManager === (passwordManagers.current ?? "")) {
        saveSettings();
        return;
    }

    awaitedPasswordManager = passwordManager;
    if (passwordManager) {
        passwordManagerPreset.classList.add("is-installing");
        showSaveStatus(`Installing ${passwordManagerName(passwordManager)}…`, { inProgress: true });
    }
    ladybird.sendMessage("setPasswordManager", passwordManager || null);
}

function passwordManagersChanged() {
    if (awaitedPasswordManager === undefined || passwordManagers.installing) return;

    const awaited = awaitedPasswordManager;
    awaitedPasswordManager = undefined;
    passwordManagerPreset.classList.remove("is-installing");
    if ((passwordManagers.current ?? "") === awaited) {
        saveSettings();
        return;
    }

    showSaveStatus(passwordManagers.error ?? "Xenon couldn't change your password manager. Please try again.");
    setButtonsDisabled(false);
}

function saveLayout() {
    const layout = selectedLayout();
    const side = selectedSidebarSide();
    const tabs = settings.tabs ?? {};
    const sideChanged = layout === "vertical" && side !== (tabs.verticalTabsPosition ?? "left");
    if (layout === xenonSettings.layout && !sideChanged) return;

    // Ladybird resets whatever tab settings a message leaves out, so the side goes with the rest of them, before the
    // layout that turns the sidebar on.
    if (sideChanged) ladybird.sendMessage("setTabSettings", { ...tabs, verticalTabsPosition: side });
    ladybird.sendMessage("xenon/setLayout", layout);
}

function saveSettings() {
    saveLayout();

    ladybird.sendMessage("setSearchEngineSettings", {
        engine: selectedEngine()?.value || null,
        suggestions: searchSuggestions.checked && !searchSuggestions.disabled,
    });

    // Xenon's own lists (UI/FilterLists.cpp) apply whenever blocking is on. Ladybird's catalog holds the raw upstream
    // versions of some of them, which would only add duplicates without the fixes for Ladybird, so it stays off. That
    // leaves Xenon's daily lists (UI/FilterListUpdates.cpp) as all that Ladybird's filter list updates setting fetches.
    for (const list of settings.contentBlockerLists) {
        if (list.enabled)
            ladybird.sendMessage("setContentBlockerListEnabled", { identifier: list.identifier, enabled: false });
    }
    ladybird.sendMessage("setFilterListUpdatesEnabled", blockingEnabled() && filterListUpdatesEnabled.checked);
    ladybird.sendMessage("setContentBlockerEnabled", blockingEnabled());

    ladybird.sendMessage("completeFirstRun");
}

for (const button of document.querySelectorAll("[data-go]")) {
    button.addEventListener("click", () => showStep(button.dataset.go));
}

for (const radio of document.querySelectorAll('input[name="tab-layout"], input[name="sidebar-side"]')) {
    radio.addEventListener("change", updateSummaries);
}

for (const radio of document.querySelectorAll('input[name="ad-blocking"]')) {
    radio.addEventListener("change", updateBlockingDetails);
}

// Keeping the defaults keeps the layout and the password manager already in place.
useDefaults.addEventListener("click", () => {
    if (!initialized) return;

    const defaultEngine = document.querySelector(`input[name="engine"][value="${DEFAULT_ENGINE}"]`);
    if (defaultEngine) defaultEngine.checked = true;
    updateSearchDetails();
    document.querySelector('input[name="ad-blocking"][value="on"]').checked = true;
    filterListUpdatesEnabled.checked = true;
    updateBlockingDetails();
    finish();
});

form.addEventListener("submit", event => {
    event.preventDefault();
    if (initialized && !startBrowsing.disabled) finish();
});

window.addEventListener("hashchange", () => showStep(location.hash.slice(1)));
showStep(location.hash.slice(1), { focus: false });

document.addEventListener("WebUILoaded", () => {
    ladybird.sendMessage("loadAvailableEngines");
    ladybird.sendMessage("loadCurrentSettings");
    ladybird.sendMessage("xenon/loadSettings");
    ladybird.sendMessage("loadPasswordManagers");
});

document.addEventListener("WebUIMessage", event => {
    const { name, data } = event.detail;

    if (name === "loadSettings") {
        settings = { ...settings, ...data };
    } else if (name === "xenon/settings") {
        xenonSettings = data;
    } else if (name === "loadEngines") {
        engines = data;
    } else if (name === "passwordManagers") {
        passwordManagers = data;
        passwordManagersChanged();
    } else if (name === "firstRunCompleted") {
        location.replace(data);
    } else if (name === "firstRunError") {
        showSaveStatus(data);
        setButtonsDisabled(false);
    }

    initialize();
});
