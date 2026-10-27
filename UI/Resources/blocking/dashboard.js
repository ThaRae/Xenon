/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

"use strict";

// The dashboard talks to Xenon through window.xenonBlocking (UI/BlockingDashboard.cpp), which answers every message
// with the whole state as a xenonblocking event.

const GROUPS = [
    { id: "default", title: "Built-in", description: "uBlock Origin's own filters." },
    { id: "ads", title: "Ads" },
    { id: "privacy", title: "Privacy" },
    { id: "malware", title: "Malware domains" },
    { id: "annoyances", title: "Annoyances", description: "Cookie notices, newsletter pop-ups, and other clutter." },
];

const PANES = ["lists", "filters", "trusted"];

let state = null;
let customFiltersSent = null;

const send = message => window.xenonBlocking.send(message);
const count = number => Number(number).toLocaleString();
const element = id => document.getElementById(id);

function make(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
}

function showPane(pane) {
    if (!PANES.includes(pane)) pane = PANES[0];
    for (const tab of document.querySelectorAll(".tabs [role=tab]")) {
        tab.setAttribute("aria-selected", String(tab.dataset.pane === pane));
    }
    for (const id of PANES) {
        element(id).classList.toggle("hidden", id !== pane);
    }
}

function renderSummary() {
    const total = state.blockedTotal;
    element("summary").textContent = state.enabled
        ? `${count(total)} request${total === 1 ? "" : "s"} blocked since install.`
        : "Blocking is off for every site.";
    element("enabled").checked = state.enabled;
    element("enabled-label").textContent = state.enabled ? "On" : "Off";
}

function renderLists() {
    const items = state.lists.items;
    const enabled = items.filter(item => item.enabled);
    const filters = enabled.reduce((sum, item) => sum + (item.filters ?? 0), 0);

    let summary = `${count(filters)} filters from ${enabled.length} of ${items.length} lists in use.`;
    if (state.lists.generatedAt) {
        summary += ` ${state.lists.downloaded ? "Updated" : "Built"} ${state.lists.generatedAt}.`;
    }
    element("lists-summary").textContent = summary;

    const container = element("list-groups");
    container.replaceChildren();
    for (const group of GROUPS) {
        const lists = items.filter(item => item.group === group.id);
        if (lists.length === 0) continue;

        const title = make("div", "card-title", group.title);
        if (group.description) title.append(make("span", "card-subtitle", group.description));
        container.append(title);

        const card = make("div", "card");
        const body = make("div", "card-body list-body");
        for (const list of lists) {
            const row = make("label", "list-row");
            const text = make("div", "list-text");
            text.append(make("span", "list-title", list.title));

            const meta = make("span", "list-meta");
            const parts = [];
            if (list.filters !== undefined) parts.push(`${count(list.filters)} filters`);
            if (list.license) parts.push(list.license);
            meta.textContent = parts.join(" · ");
            if (list.homepage) {
                const link = make("a", "list-link", "Source");
                link.href = list.homepage;
                link.target = "_blank";
                link.rel = "noopener";
                meta.append(link);
            }
            text.append(meta);
            row.append(text);

            const toggle = document.createElement("input");
            toggle.type = "checkbox";
            toggle.setAttribute("switch", "");
            toggle.checked = list.enabled;
            toggle.setAttribute("aria-label", `Use ${list.title}`);
            toggle.addEventListener("change", () =>
                send({ name: "setListEnabled", id: list.id, enabled: toggle.checked })
            );
            row.append(toggle);
            body.append(row);
        }
        card.append(body);
        container.append(card);
    }
}

function renderCustomFilters() {
    const textarea = element("custom-filters");
    // Keep what's being typed. The text is replaced only when it hasn't been changed since it was last shown or applied.
    if (customFiltersSent === null || textarea.value === customFiltersSent) {
        textarea.value = state.customFilters;
        customFiltersSent = state.customFilters;
    }
    updateFilterButtons();
}

function updateFilterButtons() {
    const changed = element("custom-filters").value !== state.customFilters;
    element("apply-filters").disabled = !changed;
    element("revert-filters").disabled = !changed;
}

function renderTrustedSites() {
    const list = element("trusted-sites");
    list.replaceChildren();
    for (const site of state.trustedSites) {
        const item = make("li", "trusted-site");
        item.append(make("span", "trusted-site-name", site));
        const remove = make("button", "secondary-button", "Remove");
        remove.type = "button";
        remove.setAttribute("aria-label", `Turn blocking back on for ${site}`);
        remove.addEventListener("click", () => send({ name: "setSiteTrusted", site, trusted: false }));
        item.append(remove);
        list.append(item);
    }
    element("no-trusted-sites").classList.toggle("hidden", state.trustedSites.length > 0);
}

function render(newState) {
    state = newState;
    renderSummary();
    renderLists();
    renderCustomFilters();
    renderTrustedSites();
}

document.addEventListener("DOMContentLoaded", () => {
    for (const tab of document.querySelectorAll(".tabs [role=tab]")) {
        tab.addEventListener("click", () => {
            history.replaceState(null, "", `#${tab.dataset.pane}`);
            showPane(tab.dataset.pane);
        });
    }
    showPane(location.hash.slice(1));
    addEventListener("hashchange", () => showPane(location.hash.slice(1)));

    element("enabled").addEventListener("change", event => send({ name: "setEnabled", enabled: event.target.checked }));

    const textarea = element("custom-filters");
    textarea.addEventListener("input", () => state && updateFilterButtons());
    element("apply-filters").addEventListener("click", () => {
        customFiltersSent = textarea.value;
        send({ name: "setCustomFilters", filters: textarea.value });
    });
    element("revert-filters").addEventListener("click", () => {
        textarea.value = state.customFilters;
        customFiltersSent = state.customFilters;
        updateFilterButtons();
    });

    element("add-trusted").addEventListener("submit", event => {
        event.preventDefault();
        const input = element("trusted-site");
        let site = input.value.trim();
        // A pasted address stands for its host.
        if (site.includes("://")) {
            try {
                site = new URL(site).hostname;
            } catch {}
        }
        if (!site) return;
        send({ name: "setSiteTrusted", site, trusted: true });
        input.value = "";
    });

    addEventListener("xenonblocking", event => {
        if (event.detail?.name === "state") render(event.detail);
    });
    send({ name: "load" });
});
