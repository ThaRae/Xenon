/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Xenon's settings page: its sections and search, and Xenon's own settings, which UI/SettingsPage.cpp answers for as
// "xenon/..." messages. Ladybird's scripts in about-pages/settings run Ladybird's settings, by their element ids.

const isMac = navigator.platform.startsWith("Mac");

// Ladybird's scripts name the tab of its page that each dialog belongs to (see dialog-deep-link.js), and other pages
// link to those tabs. Each comes to the section that holds the same settings here.
const SECTION_FOR_LADYBIRD_TAB = {
    general: "startup",
    languages: "startup",
    services: "privacy",
    appearance: "look",
};

const sections = [...document.querySelectorAll(".section")];
const navLinks = [...document.querySelectorAll(".sidebar nav a")];

function sectionForHash(hash) {
    const name = SECTION_FOR_LADYBIRD_TAB[hash] ?? hash;
    return sections.find(section => section.id === name) ?? null;
}

function setCurrentSection(section) {
    for (const link of navLinks) {
        const current = section !== null && link.hash === `#${section.id}`;
        link.classList.toggle("current", current);
        if (current) link.setAttribute("aria-current", "true");
        else link.removeAttribute("aria-current");
    }
}

// The section at the top of the window, or the last one once the page is scrolled to its end.
function updateCurrentSection() {
    const visible = sections.filter(section => !section.hidden);
    if (visible.length === 0) {
        setCurrentSection(null);
        return;
    }

    if (window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 2) {
        setCurrentSection(visible[visible.length - 1]);
        return;
    }

    let current = visible[0];
    for (const section of visible) {
        if (section.getBoundingClientRect().top <= 120) current = section;
    }
    setCurrentSection(current);
}

function scrollToHash({ smooth }) {
    const section = sectionForHash(location.hash.slice(1));
    if (!section) return;
    section.scrollIntoView({ behavior: smooth ? "smooth" : "auto", block: "start" });
    setCurrentSection(section);
}

window.addEventListener("scroll", updateCurrentSection, { passive: true });
window.addEventListener("resize", updateCurrentSection);
window.addEventListener("hashchange", () => scrollToHash({ smooth: true }));

// Finding a setting: rows match on their text and keywords, and a section's title shows all of it.
const findSetting = document.querySelector("#find-setting");
const noMatches = document.querySelector("#no-matches");

function textOf(element) {
    return `${element.textContent} ${element.dataset.keywords ?? ""}`.toLowerCase().replace(/\s+/g, " ");
}

function applyFind() {
    const query = findSetting.value.trim().toLowerCase();
    let anyMatch = false;

    for (const section of sections) {
        const title = textOf(section.querySelector("h1"));
        const showWholeSection = query.length === 0 || title.includes(query);
        let sectionMatches = showWholeSection;

        for (const group of section.querySelectorAll(".group")) {
            const groupTitle = group.querySelector(".group-title");
            let groupMatches = showWholeSection || (groupTitle !== null && textOf(groupTitle).includes(query));
            const showWholeGroup = groupMatches;

            for (const row of group.querySelectorAll(".rows .row, .layouts")) {
                const rowMatches = showWholeGroup || textOf(row).includes(query);
                row.classList.toggle("filtered-out", !rowMatches);
                groupMatches ||= rowMatches;
            }

            group.classList.toggle("filtered-out", !groupMatches);
            if (groupMatches && query.length !== 0 && group.tagName === "DETAILS") group.open = true;
            sectionMatches ||= groupMatches;
        }

        section.hidden = !sectionMatches;
        anyMatch ||= sectionMatches;
    }

    noMatches.hidden = anyMatch;
    noMatches.querySelector("span").textContent = findSetting.value.trim();
    updateCurrentSection();
}

findSetting.addEventListener("input", applyFind);
findSetting.addEventListener("keydown", event => {
    if (event.key === "Escape" && findSetting.value) {
        findSetting.value = "";
        applyFind();
    }
});

for (const shortcut of document.querySelectorAll("kbd[data-other]")) {
    if (!isMac) shortcut.textContent = shortcut.dataset.other;
}

// Layout, and the vertical tab options that come with it, which are Ladybird's tab settings.
const layoutRadios = [...document.querySelectorAll('input[name="layout"]')];
const verticalTabsPosition = document.querySelector("#vertical-tabs-position");
const verticalTabsExpandOnHover = document.querySelector("#vertical-tabs-expand-on-hover");

let XENON = null;
let TAB_SETTINGS = null;

// The vertical layout's picture puts the sidebar on the side it's on.
function renderVerticalTabsSide() {
    document.querySelector("#vertical-layout").classList.toggle("right-side", verticalTabsPosition.value === "right");
}

function renderLayout() {
    const layout = XENON?.layout;
    for (const radio of layoutRadios) radio.checked = radio.value === layout;

    const vertical = layout === "vertical";
    document.querySelector("#vertical-tabs-position-row").hidden = !vertical;
    document.querySelector("#vertical-tabs-expand-on-hover-row").hidden = !vertical;
}

for (const radio of layoutRadios) {
    radio.addEventListener("change", () => {
        if (radio.checked) ladybird.sendMessage("xenon/setLayout", radio.value);
    });
}

function saveTabSettings(changes) {
    if (!TAB_SETTINGS) return;
    TAB_SETTINGS = { ...TAB_SETTINGS, ...changes };
    ladybird.sendMessage("setTabSettings", TAB_SETTINGS);
}

verticalTabsPosition.addEventListener("change", () => {
    renderVerticalTabsSide();
    saveTabSettings({ verticalTabsPosition: verticalTabsPosition.value });
});
verticalTabsExpandOnHover.addEventListener("change", () =>
    saveTabSettings({ verticalTabsExpandOnHover: verticalTabsExpandOnHover.checked })
);

// Tabs.
const cycleTabs = document.querySelector("#cycle-tabs-in-recently-used-order");
const hibernationEnabled = document.querySelector("#tab-hibernation-enabled");
const hibernationTimeout = document.querySelector("#tab-hibernation-timeout");
const splitViewDragAndDrop = document.querySelector("#split-view-drag-and-drop");

function describeTimeout(seconds) {
    if (seconds % 3600 === 0) return seconds === 3600 ? "1 hour" : `${seconds / 3600} hours`;
    if (seconds % 60 === 0) return seconds === 60 ? "1 minute" : `${seconds / 60} minutes`;
    return `${seconds} seconds`;
}

function renderTabs() {
    cycleTabs.checked = XENON.cycleTabsInRecentlyUsedOrder;
    splitViewDragAndDrop.checked = XENON.splitViewDragAndDrop;

    const { enabled, timeoutSeconds, timeoutChoices } = XENON.tabHibernation;
    hibernationEnabled.checked = enabled;

    // A timeout set some other way, like a short one for testing, shows as a choice of its own.
    const choices = timeoutChoices.includes(timeoutSeconds) ? timeoutChoices : [...timeoutChoices, timeoutSeconds];
    hibernationTimeout.replaceChildren(
        ...choices
            .sort((a, b) => a - b)
            .map(seconds => {
                const option = document.createElement("option");
                option.value = seconds;
                option.textContent = describeTimeout(seconds);
                return option;
            })
    );
    hibernationTimeout.value = timeoutSeconds;
    hibernationTimeout.disabled = !enabled;

    for (const control of [cycleTabs, hibernationEnabled, splitViewDragAndDrop]) control.disabled = false;
}

cycleTabs.addEventListener("change", () =>
    ladybird.sendMessage("xenon/setCycleTabsInRecentlyUsedOrder", cycleTabs.checked)
);
hibernationEnabled.addEventListener("change", () => {
    hibernationTimeout.disabled = !hibernationEnabled.checked;
    ladybird.sendMessage("xenon/setTabHibernation", { enabled: hibernationEnabled.checked });
});
hibernationTimeout.addEventListener("change", () =>
    ladybird.sendMessage("xenon/setTabHibernation", { timeoutSeconds: Number(hibernationTimeout.value) })
);
splitViewDragAndDrop.addEventListener("change", () =>
    ladybird.sendMessage("xenon/setSplitViewDragAndDrop", splitViewDragAndDrop.checked)
);

// Bangs.
const bangsEnabled = document.querySelector("#bangs-enabled");
const bangFinder = document.querySelector("#bang-finder");
const bangResults = document.querySelector("#bang-results");

function renderBangs() {
    bangsEnabled.checked = XENON.bangs.enabled;
    bangsEnabled.disabled = false;
    bangFinder.placeholder = `${XENON.bangs.count.toLocaleString()} bangs, like !yt or YouTube`;
}

bangsEnabled.addEventListener("change", () => ladybird.sendMessage("xenon/setBangsEnabled", bangsEnabled.checked));
bangFinder.addEventListener("input", () => {
    if (bangFinder.value.trim()) ladybird.sendMessage("xenon/searchBangs", bangFinder.value);
    else bangResults.replaceChildren();
});

function showBangs({ query, bangs }) {
    if (query !== bangFinder.value) return;

    if (bangs.length === 0) {
        const none = document.createElement("li");
        none.className = "bang-none";
        none.textContent = "No bang has that trigger or site.";
        bangResults.replaceChildren(none);
        return;
    }

    bangResults.replaceChildren(
        ...bangs.map(bang => {
            const item = document.createElement("li");
            const trigger = document.createElement("kbd");
            trigger.textContent = `!${bang.trigger}`;
            const name = document.createElement("span");
            name.className = "bang-name";
            name.textContent = bang.name;
            const host = document.createElement("span");
            host.className = "bang-host";
            host.textContent = bang.host ?? "";
            item.append(trigger, name, host);
            return item;
        })
    );
}

// The count of what's blocked on the toolbar's shield.
const showContentBlockerCount = document.querySelector("#show-content-blocker-count");

function renderContentBlockerCount() {
    showContentBlockerCount.checked = XENON.showContentBlockerCount;
    showContentBlockerCount.disabled = false;
}

showContentBlockerCount.addEventListener("change", () =>
    ladybird.sendMessage("xenon/setShowContentBlockerCount", showContentBlockerCount.checked)
);

// Filter lists.
const filterListsSummary = document.querySelector("#filter-lists-summary");
const filterListsStatus = document.querySelector("#filter-lists-status");
const checkFilterLists = document.querySelector("#check-filter-lists");
const filterListUpdates = document.querySelector("#filter-list-updates");
let filterListsTimer;

// "2026-10-27 05:17 UTC", as the manifest writes it.
function parseGeneratedAt(generatedAt) {
    const match = /^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}) UTC$/.exec(generatedAt);
    if (!match) return null;
    const [, year, month, day, hours, minutes] = match.map(Number);
    return new Date(Date.UTC(year, month - 1, day, hours, minutes));
}

function formatDate(date) {
    return date.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
}

function renderFilterLists() {
    const lists = XENON.filterLists;
    clearTimeout(filterListsTimer);

    if (!lists) {
        filterListsSummary.textContent = "uBlock Origin's lists";
        filterListsStatus.textContent = "";
        checkFilterLists.hidden = true;
        return;
    }

    const generatedAt = parseGeneratedAt(lists.generatedAt);
    // Lists are turned off one by one in the blocking dashboard.
    const listsOn = Math.max(lists.listCount - lists.turnedOff, 0);
    filterListsSummary.textContent =
        lists.turnedOff > 0
            ? `uBlock Origin's lists, ${listsOn} of ${lists.listCount} on`
            : `uBlock Origin's ${lists.listCount} lists`;

    const parts = [
        `From ${generatedAt ? formatDate(generatedAt) : lists.generatedAt}`,
        lists.downloaded ? "downloaded" : "as they came with Xenon",
    ];
    if (lists.checking) parts.push("checking for new ones…");
    else if (lists.error) parts.push(`the last check failed: ${lists.error}`);
    else if (lists.checkedAt) parts.push(`checked ${formatDate(new Date(lists.checkedAt))}`);
    filterListsStatus.textContent = `${parts.join(", ")}.`;

    // The updater has nothing to say when it's done, so the page asks again while it works.
    checkFilterLists.hidden = !filterListUpdates.checked || filterListUpdates.disabled;
    checkFilterLists.disabled = lists.checking;
    checkFilterLists.textContent = lists.checking ? "Checking…" : "Check now";
    if (lists.checking) filterListsTimer = setTimeout(() => ladybird.sendMessage("xenon/loadSettings"), 1000);
}

checkFilterLists.addEventListener("click", () => ladybird.sendMessage("xenon/checkFilterLists"));
filterListUpdates.addEventListener("change", () => {
    if (XENON) renderFilterLists();
});

// Updates, which Xenon gets from Sparkle on macOS (UI/Updater.h).
const updatesVersion = document.querySelector("#updates-version");
const updatesStatus = document.querySelector("#updates-status");
const checkForUpdates = document.querySelector("#check-for-updates");
const installUpdate = document.querySelector("#install-update");
const automaticUpdatesRow = document.querySelector("#automatic-updates-row");
const automaticUpdates = document.querySelector("#automatic-updates");
const automaticUpdatesDescription = document.querySelector("#automatic-updates-description");
const automaticUpdatesText = automaticUpdatesDescription.textContent;

function updatesStatusText(updates) {
    switch (updates.availability) {
        case "developmentBuild":
            return "This development build doesn't update itself. Releases packaged by Meta/release.py do.";
        case "unsupportedPlatform":
            return "Xenon updates itself on macOS. Here, update it the way you installed it.";
    }
    if (updates.readyVersion) return `Xenon ${updates.readyVersion} is ready, and installs when you quit Xenon.`;
    if (updates.checking) return "Checking for a new version…";
    if (updates.error) return `The last check failed: ${updates.error}`;
    if (updates.checkedAt) return `Up to date. Checked ${formatDate(new Date(updates.checkedAt))}.`;
    return "Not checked yet.";
}

// The updater tells the page whenever its status changes, so nothing here asks again.
function renderUpdates() {
    const updates = XENON.updates;

    const available = updates.availability === "available";
    const ready = updates.readyVersion !== undefined;
    updatesVersion.textContent = `Xenon ${updates.version}`;
    updatesStatus.textContent = updatesStatusText(updates);

    checkForUpdates.hidden = !available || ready;
    checkForUpdates.disabled = updates.checking;
    checkForUpdates.textContent = updates.checking ? "Checking…" : "Check now";
    installUpdate.hidden = !available || !ready;

    automaticUpdatesRow.hidden = !available;
    automaticUpdates.checked = updates.automatic;
    automaticUpdatesDescription.textContent =
        updates.automatic && !updates.backgroundConnections
            ? "Paused while Xenon can't connect on its own (Privacy and data). Check now still works."
            : automaticUpdatesText;
}

checkForUpdates.addEventListener("click", () => ladybird.sendMessage("xenon/checkForUpdates"));
installUpdate.addEventListener("click", () => ladybird.sendMessage("xenon/installUpdate"));
automaticUpdates.addEventListener("change", () => {
    ladybird.sendMessage("xenon/setAutomaticUpdates", automaticUpdates.checked);
});

// Extensions.
const extensionList = document.querySelector("#extension-list");

function renderExtensions() {
    if (XENON.extensions.length === 0) {
        const none = document.createElement("p");
        none.className = "row note-row";
        none.textContent = "No extensions yet.";
        extensionList.replaceChildren(none);
        return;
    }

    extensionList.replaceChildren(
        ...XENON.extensions.map(extension => {
            const row = document.createElement("div");
            row.className = "row extension";

            const icon = document.createElement("span");
            icon.className = "extension-icon";
            if (extension.icon) {
                const image = document.createElement("img");
                image.src = extension.icon;
                image.alt = "";
                icon.append(image);
            } else {
                icon.textContent = extension.name.slice(0, 1);
            }

            const text = document.createElement("div");
            text.className = "label";
            const name = document.createElement("span");
            name.textContent = extension.name;
            const version = document.createElement("span");
            version.className = "extension-version";
            version.textContent = extension.version;
            text.append(name, " ", version);
            if (extension.description) {
                const description = document.createElement("p");
                description.className = "description";
                description.textContent = extension.description;
                text.append(description);
            }
            if (!extension.persisted) {
                const session = document.createElement("p");
                session.className = "description";
                session.textContent = "Loaded for this session only.";
                text.append(session);
            }

            row.append(icon, text);
            return row;
        })
    );
}

document
    .querySelector("#show-extensions")
    .addEventListener("click", () => ladybird.sendMessage("xenon/showExtensions"));

// Ladybird's startup.js resets the new tab page to about:blank. Xenon's is its own about:newtab, so this runs after that
// and resets it to about:newtab instead.
const newTabPageURL = document.querySelector("#new-tab-page-url");

document.querySelector("#new-tab-page-url-reset").addEventListener("click", () => {
    newTabPageURL.value = "about:newtab";
    newTabPageURL.dispatchEvent(new Event("change"));
});

// Profile.
function renderProfile() {
    const { name, temporary, locations } = XENON.profile;
    let lede = "Where Xenon keeps what it saves.";
    if (temporary) lede = "This profile is temporary, and goes away when Xenon quits.";
    else if (name !== "default") lede = `Where Xenon keeps what it saves for the “${name}” profile.`;
    document.querySelector("#profile-lede").textContent = lede;

    for (const row of document.querySelectorAll(".row.location")) {
        const path = locations[row.dataset.location];
        row.hidden = !path;
        row.querySelector(".path").textContent = path ?? "";
    }
}

for (const button of document.querySelectorAll("[data-show-location]")) {
    button.addEventListener("click", () => ladybird.sendMessage("xenon/showLocation", button.dataset.showLocation));
}

// The blocking dashboard, once Xenon has one, turns each list on or off and keeps your own filters, so the page links to
// its tabs for those rather than editing them here.
function renderBlockingDashboard() {
    const url = XENON.blockingDashboard;
    document.querySelector("#blocking-dashboard-row").hidden = !url;
    document.querySelector("#custom-filters-row").hidden = !!url;

    const chooseLists = document.querySelector("#choose-filter-lists");
    chooseLists.hidden = !url;

    const destination = document.querySelector("#element-picker-destination");
    if (!url) {
        destination.textContent = "your own filters below";
        return;
    }

    document.querySelector("#blocking-dashboard").href = url;
    chooseLists.href = `${url}#lists`;
    const myFilters = document.createElement("a");
    myFilters.href = `${url}#filters`;
    myFilters.textContent = "My filters";
    destination.replaceChildren(myFilters, ", in the dashboard");
}

// The password manager, which UI/PasswordManagers.h installs from its developer's Chrome Web Store listing. Without one,
// the section says how to load one by hand.
const passwordManagerChoices = document.querySelector("#password-manager-choices");
const passwordManagerStatus = document.querySelector("#password-manager-status");
let PASSWORD_MANAGERS = null;

function passwordManagerName(id) {
    return PASSWORD_MANAGERS.choices.find(choice => choice.id === id)?.name ?? id;
}

function renderPasswordManagers() {
    const { choices, current, installing, error } = PASSWORD_MANAGERS;
    document.querySelector("#password-manager-group").hidden = false;
    document.querySelector("#password-manager-note-group").hidden = true;

    const selected = installing ?? current ?? "";
    const rows = [...choices.map(choice => [choice.id, choice.name]), ["", "None"]];
    passwordManagerChoices.replaceChildren(
        ...rows.map(([id, name]) => {
            const row = document.createElement("label");
            row.className = "row choice-row";

            const radio = document.createElement("input");
            radio.type = "radio";
            radio.name = "password-manager";
            radio.value = id;
            radio.checked = id === selected;
            radio.disabled = installing != null;
            radio.addEventListener("change", () => choosePasswordManager(id));

            const marker = document.createElement("span");
            marker.className = "marker";
            marker.setAttribute("aria-hidden", "true");

            const label = document.createElement("span");
            label.textContent = name;

            const note = document.createElement("span");
            note.className = "choice-note";
            if (id === installing) note.textContent = "Installing…";
            else if (id && id === current) note.textContent = "Installed";

            row.append(radio, marker, label, note);
            return row;
        })
    );

    if (installing)
        passwordManagerStatus.textContent = `Downloading ${passwordManagerName(installing)} from the Chrome Web Store…`;
    else if (error) passwordManagerStatus.textContent = error;
    else if (current)
        passwordManagerStatus.textContent = `${passwordManagerName(current)} is installed, and Xenon keeps it up to date.`;
    else
        passwordManagerStatus.textContent =
            "Xenon downloads the one you choose from the Chrome Web Store, signed by its developer.";
}

function choosePasswordManager(id) {
    const { current } = PASSWORD_MANAGERS;
    if (current && id !== current && !confirm(`${passwordManagerName(current)} is removed, with its data in Xenon.`)) {
        renderPasswordManagers();
        return;
    }
    ladybird.sendMessage("setPasswordManager", id || null);
}

function render() {
    renderLayout();
    renderTabs();
    renderBangs();
    renderContentBlockerCount();
    renderFilterLists();
    renderBlockingDashboard();
    renderExtensions();
    renderUpdates();
    renderProfile();
    if (findSetting.value) applyFind();
}

document.addEventListener("WebUILoaded", () => {
    ladybird.sendMessage("loadFeatures");
    ladybird.sendMessage("loadCurrentSettings");
    ladybird.sendMessage("xenon/loadSettings");
    ladybird.sendMessage("loadPasswordManagers");
});

document.addEventListener("WebUIMessage", event => {
    const { name, data } = event.detail;

    if (name === "xenon/settings") {
        XENON = data;
        render();
    } else if (name === "xenon/bangs") {
        showBangs(data);
    } else if (name === "passwordManagers") {
        PASSWORD_MANAGERS = data;
        renderPasswordManagers();
        if (findSetting.value) applyFind();
    } else if (name === "loadSettings") {
        if (Object.hasOwn(data, "tabs")) {
            TAB_SETTINGS = data.tabs;
            verticalTabsPosition.value = TAB_SETTINGS.verticalTabsPosition || "left";
            verticalTabsExpandOnHover.checked = !!TAB_SETTINGS.verticalTabsExpandOnHover;
            renderVerticalTabsSide();
        }
        // Ladybird's services.js has just set whether filter list updates are on.
        if (XENON) renderFilterLists();
    }
});

// FIXME: Remove this once closedby="any" triggers for Esc key.
for (const dialog of document.querySelectorAll("dialog")) {
    dialog.addEventListener("keydown", event => {
        if (event.key === "Escape") dialog.close();
    });
}

if (location.hash) scrollToHash({ smooth: false });
updateCurrentSection();
