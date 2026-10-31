/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

"use strict";

const { state } = window.xenonCertificateError;
const certificate = state.certificate;
const problems = certificate?.problems ?? [];

const TEST_MARKER = "XENON-CERTIFICATE-ERROR-TEST ";

const PROBLEM_LABELS = {
    selfSigned: "Self-signed",
    untrusted: "Not from a trusted authority",
    host: "For another host",
    expired: "Expired",
    notYetValid: "Not valid yet",
};

function formatDate(milliseconds) {
    const date = new Date(milliseconds);
    try {
        return date.toLocaleDateString(undefined, { year: "numeric", month: "long", day: "numeric" });
    } catch {
        return date.toISOString().slice(0, 10);
    }
}

function formatList(items) {
    if (items.length <= 1) return items.join("");
    return `${items.slice(0, -1).join(", ")} and ${items[items.length - 1]}`;
}

function reasonText() {
    switch (problems[0]) {
        case "selfSigned":
            return "Its certificate is self-signed: it vouches for itself, and no authority your computer trusts has confirmed it.";
        case "untrusted":
            return "Its certificate wasn’t issued by an authority your computer trusts.";
        case "host":
            if (certificate.names?.length)
                return `Its certificate is for ${formatList(certificate.names)}, not ${state.host}.`;
            return `Its certificate isn’t for ${state.host}.`;
        case "expired":
            return `Its certificate expired on ${formatDate(certificate.validUntil)}.`;
        case "notYetValid":
            return `Its certificate isn’t valid until ${formatDate(certificate.validFrom)}. Check that your computer’s date and time are right.`;
        default:
            return "Xenon couldn’t verify its certificate. Try again to see what’s wrong with it.";
    }
}

function textWithHost(before, after) {
    const host = document.createElement("strong");
    host.textContent = state.host;
    return [before, host, after];
}

function nameAndOrganization(name, organization) {
    if (name && organization && name !== organization) return `${name} (${organization})`;
    return name || organization || "Unknown";
}

function showCertificate() {
    document.getElementById("subject").textContent = nameAndOrganization(
        certificate.subject,
        certificate.subjectOrganization
    );
    document.getElementById("issuer").textContent = nameAndOrganization(
        certificate.issuer,
        certificate.issuerOrganization
    );

    const validity = [];
    if (certificate.validFrom !== undefined) validity.push(`from ${formatDate(certificate.validFrom)}`);
    if (certificate.validUntil !== undefined) validity.push(`until ${formatDate(certificate.validUntil)}`);
    document.getElementById("validity").textContent = validity.join(" ") || "Unknown";

    document.getElementById("names").textContent = certificate.names?.length ? certificate.names.join(", ") : "Unknown";
    document.getElementById("fingerprint").textContent = certificate.fingerprint || "Unknown";

    const list = document.getElementById("problems");
    for (const problem of problems) {
        const item = document.createElement("li");
        item.textContent = PROBLEM_LABELS[problem] ?? problem;
        list.append(item);
    }
}

const advanced = document.getElementById("advanced");
const details = document.getElementById("details");
const proceed = document.getElementById("proceed");
const retry = document.getElementById("retry");

document
    .getElementById("summary")
    .replaceChildren(
        ...textWithHost(
            "Xenon can’t confirm that this is really ",
            ". Someone could be trying to read or change what you send it, like passwords or messages."
        )
    );
document.getElementById("reason").textContent = reasonText();

if (certificate) {
    showCertificate();

    if (state.canProceed) {
        const until = state.private ? "until you close your private windows" : "until you quit Xenon";
        document
            .getElementById("explanation")
            .replaceChildren(
                ...textWithHost(
                    "Proceed only to a site you know, like a server of your own. Everything you do on ",
                    ` would go over a connection Xenon can’t vouch for. Xenon accepts this certificate, and only this one, ${until}.`
                )
            );
        proceed.textContent = `Proceed to ${state.host} (unsafe)`;
    } else {
        document
            .getElementById("proceed-note")
            .replaceChildren(
                ...textWithHost(
                    "",
                    " asks to be reached over a secure connection only (HSTS), so Xenon can’t let you proceed to it with this certificate."
                )
            );
        proceed.remove();
    }
} else {
    advanced.classList.add("hidden");
    details.remove();
    if (state.url) retry.classList.remove("hidden");
}

advanced.addEventListener("click", () => {
    const expanded = details.classList.toggle("hidden") === false;
    advanced.setAttribute("aria-expanded", String(expanded));
    if (expanded) details.scrollIntoView({ block: "nearest" });
});

proceed?.addEventListener("click", () => {
    window.xenonCertificateError.send({ name: "proceed" });
});

retry.addEventListener("click", () => {
    location.replace(state.url);
});

document.getElementById("back").addEventListener("click", () => {
    if (history.length > 1) history.back();
    else window.xenonCertificateError.send({ name: "leave" });
});

if (state.proceedsByItself) {
    console.log(
        TEST_MARKER +
            JSON.stringify({
                title: document.getElementById("title").textContent,
                reason: document.getElementById("reason").textContent,
                problems,
                canProceed: state.canProceed === true,
            })
    );
    if (state.canProceed) {
        advanced.click();
        proceed.click();
    } else {
        window.close();
    }
}
