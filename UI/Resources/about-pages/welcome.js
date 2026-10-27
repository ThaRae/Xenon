/*
 * Copyright (c) 2026-present, the Ladybird developers.
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

const form = document.querySelector("#welcome-form");
const steps = [...document.querySelectorAll(".step")];
const stepCounter = document.querySelector("#step-counter");
const useDefaults = document.querySelector("#use-defaults");
const privateEngines = document.querySelector("#private-engines");
const mainstreamEngines = document.querySelector("#mainstream-engines");
const searchPreview = document.querySelector("#search-preview");
const searchSuggestions = document.querySelector("#search-suggestions-enabled");
const searchSuggestionsLabel = document.querySelector("#search-suggestions-label");
const filterLists = document.querySelector("#filter-lists");
const filterListUpdates = document.querySelector("#filter-list-updates");
const filterListUpdatesEnabled = document.querySelector("#filter-list-updates-enabled");
const filterListsNote = document.querySelector("#filter-lists-note");
const startBrowsing = document.querySelector("#start-browsing");
const saveStatus = document.querySelector("#save-status");

const DEFAULT_ENGINE = "DuckDuckGo";
const PREVIEW_QUERY = "ladybird browser";

// Built-in engines, as Xenon sorts them. Anything else (a custom engine) is listed as mainstream, at the end.
const engineInfo = {
    DuckDuckGo: { group: "private", note: "No tracking", host: "duckduckgo.com/?q=" },
    Brave: { group: "private", note: "Own index", host: "search.brave.com/search?q=" },
    Startpage: { group: "private", note: "Google, anonymized", host: "startpage.com/search?q=" },
    Mojeek: { group: "private", note: "Own index", host: "mojeek.com/search?q=" },
    Kagi: { group: "private", note: "Paid, no ads", host: "kagi.com/search?q=" },
    Ecosia: { group: "private", note: "Plants trees", host: "ecosia.org/search?q=" },
    Google: { group: "mainstream", note: "", host: "google.com/search?q=" },
    Bing: { group: "mainstream", note: "", host: "bing.com/search?q=" },
    Yahoo: { group: "mainstream", note: "Bing results", host: "search.yahoo.com/search?p=" },
    Yandex: { group: "mainstream", note: "", host: "yandex.com/search/?text=" },
};

let settings;
let engines;
let initialized = false;

function showStep(name, { focus = true } = {}) {
    const index = Math.max(
        0,
        steps.findIndex(step => step.dataset.step === name)
    );
    steps.forEach((step, i) => {
        step.hidden = i !== index;
    });

    document.body.dataset.step = steps[index].dataset.step;

    // The greeting stands on its own; the questions are counted.
    stepCounter.textContent = index === 0 ? "" : `${index} of ${steps.length - 1}`;

    if (location.hash.slice(1) !== steps[index].dataset.step)
        history.replaceState(null, "", `#${steps[index].dataset.step}`);
    if (focus) steps[index].querySelector("h1").focus({ preventScroll: true });
    window.scrollTo(0, 0);
}

function selectedEngine() {
    return document.querySelector('input[name="engine"]:checked');
}

function updateSearchDetails() {
    const engine = selectedEngine();
    const name = engine?.value ?? "";
    const host = engineInfo[name]?.host;

    searchPreview.replaceChildren();
    if (!name) {
        searchPreview.append("Typing ", kbd(PREVIEW_QUERY), " will be treated as a web address.");
    } else if (host) {
        const url = document.createElement("span");
        url.className = "preview-url";
        url.textContent = host + PREVIEW_QUERY.replaceAll(" ", "+");
        searchPreview.append("Typing ", kbd(PREVIEW_QUERY), " goes to ", url);
    } else {
        searchPreview.append("Typing ", kbd(PREVIEW_QUERY), ` searches ${name}.`);
    }

    const supportsSuggestions = engine?.dataset.supportsSuggestions === "true";
    searchSuggestions.disabled = !supportsSuggestions;
    if (!supportsSuggestions) searchSuggestions.checked = false;
    searchSuggestionsLabel.textContent = supportsSuggestions
        ? `Suggest searches while I type (sends keystrokes to ${name})`
        : `Suggestions aren't available${name ? ` for ${name}` : ""}`;
}

function kbd(text) {
    const element = document.createElement("kbd");
    element.textContent = text;
    return element;
}

function addEngine(container, name, note, supportsSuggestions, checked) {
    const row = document.createElement("label");
    row.className = "choice";

    const radio = document.createElement("input");
    radio.type = "radio";
    radio.name = "engine";
    radio.value = name;
    radio.checked = checked;
    radio.dataset.supportsSuggestions = supportsSuggestions;
    radio.addEventListener("change", updateSearchDetails);

    const marker = document.createElement("span");
    marker.className = "marker";
    marker.setAttribute("aria-hidden", "true");

    const label = document.createElement("span");
    label.className = "choice-name";
    label.textContent = name || "None";

    const detail = document.createElement("span");
    detail.className = "choice-note";
    detail.textContent = note;

    row.append(radio, marker, label, detail);
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

function blockingEnabled() {
    return document.querySelector('input[name="ad-blocking"]:checked').value === "on";
}

function updateBlockingDetails() {
    filterLists.classList.toggle("hidden", !blockingEnabled());
    filterListUpdates.classList.toggle("hidden", !blockingEnabled());
    filterListsNote.classList.toggle("hidden", !blockingEnabled());
    filterListsNote.textContent = filterListUpdatesEnabled.checked
        ? "Once a day, Xenon downloads the latest lists from git.avx512.dev, and uses them only if they carry Xenon's signature."
        : "The lists come with Xenon and update when Xenon does.";
}

function initialize() {
    if (initialized || !settings || !engines) return;
    initialized = true;

    populateEngines();
    searchSuggestions.checked = settings.searchEngine.suggestions;
    updateSearchDetails();

    updateBlockingDetails();

    for (const button of document.querySelectorAll("button")) button.disabled = false;
}

// Nothing is saved until the user is done, so the choices shown here only take effect with their go-ahead.
function finish() {
    for (const button of document.querySelectorAll("button")) button.disabled = true;
    saveStatus.textContent = "";

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

for (const radio of document.querySelectorAll('input[name="ad-blocking"]')) {
    radio.addEventListener("change", updateBlockingDetails);
}
filterListUpdatesEnabled.addEventListener("change", updateBlockingDetails);

useDefaults.addEventListener("click", () => {
    if (!initialized) return;

    const defaultEngine = document.querySelector(`input[name="engine"][value="${DEFAULT_ENGINE}"]`);
    if (defaultEngine) defaultEngine.checked = true;
    updateSearchDetails();
    document.querySelector('input[name="ad-blocking"][value="on"]').checked = true;
    filterListUpdatesEnabled.checked = true;
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
});

document.addEventListener("WebUIMessage", event => {
    const { name, data } = event.detail;

    if (name === "loadSettings") {
        settings = { ...settings, ...data };
    } else if (name === "loadEngines") {
        engines = data;
    } else if (name === "firstRunCompleted") {
        location.replace(data);
    } else if (name === "firstRunError") {
        saveStatus.textContent = data;
        for (const button of document.querySelectorAll("button")) button.disabled = false;
    }

    initialize();
});
