/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

const DIAL_SIZE = 8;

let shown;

window.xenonNewTab = {
    show(data) {
        shown = data ?? {};
        const isPrivate = Boolean(shown.isPrivate);

        document.body.classList.toggle("private", isPrivate);
        document.querySelector("#private-window").hidden = !isPrivate;
        document.querySelector("#everyday").hidden = isPrivate;
        document.querySelector("#customize").hidden = isPrivate;
        if (isPrivate) return;

        renderDial();
        renderRecentlyClosed(shown.recentlyClosed ?? []);
    },
};

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

function siteTile(site) {
    const item = document.createElement("li");
    item.className = "tile";

    const link = document.createElement("a");
    link.href = site.url;
    link.title = site.title ? `${site.title}\n${site.url}` : site.url;

    const name = document.createElement("span");
    name.className = "tile-name";
    name.textContent = site.title || shortHost(site.host);

    link.append(monogram(site, "tile-face"), name);
    item.append(link);
    return item;
}

function renderDial() {
    const mostVisited = shown.sites ?? [];
    const sites = (mostVisited.length ? mostVisited : (shown.bookmarksBar ?? [])).slice(0, DIAL_SIZE);
    const dial = document.querySelector("#dial");
    dial.setAttribute("aria-label", mostVisited.length ? "Most visited" : "Bookmarks bar");
    dial.replaceChildren(...sites.map(siteTile));
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

            const when = document.createElement("span");
            when.className = "closed-when";
            when.textContent = closedWhen(tab.closedAt);

            link.append(monogram(tab, "closed-icon"), title, when);
            item.append(link);
            return item;
        })
    );
    document.querySelector("#recently-closed").hidden = closed.length === 0;
}

document.addEventListener("DOMContentLoaded", () => {
    setTimeout(() => {
        if (!shown) window.xenonNewTab.show({});
    }, 1500);
});
