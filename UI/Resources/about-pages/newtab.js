/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// The new tab page has no channel to the browser of its own, so once it has loaded, Xenon's UI calls show() with what
// to display (UI/NewTabPage.cpp). Site titles come from the pages themselves, so everything goes in as text.
window.xenonNewTab = {
    show({ sites = [], isPrivate = false }) {
        const list = document.querySelector("#top-sites");
        list.replaceChildren(...sites.map(siteTile));

        document.querySelector("#private-window").hidden = !isPrivate;
        document.querySelector("#no-sites").hidden = isPrivate || sites.length > 0;
    },
};

function siteTile(site) {
    const item = document.createElement("li");
    item.className = "site";

    const link = document.createElement("a");
    link.href = site.url;
    link.title = site.title ? `${site.title}\n${site.url}` : site.url;

    const icon = document.createElement("span");
    icon.className = "site-icon";
    if (site.icon) {
        const image = document.createElement("img");
        image.src = site.icon;
        image.alt = "";
        icon.append(image);
    } else {
        icon.textContent = site.host.replace(/^www\./, "").charAt(0);
    }

    const name = document.createElement("span");
    name.className = "site-name";
    name.textContent = site.host.replace(/^www\./, "");

    link.append(icon, name);
    item.append(link);
    return item;
}
