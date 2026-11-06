/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Time.h>
#include <LibURL/InternalURLs.h>
#include <LibURL/Parser.h>
#include <LibURL/URL.h>
#include <LibWebView/Application.h>
#include <LibWebView/Autocomplete.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/HistoryVisitTransition.h>
#include <LibWebView/Omnibox.h>
#include <LibWebView/URL.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeLocation.h>
#include <UI/Bangs.h>
#include <UI/SiteSecurity.h>
#include <UI/XenonSettings.h>

namespace Xenon::Location {

static constexpr i64 no_index = -1;

static XenonLocationCallbacks s_callbacks {};

template<typename Callback, typename... Arguments>
static void notify(Callback XenonLocationCallbacks::* callback, Arguments... arguments)
{
    if (auto function = s_callbacks.*callback)
        function(s_callbacks.context, arguments...);
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

static StringView string_view_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return { string.data, string.length };
}

static String string_from(XenonStr string)
{
    return String::from_utf8_with_replacement_character(string_view_from(string));
}

static i64 index_or_none(Optional<size_t> index)
{
    return index.has_value() ? static_cast<i64>(*index) : no_index;
}

struct Session {
    AK_MAKE_NONCOPYABLE(Session);
    AK_MAKE_NONMOVABLE(Session);

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit Session(XenonViewId view_id, WebView::IsPrivate is_private)
        : view(view_id)
        , omnibox(is_private)
    {
    }

    XenonViewId view { 0 };
    WebView::Omnibox omnibox;
    u64 generation { 0 };
    String display_text;
    bool bang_dismissed { false };
    u32 submit_modifiers { 0 };
};

static HashMap<XenonViewId, NonnullOwnPtr<Session>>& sessions()
{
    static auto& sessions = *new HashMap<XenonViewId, NonnullOwnPtr<Session>>;
    return sessions;
}

static void forget_closed_views()
{
    sessions().remove_all_matching([](XenonViewId id, auto const&) {
        return !Engine::the().view(id);
    });
}

static bool bangs_apply(Session const& session)
{
    return !session.bang_dismissed && Ladybird::XenonSettings::bangs_enabled();
}

static void report_input(Session const& session)
{
    auto query = session.omnibox.is_editing() ? session.omnibox.query() : session.display_text;
    auto trimmed_query = query.bytes_as_string_view().trim_whitespace();

    XenonLocationInputKind kind = XENON_LOCATION_INPUT_OTHER;
    if (trimmed_query.is_empty())
        kind = XENON_LOCATION_INPUT_EMPTY;
    else if (WebView::location_looks_like_url(trimmed_query))
        kind = XENON_LOCATION_INPUT_URL;
    else if (WebView::Application::settings().search_engine_settings().engine.has_value())
        kind = XENON_LOCATION_INPUT_SEARCH;

    XenonLocationInput input { .kind = static_cast<u32>(kind), .bang_name = {}, .bang_length = 0 };
    if (bangs_apply(session)) {
        if (auto leading_bang = Ladybird::Bangs::leading_bang(session.display_text.bytes_as_string_view()); leading_bang.has_value()) {
            input.bang_name = xenon_str(leading_bang->bang.name);
            input.bang_length = leading_bang->length;
        }
    }
    notify(&XenonLocationCallbacks::input_changed, session.view, session.generation, static_cast<XenonLocationInput const*>(&input));
}

static XenonSuggestionKind suggestion_kind(WebView::AutocompleteSuggestionSource source)
{
    switch (source) {
    case WebView::AutocompleteSuggestionSource::LiteralURL:
        return XENON_SUGGESTION_URL;
    case WebView::AutocompleteSuggestionSource::WebUI:
        return XENON_SUGGESTION_INTERNAL_PAGE;
    case WebView::AutocompleteSuggestionSource::History:
        return XENON_SUGGESTION_HISTORY;
    case WebView::AutocompleteSuggestionSource::Bookmark:
        return XENON_SUGGESTION_BOOKMARK;
    case WebView::AutocompleteSuggestionSource::Adaptive:
        return XENON_SUGGESTION_ADAPTIVE;
    case WebView::AutocompleteSuggestionSource::Search:
        return XENON_SUGGESTION_SEARCH;
    case WebView::AutocompleteSuggestionSource::ChooseSearchEngine:
        return XENON_SUGGESTION_CHOOSE_SEARCH_ENGINE;
    }
    VERIFY_NOT_REACHED();
}

static bool shows_favicon(WebView::AutocompleteSuggestionSource source)
{
    return source == WebView::AutocompleteSuggestionSource::History
        || source == WebView::AutocompleteSuggestionSource::Bookmark
        || source == WebView::AutocompleteSuggestionSource::Adaptive;
}

struct SuggestionRow {
    String primary;
    Vector<XenonTextRange> primary_matches;
    String secondary;
    Vector<XenonTextRange> secondary_matches;
    bool emphasize_origin { false };
    Optional<ReadonlyBytes> favicon_png;
};

static Vector<XenonTextRange> match_ranges(StringView input, StringView text)
{
    Vector<XenonTextRange> ranges;
    for (auto const& range : WebView::autocomplete_match_ranges(input, text))
        ranges.append({ .start = range.start, .length = range.length });
    return ranges;
}

static SuggestionRow row_for(WebView::AutocompleteSuggestion const& suggestion)
{
    SuggestionRow row;
    auto display_text = WebView::autocomplete_suggestion_display_text(suggestion);
    auto has_title = suggestion.title.has_value() && !suggestion.title->is_empty();
    auto has_subtitle = suggestion.subtitle.has_value() && !suggestion.subtitle->is_empty();
    row.emphasize_origin = suggestion.source != WebView::AutocompleteSuggestionSource::Search && !has_subtitle;

    if (has_title) {
        row.primary = *suggestion.title;
        row.secondary = has_subtitle ? *suggestion.subtitle : display_text;
    } else {
        row.primary = display_text;
    }

    auto input = suggestion.highlight_input.bytes_as_string_view();
    row.primary_matches = match_ranges(input, row.primary);
    row.secondary_matches = match_ranges(input, row.secondary);

    if (shows_favicon(suggestion.source) && suggestion.favicon_png.has_value())
        row.favicon_png = suggestion.favicon_png->bytes();
    return row;
}

static void report_suggestions(Session const& session)
{
    auto const& suggestions = session.omnibox.suggestions();
    Vector<SuggestionRow> rows;
    rows.ensure_capacity(suggestions.size());
    for (auto const& suggestion : suggestions)
        rows.unchecked_append(row_for(suggestion));

    Vector<XenonLocationSuggestion> entries;
    entries.ensure_capacity(suggestions.size());
    for (size_t i = 0; i < suggestions.size(); ++i) {
        auto const& row = rows[i];
        entries.unchecked_append({
            .kind = static_cast<u32>(suggestion_kind(suggestions[i].source)),
            .text = xenon_str(suggestions[i].text),
            .primary = xenon_str(row.primary),
            .primary_matches = row.primary_matches.data(),
            .primary_match_count = row.primary_matches.size(),
            .secondary = xenon_str(row.secondary),
            .secondary_matches = row.secondary_matches.data(),
            .secondary_match_count = row.secondary_matches.size(),
            .emphasize_origin = row.emphasize_origin,
            .favicon_png = row.favicon_png.has_value() ? row.favicon_png->data() : nullptr,
            .favicon_png_length = row.favicon_png.has_value() ? row.favicon_png->size() : 0,
        });
    }

    notify(&XenonLocationCallbacks::suggestions_changed, session.view, session.generation, static_cast<XenonLocationSuggestion const*>(entries.data()), entries.size(), session.omnibox.is_popup_visible(), index_or_none(session.omnibox.selected_suggestion()));
}

static bool submits_with_top_level_domain(u32 modifiers)
{
#if defined(AK_OS_MACOS)
    return (modifiers & (XENON_MODIFIER_CTRL | XENON_MODIFIER_SUPER)) != 0;
#else
    return (modifiers & XENON_MODIFIER_CTRL) != 0;
#endif
}

static void commit(Session& session, String const& input)
{
    auto* view = Engine::the().view(session.view);
    if (!view || input.is_empty())
        return;

    view->set_next_history_visit_transition(WebView::HistoryVisitTransition::Omnibox);

    Optional<Ladybird::Bangs::BangNavigation> bang_navigation;
    if (bangs_apply(session))
        bang_navigation = Ladybird::Bangs::resolve(input);

    if (bang_navigation.has_value()) {
        view->load(bang_navigation->url);
    } else {
        auto append_tld = submits_with_top_level_domain(session.submit_modifiers) ? WebView::AppendTLD::Yes : WebView::AppendTLD::No;
        auto url = WebView::sanitize_url(input, WebView::Application::settings().search_engine_settings().engine, append_tld);
        if (session.omnibox.destination_kind_for_last_commit() == WebView::OmniboxDestinationKind::Search) {
            if (url.has_value())
                view->load(*url);
            else
                view->load_navigation_error_page(input);
        } else {
            view->load_from_user_input(input, move(url));
        }
    }

    session.submit_modifiers = 0;
    notify(&XenonLocationCallbacks::committed, session.view, xenon_str(input));
}

static Session* session_for(XenonViewId id)
{
    forget_closed_views();
    auto* view = Engine::the().view(id);
    if (!view)
        return nullptr;
    if (auto existing = sessions().get(id); existing.has_value())
        return existing.value();

    auto session = make<Session>(id, view->session().is_private());
    auto& omnibox = session->omnibox;
    auto* raw_session = session.ptr();

    omnibox.on_display_change = [raw_session](WebView::Omnibox::Display const& display) {
        raw_session->display_text = display.text;
        notify(&XenonLocationCallbacks::display_changed, raw_session->view, raw_session->generation, xenon_str(display.text), index_or_none(display.selection_start));
        report_input(*raw_session);
    };
    omnibox.on_suggestions_change = [raw_session] {
        report_suggestions(*raw_session);
    };
    omnibox.on_selection_change = [raw_session] {
        notify(&XenonLocationCallbacks::selection_changed, raw_session->view, raw_session->generation, index_or_none(raw_session->omnibox.selected_suggestion()));
    };
    omnibox.on_commit = [raw_session](String input) {
        commit(*raw_session, input);
    };

    sessions().set(id, move(session));
    return raw_session;
}

static void post_to_session(XenonViewId id, u64 generation, Function<void(Session&)> command)
{
    Engine::the().post([id, generation, command = move(command)] {
        if (auto* session = session_for(id)) {
            session->generation = generation;
            command(*session);
        }
    });
}

static bool should_hide_url(URL::URL const& url)
{
    if (url == URL::about_newtab())
        return true;
    return url.scheme() == "about"sv && url == WebView::Application::settings().new_tab_page_url();
}

static XenonSiteSecurityState security_state(View& view, URL::URL const& url)
{
    if (!Ladybird::SiteSecurity::applies_to(url))
        return XENON_SITE_SECURITY_NONE;
    if (url.scheme() == "http"sv)
        return XENON_SITE_SECURITY_NOT_SECURE;
    if (Ladybird::SiteSecurity::has_certificate_exception(view.session(), url))
        return XENON_SITE_SECURITY_ACCEPTED_CERTIFICATE;
    return XENON_SITE_SECURITY_SECURE;
}

static void describe_url(View& view, String const& url_string)
{
    auto url = URL::Parser::basic_parse(url_string);
    auto serialized = url.has_value() ? url->serialize() : url_string;
    auto parts = WebView::break_url_into_parts(serialized);

    String search_engine;
    if (auto const& engine = WebView::Application::settings().search_engine_settings().engine; engine.has_value())
        search_engine = engine->name;

    XenonLocationUrl description {
        .url = xenon_str(serialized),
        .scheme = url.has_value() ? xenon_str(url->scheme()) : XenonStr {},
        .has_parts = parts.has_value(),
        .scheme_and_subdomain = parts.has_value() ? xenon_str(parts->scheme_and_subdomain) : XenonStr {},
        .registrable_domain = parts.has_value() ? xenon_str(parts->effective_tld_plus_one) : XenonStr {},
        .remainder = parts.has_value() ? xenon_str(parts->remainder) : XenonStr {},
        .hidden = url.has_value() && should_hide_url(*url),
        .security = static_cast<u32>(url.has_value() ? security_state(view, *url) : XENON_SITE_SECURITY_NONE),
        .search_engine = xenon_str(search_engine),
    };
    notify(&XenonLocationCallbacks::url_described, view.id(), static_cast<XenonLocationUrl const*>(&description));
}

static String string_from_json(JsonObject const& object, StringView key)
{
    return object.get_string(key).value_or({});
}

static String name_with_organization(JsonObject const& certificate, StringView name_key, StringView organization_key)
{
    auto name = string_from_json(certificate, name_key);
    auto organization = string_from_json(certificate, organization_key);
    if (name.is_empty())
        return organization;
    if (organization.is_empty() || organization == name)
        return name;
    return MUST(String::formatted("{}\n{}", name, organization));
}

static String certificate_date(Optional<i64> milliseconds_since_epoch)
{
    if (!milliseconds_since_epoch.has_value())
        return {};
    auto date = UnixDateTime::from_milliseconds_since_epoch(*milliseconds_since_epoch).to_byte_string("%d %B %Y"sv, UnixDateTime::LocalTime::No);
    auto trimmed = date.view().trim("0"sv, TrimMode::Left);
    return String::from_utf8_with_replacement_character(trimmed);
}

static String fingerprint_in_lines(String const& fingerprint)
{
    static constexpr size_t bytes_per_line = 8;
    auto bytes = fingerprint.bytes_as_string_view().split_view(':');
    StringBuilder builder;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0)
            builder.append(i % bytes_per_line == 0 ? '\n' : ':');
        builder.append(bytes[i]);
    }
    return builder.to_string_without_validation();
}

static XenonSiteSecurityState security_state_from(StringView state)
{
    if (state == "secure"sv)
        return XENON_SITE_SECURITY_SECURE;
    if (state == "acceptedCertificate"sv)
        return XENON_SITE_SECURITY_ACCEPTED_CERTIFICATE;
    if (state == "notSecure"sv)
        return XENON_SITE_SECURITY_NOT_SECURE;
    return XENON_SITE_SECURITY_NONE;
}

static void report_site_security(XenonViewId view_id, JsonObject const& description, bool is_final)
{
    auto host = string_from_json(description, "host"sv);
    auto protocol = string_from_json(description, "protocol"sv);
    auto cipher = string_from_json(description, "cipher"sv);
    auto state = security_state_from(string_from_json(description, "state"sv));

    auto certificate = description.get_object("certificate"sv);
    String issued_to;
    String issued_by;
    String valid_from;
    String valid_until;
    String fingerprint;
    if (certificate.has_value()) {
        issued_to = name_with_organization(*certificate, "subject"sv, "subjectOrganization"sv);
        issued_by = name_with_organization(*certificate, "issuer"sv, "issuerOrganization"sv);
        valid_from = certificate_date(certificate->get_i64("validFrom"sv));
        valid_until = certificate_date(certificate->get_i64("validUntil"sv));
        fingerprint = fingerprint_in_lines(string_from_json(*certificate, "fingerprint"sv));
    }

    Vector<String> chain_names;
    if (auto chain = description.get_array("chain"sv); chain.has_value()) {
        for (size_t i = chain->size(); i-- > 0;) {
            if (auto const& name = chain->at(i); name.is_string())
                chain_names.append(name.as_string());
        }
    }
    Vector<XenonStr> chain;
    for (auto const& name : chain_names)
        chain.append(xenon_str(name));

    XenonSiteSecurity security {
        .state = static_cast<u32>(state),
        .is_final = is_final,
        .has_connection = description.has("protocol"sv),
        .host = xenon_str(host),
        .protocol = xenon_str(protocol),
        .cipher = xenon_str(cipher),
        .has_certificate = certificate.has_value(),
        .issued_to = xenon_str(issued_to),
        .issued_by = xenon_str(issued_by),
        .valid_from = xenon_str(valid_from),
        .valid_until = xenon_str(valid_until),
        .fingerprint = xenon_str(fingerprint),
        .chain = chain.data(),
        .chain_length = chain.size(),
    };
    notify(&XenonLocationCallbacks::site_security, view_id, static_cast<XenonSiteSecurity const*>(&security));
}

static void request_site_security(View& view)
{
    auto url = view.url();
    auto view_id = view.id();
    auto has_exception = Ladybird::SiteSecurity::has_certificate_exception(view.session(), url);
    auto description = Ladybird::SiteSecurity::describe(url, {}, has_exception);
    if (!Ladybird::SiteSecurity::applies_to(url) || url.scheme() != "https"sv) {
        report_site_security(view_id, description, true);
        return;
    }
    report_site_security(view_id, description, false);
    Ladybird::SiteSecurity::request_description(view, [view_id](JsonObject description) {
        report_site_security(view_id, description, true);
    });
}

}

using namespace Xenon::Location;

void xenon_engine_set_location_callbacks(XenonLocationCallbacks const* callbacks)
{
    s_callbacks = callbacks ? *callbacks : XenonLocationCallbacks {};
}

void xenon_engine_location_describe_url(XenonViewId id, XenonStr url)
{
    Xenon::Engine::the().post([id, url = string_from(url)] {
        if (auto* view = Xenon::Engine::the().view(id))
            describe_url(*view, url);
    });
}

void xenon_engine_location_begin_editing(XenonViewId id, uint64_t generation, XenonStr text)
{
    post_to_session(id, generation, [text = string_from(text)](Session& session) {
        session.bang_dismissed = false;
        session.display_text = text;
        session.omnibox.begin_editing(text);
        report_input(session);
    });
}

void xenon_engine_location_end_editing(XenonViewId id, uint64_t generation)
{
    post_to_session(id, generation, [](Session& session) {
        session.omnibox.end_editing();
        session.bang_dismissed = false;
    });
}

void xenon_engine_location_text_edited(XenonViewId id, uint64_t generation, XenonStr text, bool cursor_at_end)
{
    post_to_session(id, generation, [text = string_from(text), cursor_at_end](Session& session) {
        session.bang_dismissed = false;
        session.display_text = text;
        session.omnibox.text_edited(text, cursor_at_end);
        report_input(session);
    });
}

void xenon_engine_location_cursor_moved(XenonViewId id, uint64_t generation, bool cursor_at_end)
{
    post_to_session(id, generation, [cursor_at_end](Session& session) {
        session.omnibox.cursor_moved(cursor_at_end);
    });
}

void xenon_engine_location_command(XenonViewId id, uint64_t generation, uint32_t command)
{
    post_to_session(id, generation, [command](Session& session) {
        switch (command) {
        case XENON_LOCATION_SELECT_NEXT:
            session.omnibox.select_next_suggestion();
            break;
        case XENON_LOCATION_SELECT_PREVIOUS:
            session.omnibox.select_previous_suggestion();
            break;
        case XENON_LOCATION_ACCEPT_COMPLETION:
            session.omnibox.accept_completion();
            break;
        case XENON_LOCATION_ESCAPE:
            session.omnibox.escape_pressed();
            break;
        case XENON_LOCATION_DISMISS_POPUP:
            session.omnibox.popup_dismissed();
            break;
        case XENON_LOCATION_DISMISS_BANG:
            session.bang_dismissed = true;
            report_input(session);
            break;
        case XENON_LOCATION_WILL_DELETE:
            session.omnibox.will_delete_text();
            break;
        default:
            break;
        }
    });
}

void xenon_engine_location_submit(XenonViewId id, uint64_t generation, uint32_t modifiers)
{
    post_to_session(id, generation, [modifiers](Session& session) {
        session.submit_modifiers = modifiers;
        session.omnibox.return_pressed();
        session.submit_modifiers = 0;
    });
}

void xenon_engine_location_activate_suggestion(XenonViewId id, uint64_t generation, size_t index)
{
    post_to_session(id, generation, [index](Session& session) {
        session.omnibox.suggestion_clicked(index);
    });
}

void xenon_engine_location_request_site_security(XenonViewId id)
{
    Xenon::Engine::the().post([id] {
        if (auto* view = Xenon::Engine::the().view(id))
            request_site_security(*view);
    });
}
