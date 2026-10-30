/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

const DIAL_SIZE = 8;

const dialGroups = {
    sites: { label: "Most visited", note: "Busiest first, by how often you visit" },
    bookmarksBar: { label: "Bookmarks bar", note: "In the bookmarks bar's order" },
};

let shown;
let currentGroup = "sites";

window.xenonNewTab = {
    show(data) {
        shown = data ?? {};
        const isPrivate = Boolean(shown.isPrivate);

        document.body.classList.toggle("private", isPrivate);
        document.querySelector("#private-window").hidden = !isPrivate;
        document.querySelector("#everyday").hidden = isPrivate;
        document.querySelector("#customize").hidden = isPrivate;
        if (isPrivate) return;

        document.querySelector("#greeting").textContent = greeting(new Date());

        const bookmarksBarKey = document.querySelector('[data-group="bookmarksBar"]');
        bookmarksBarKey.hidden = !(shown.bookmarksBar?.length > 0);
        if (bookmarksBarKey.hidden && currentGroup === "bookmarksBar") currentGroup = "sites";

        renderDial();
        renderRecentlyClosed(shown.recentlyClosed ?? []);
    },
};

function greeting(now) {
    const hour = now.getHours();
    if (hour < 5) return "Good evening.";
    if (hour < 12) return "Good morning.";
    if (hour < 18) return "Good afternoon.";
    return "Good evening.";
}

function shortHost(host) {
    return host.replace(/^www\./, "");
}

function siteName(host) {
    const labels = host.split(".");
    if (labels.length < 2) return host;
    const shortSuffix = labels.length > 2 && labels.at(-1).length === 2 && labels.at(-2).length <= 3;
    return labels.at(shortSuffix ? -3 : -2);
}

function monogram(site, className) {
    const element = document.createElement("span");
    element.className = className;
    element.setAttribute("aria-hidden", "true");
    if (site.icon) {
        const image = document.createElement("img");
        image.src = site.icon;
        image.alt = "";
        element.append(image);
    } else {
        element.textContent = siteName(site.host).charAt(0);
    }
    return element;
}

function siteTile(site, index) {
    const item = document.createElement("li");
    item.className = "tile";

    const link = document.createElement("a");
    link.href = site.url;
    link.title = site.title ? `${site.title}\n${site.url}` : site.url;

    const face = monogram(site, "tile-face");
    const legend = document.createElement("span");
    legend.className = "legend";
    legend.textContent = index + 1;
    face.prepend(legend);

    const name = document.createElement("span");
    name.className = "tile-name";
    name.textContent = shortHost(site.host);

    const detail = document.createElement("span");
    detail.className = "tile-detail";
    detail.textContent = site.title || site.host;

    link.append(face, name, detail);
    item.append(link);
    return item;
}

function emptyTile(index) {
    const item = document.createElement("li");
    item.className = "tile";
    const face = document.createElement("span");
    face.className = "tile-face";
    const legend = document.createElement("span");
    legend.className = "legend";
    legend.textContent = index + 1;
    face.append(legend);
    item.append(face);
    return item;
}

function renderDial() {
    for (const key of document.querySelectorAll("#dial-groups [data-group]")) {
        key.setAttribute("aria-selected", String(key.dataset.group === currentGroup));
    }

    const group = dialGroups[currentGroup];
    const sites = (shown[currentGroup] ?? []).slice(0, DIAL_SIZE);
    const dial = document.querySelector("#dial");
    dial.setAttribute("aria-label", group.label);
    document.querySelector("#dial-note").textContent = sites.length ? group.note : "";

    const empty = sites.length === 0 && currentGroup === "sites";
    dial.classList.toggle("empty", empty);
    dial.replaceChildren(...(empty ? [...Array(DIAL_SIZE).keys()].map(emptyTile) : sites.map(siteTile)));
    document.querySelector("#no-sites").hidden = !empty;
}

function closedWhen(closedAt) {
    const minutes = Math.max(0, Math.round((Date.now() - closedAt) / 60000));
    if (minutes < 1) return "Just now";
    if (minutes < 60) return `${minutes} min ago`;
    const hours = Math.round(minutes / 60);
    if (hours < 24) return `${hours} hr ago`;
    const days = Math.round(hours / 24);
    return days === 1 ? "Yesterday" : `${days} days ago`;
}

function renderRecentlyClosed(closed) {
    const list = document.querySelector("#recently-closed-list");
    list.replaceChildren(
        ...closed.map(tab => {
            const item = document.createElement("li");
            const link = document.createElement("a");
            link.href = tab.url;
            link.title = tab.url;

            const title = document.createElement("span");
            title.className = "closed-title";
            title.textContent = tab.title || tab.url;
            if (tab.tabCount > 1) title.textContent += ` and ${tab.tabCount - 1} more tabs`;

            const host = document.createElement("span");
            host.className = "closed-host";
            host.textContent = shortHost(tab.host);

            const when = document.createElement("span");
            when.className = "closed-when";
            when.textContent = closedWhen(tab.closedAt);

            link.append(monogram(tab, "monogram soft"), title, host, when);
            item.append(link);
            return item;
        })
    );
    document.querySelector("#recently-closed").hidden = closed.length === 0;
}

document.addEventListener("DOMContentLoaded", () => {
    if (!navigator.platform.startsWith("Mac")) {
        for (const shortcut of document.querySelectorAll("kbd[data-other]"))
            shortcut.textContent = shortcut.dataset.other;
    }

    for (const key of document.querySelectorAll("#dial-groups [data-group]")) {
        key.addEventListener("click", () => {
            currentGroup = key.dataset.group;
            renderDial();
        });
    }

    setTimeout(() => {
        if (!shown) window.xenonNewTab.show({});
    }, 1500);
});
