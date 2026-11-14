// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/quick_nav.hpp.
#include "alus/i18n/i18n.hpp"
#include "alus/nav/quick_nav.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"
#include "alus/nav/document_range.hpp"

#include <algorithm>
#include <array>

namespace alus::nav {
namespace {

using a11y::MatchRule;
using a11y::MatchType;
using a11y::PlatformRole;
using a11y::PlatformState;

std::uint64_t stateBit(PlatformState state) {
    return 1ULL << static_cast<unsigned>(state);
}

// A rule that matches any of these roles and nothing else. The shape of
// sixteen of the twenty-two rows.
MatchRule byRoles(std::vector<PlatformRole> roles) {
    MatchRule rule;
    rule.roles = std::move(roles);
    rule.roleMatch = MatchType::Any;
    return rule;
}

// A rule that matches an attribute whose value is one of several
// alternatives, which AT-SPI spells with ':' separators.
std::string alternatives(const std::vector<std::string>& values) {
    std::string out;
    for (const std::string& value : values) {
        if (!out.empty()) {
            out += ':';
        }
        out += value;
    }
    return out;
}

struct Row {
    ElementType type;
    const char* scriptName;
    const char* label;
};

// The order is the order of the elements list. The script names are the ones
// `data/scripts.csv` already carries (`nextHeading`, `previousCheckBox`), so
// nothing here is retyped from NVDA -- §5.4's rule, applied to phase 6.
constexpr std::array<Row, 37> kRows{{
    {ElementType::Heading, "Heading", N_("heading")},
    {ElementType::Heading1, "Heading1", N_("heading at level 1")},
    {ElementType::Heading2, "Heading2", N_("heading at level 2")},
    {ElementType::Heading3, "Heading3", N_("heading at level 3")},
    {ElementType::Heading4, "Heading4", N_("heading at level 4")},
    {ElementType::Heading5, "Heading5", N_("heading at level 5")},
    {ElementType::Heading6, "Heading6", N_("heading at level 6")},
    {ElementType::Heading7, "Heading7", N_("heading at level 7")},
    {ElementType::Heading8, "Heading8", N_("heading at level 8")},
    {ElementType::Heading9, "Heading9", N_("heading at level 9")},
    {ElementType::Link, "Link", N_("link")},
    {ElementType::VisitedLink, "VisitedLink", N_("visited link")},
    {ElementType::UnvisitedLink, "UnvisitedLink", N_("unvisited link")},
    {ElementType::FormField, "FormField", N_("form field")},
    {ElementType::ListItem, "ListItem", N_("list item")},
    {ElementType::List, "List", N_("list")},
    {ElementType::Table, "Table", N_("table")},
    {ElementType::Button, "Button", N_("button")},
    {ElementType::CheckBox, "CheckBox", N_("check box")},
    {ElementType::RadioButton, "RadioButton", N_("radio button")},
    {ElementType::ComboBox, "ComboBox", N_("combo box")},
    {ElementType::Edit, "Edit", N_("edit field")},
    {ElementType::Graphic, "Graphic", N_("graphic")},
    {ElementType::BlockQuote, "BlockQuote", N_("block quote")},
    {ElementType::Separator, "Separator", N_("separator")},
    {ElementType::Frame, "Frame", N_("frame")},
    {ElementType::Landmark, "Landmark", N_("landmark")},
    {ElementType::EmbeddedObject, "EmbeddedObject", N_("embedded object")},
    {ElementType::Annotation, "Annotation", N_("annotation")},
    {ElementType::Error, "Error", N_("error")},
    {ElementType::Paragraph, "Paragraph", N_("text paragraph")},
    {ElementType::Article, "Article", N_("article")},
    {ElementType::Grouping, "Grouping", N_("grouping")},
    {ElementType::Figure, "Figure", N_("figure")},
    {ElementType::Clickable, "Clickable", N_("clickable element")},
    {ElementType::Tab, "Tab", N_("tab")},
    {ElementType::NotLinkBlock, "NotLinkBlock", N_("more text")},
}};

const Row* rowFor(ElementType type) {
    for (const Row& row : kRows) {
        if (row.type == type) {
            return &row;
        }
    }
    return nullptr;
}

// A heading at a level. AT-SPI exposes the level as the `level` attribute, so
// the rule is "role is HEADING and level is N" -- both criteria, hence ALL on
// the attributes and ANY on the single role.
MatchRule headingAtLevel(int level) {
    MatchRule rule = byRoles({PlatformRole::Heading});
    rule.attributes["level"] = std::to_string(level);
    rule.attributeMatch = MatchType::All;
    return rule;
}

}  // namespace

const std::vector<std::string>& ariaLandmarkTokens() {
    // WAI-ARIA 1.2's landmark roles, plus `region`, which is a landmark only
    // when it has a name -- the naming condition is applied by the caller,
    // because a match rule cannot express "name != ''".
    static const std::vector<std::string> tokens{
        "banner", "complementary", "contentinfo", "form",   "main",
        "navigation", "region",     "search",      "doc-acknowledgments",
        "doc-afterword", "doc-appendix", "doc-bibliography", "doc-chapter",
        "doc-conclusion", "doc-credits", "doc-endnotes", "doc-epilogue",
        "doc-errata", "doc-foreword", "doc-glossary", "doc-index",
        "doc-introduction", "doc-part", "doc-preface", "doc-prologue",
        "doc-toc",
    };
    return tokens;
}

std::string_view elementTypeScriptName(ElementType type) {
    const Row* row = rowFor(type);
    return row != nullptr ? row->scriptName : "";
}

// Translated here; the table is constexpr, so it carries the msgid. These
// labels are what "no next heading" and the elements list are built from, so
// they are user-visible text and not identifiers -- the identifier is the
// second column (`scriptName`) and is never translated.
std::string_view elementTypeLabel(ElementType type) {
    const Row* row = rowFor(type);
    return row != nullptr ? i18n::translate(row->label) : "";
}

std::string_view elementTypeLabelUntranslated(ElementType type) {
    const Row* row = rowFor(type);
    return row != nullptr ? row->label : "";
}

const std::vector<ElementType>& allElementTypes() {
    static const std::vector<ElementType> types = [] {
        std::vector<ElementType> out;
        out.reserve(kRows.size());
        for (const Row& row : kRows) {
            out.push_back(row.type);
        }
        return out;
    }();
    return types;
}

std::optional<ElementType> parseElementType(std::string_view scriptName) {
    for (const Row& row : kRows) {
        if (scriptName == row.scriptName) {
            return row.type;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The table -- §9.2, row for row
// ---------------------------------------------------------------------------

std::optional<MatchRule> QuickNav::ruleFor(ElementType type, bool includeLayoutTables) {
    switch (type) {
        // h
        case ElementType::Heading:
            return byRoles({PlatformRole::Heading});
        // 1-9
        case ElementType::Heading1:
            return headingAtLevel(1);
        case ElementType::Heading2:
            return headingAtLevel(2);
        case ElementType::Heading3:
            return headingAtLevel(3);
        case ElementType::Heading4:
            return headingAtLevel(4);
        case ElementType::Heading5:
            return headingAtLevel(5);
        case ElementType::Heading6:
            return headingAtLevel(6);
        case ElementType::Heading7:
            return headingAtLevel(7);
        case ElementType::Heading8:
            return headingAtLevel(8);
        case ElementType::Heading9:
            return headingAtLevel(9);

        // k
        case ElementType::Link:
            return byRoles({PlatformRole::Link});
        // v -- a link the user has followed
        case ElementType::VisitedLink: {
            MatchRule rule = byRoles({PlatformRole::Link});
            rule.states = stateBit(PlatformState::Visited);
            rule.stateMatch = MatchType::All;
            return rule;
        }
        // u -- a link the user has not followed. NONE over the same bit, which
        // is AT-SPI's own negation and is why no second query is needed.
        case ElementType::UnvisitedLink: {
            MatchRule rule = byRoles({PlatformRole::Link});
            rule.states = stateBit(PlatformState::Visited);
            rule.stateMatch = MatchType::None;
            return rule;
        }

        // f -- a form field: a widget the user can operate. READ_ONLY is
        // excluded because a disabled field is not a field to jump to, and a
        // static combo box in a toolbar is not part of a form.
        case ElementType::FormField: {
            MatchRule rule = byRoles({
                PlatformRole::Entry,
                PlatformRole::ComboBox,
                PlatformRole::CheckBox,
                PlatformRole::RadioButton,
                PlatformRole::Button,
                PlatformRole::PushButtonMenu,
                PlatformRole::ToggleButton,
                PlatformRole::Switch,
                PlatformRole::ListBox,
                PlatformRole::Slider,
                PlatformRole::SpinButton,
                PlatformRole::PasswordText,
            });
            rule.states = stateBit(PlatformState::ReadOnly);
            rule.stateMatch = MatchType::None;
            return rule;
        }

        // i / l
        case ElementType::ListItem:
            return byRoles({PlatformRole::ListItem});
        case ElementType::List:
            return byRoles({PlatformRole::List});

        // t -- a table. A LAYOUT TABLE IS NOT ONE unless the user asked for
        // them: a page that uses a table for positioning has no rows worth
        // announcing, and jumping to it wastes the key.
        case ElementType::Table: {
            MatchRule rule = byRoles({PlatformRole::Table});
            if (!includeLayoutTables) {
                rule.attributes["layout-guess"] = "true";
                rule.attributeMatch = MatchType::None;
            }
            return rule;
        }

        // b
        case ElementType::Button:
            return byRoles({PlatformRole::Button, PlatformRole::PushButtonMenu,
                            PlatformRole::ToggleButton});
        // x -- a check box, or an ARIA switch, which Chrome maps to CHECK_BOX
        // and Firefox to its own SWITCH role.
        case ElementType::CheckBox:
            return byRoles({PlatformRole::CheckBox, PlatformRole::Switch});
        // r
        case ElementType::RadioButton:
            return byRoles({PlatformRole::RadioButton});
        // c
        case ElementType::ComboBox:
            return byRoles({PlatformRole::ComboBox});
        // e -- a field the user can type into. AN EDITABLE COMBO BOX IS ONE
        // (N130): the YouTube search field, and every search field with
        // suggestions, is ARIA `combobox` over a text input, which Chromium
        // exposes as COMBO_BOX with EDITABLE -- and `e` said "no next edit
        // field" with the search box right there. NVDA's own rule includes
        // editable combo boxes for the same reason. EDITABLE (ALL) instead of
        // "not READ_ONLY" (NONE), because it is the one criterion that both
        // takes the combo box in and keeps a read-only entry out; a
        // non-editable combo box (a plain drop-down) stays `c`'s.
        case ElementType::Edit: {
            MatchRule rule = byRoles(
                {PlatformRole::Entry, PlatformRole::PasswordText, PlatformRole::ComboBox});
            rule.states = stateBit(PlatformState::Editable);
            rule.stateMatch = MatchType::All;
            return rule;
        }
        // g
        case ElementType::Graphic:
            return byRoles({PlatformRole::Image, PlatformRole::ImageMap});
        // q
        case ElementType::BlockQuote:
            return byRoles({PlatformRole::BlockQuote});
        // s
        case ElementType::Separator:
            return byRoles({PlatformRole::Separator});
        // m
        case ElementType::Frame:
            return byRoles({PlatformRole::InternalFrame});

        // d -- a landmark. Two ways to be one: the role, or an ARIA token in
        // `xml-roles`. ANY over both criteria, which is exactly §9.2's
        // "role ILI ARIA token".
        case ElementType::Landmark: {
            MatchRule rule = byRoles({PlatformRole::Landmark});
            rule.attributes["xml-roles"] = alternatives(ariaLandmarkTokens());
            rule.attributeMatch = MatchType::Any;
            // Either criterion is enough, so the roles criterion must not be
            // mandatory: ANY on both, and AT-SPI ORs the criteria that are ANY.
            rule.roleMatch = MatchType::Any;
            return rule;
        }

        // o -- an embedded object: a plugin, a media element, a figure.
        case ElementType::EmbeddedObject: {
            MatchRule rule = byRoles({PlatformRole::Application, PlatformRole::Dialog,
                                      PlatformRole::Embedded, PlatformRole::Video,
                                      PlatformRole::Audio});
            rule.attributes["tag"] =
                alternatives({"embed", "object", "applet", "audio", "video", "figure"});
            rule.attributeMatch = MatchType::Any;
            rule.roleMatch = MatchType::Any;
            return rule;
        }

        // a -- an annotation: a comment, or something with a DETAILS relation.
        // The relation cannot be expressed as a match rule, so the role is the
        // rule and the relation is checked by `ControlFieldSynth`.
        case ElementType::Annotation:
            return byRoles({PlatformRole::Comment});

        // w -- an error. The ARIA `aria-invalid="spelling"` and the
        // INVALID_ENTRY state are two spellings of one thing.
        case ElementType::Error: {
            MatchRule rule;
            rule.states = stateBit(PlatformState::InvalidEntry);
            rule.stateMatch = MatchType::Any;
            rule.attributes["invalid"] = alternatives({"spelling", "grammar", "true"});
            rule.attributeMatch = MatchType::Any;
            return rule;
        }

        // p -- a paragraph WITH TEXT. The text condition cannot be a match
        // rule either, so the Text interface is the rule and emptiness is
        // filtered by the caller.
        case ElementType::Paragraph: {
            MatchRule rule = byRoles({PlatformRole::Paragraph});
            rule.interfaces = {"Text"};
            rule.interfaceMatch = MatchType::All;
            return rule;
        }

        case ElementType::Article:
            return byRoles({PlatformRole::Article});

        // A grouping carries NVDA's extra condition: an unnamed group is not
        // offered to the user. The name cannot be a match rule, so the caller
        // filters -- see `find`.
        case ElementType::Grouping:
            return byRoles({PlatformRole::Grouping, PlatformRole::Panel});

        case ElementType::Figure: {
            MatchRule rule;
            rule.attributes["tag"] = "figure";
            rule.attributeMatch = MatchType::All;
            return rule;
        }

        // A clickable element. DEVIATION FROM §9.2, and a forced one: NVDA
        // reads a `clickable` attribute that its own in-process buffer
        // synthesises from the DOM's event handlers, and AT-SPI exposes no
        // such attribute -- `a11y::State::Clickable` is ALUS's own derived
        // state, computed from the role, so it cannot be a match rule either.
        // What AT-SPI does expose is the Action interface, and an element with
        // an action IS an element the user can activate. That is narrower than
        // NVDA (a div with an onclick and no action is missed) and never
        // wrong, which is the right way round for a navigation key.
        case ElementType::Clickable: {
            MatchRule rule;
            rule.interfaces = {"Action"};
            rule.interfaceMatch = MatchType::All;
            return rule;
        }

        case ElementType::Tab:
            return byRoles({PlatformRole::PageTab});

        case ElementType::NotLinkBlock:
            // Derived; see findNotLinkBlock.
            return std::nullopt;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The search
// ---------------------------------------------------------------------------

QuickNav::QuickNav(a11y::ObjectPtr root) : root_(std::move(root)) {}

namespace {

// The conditions §9.2 names that a match rule cannot carry. Applied to each
// candidate as it comes back, which costs nothing: the object is already
// fetched and its name is one cached property read.
bool passesExtraConditions(ElementType type, const a11y::Object& object) {
    switch (type) {
        case ElementType::Grouping:
        case ElementType::Landmark:
            // §9.2's note: an unnamed container is not offered, so that a page
            // full of anonymous <div role="group"> does not fill the key.
            return !object.name().empty();
        case ElementType::Paragraph:
            // "a paragraph WITH text".
            return object.characterCount() > 0 && !object.isTextEmpty();
        case ElementType::Annotation:
            return true;
        default:
            return true;
    }
}

}  // namespace

a11y::ObjectPtr QuickNav::find(ElementType type, const a11y::ObjectPtr& from, bool forwards,
                               bool includeLayoutTables) const {
    if (root_ == nullptr || from == nullptr) {
        return nullptr;
    }
    if (type == ElementType::NotLinkBlock) {
        return findNotLinkBlock(from, forwards);
    }
    const std::optional<MatchRule> rule = ruleFor(type, includeLayoutTables);
    if (!rule.has_value()) {
        return nullptr;
    }

    a11y::Registry& registry = root_->registry();
    // FLOW, not CANONICAL -- T6.6, and the reason is in the file comment.
    // A small batch rather than one, so that the extra conditions above can
    // reject a candidate without a second round trip.
    constexpr int kBatch = 16;
    const std::vector<a11y::Ref> refs = registry.backend().matchesFrom(
        root_->ref(), from->ref(), *rule, a11y::SortOrder::Flow,
        a11y::TreeTraversal::InOrder, kBatch, forwards);
    for (const a11y::Ref ref : refs) {
        a11y::ObjectPtr candidate = registry.objectFor(ref);
        // `matchesFrom` handed over a reference per match; objectFor takes its
        // own, so each one is released here (N44).
        registry.backend().release(ref);
        if (candidate == nullptr || !candidate->isAlive()) {
            continue;
        }
        if (!passesExtraConditions(type, *candidate)) {
            continue;
        }
        return candidate;
    }
    return nullptr;
}

std::vector<a11y::ObjectPtr> QuickNav::all(ElementType type, int limit,
                                           bool includeLayoutTables) const {
    std::vector<a11y::ObjectPtr> out;
    if (root_ == nullptr) {
        return out;
    }
    const std::optional<MatchRule> rule = ruleFor(type, includeLayoutTables);
    if (!rule.has_value()) {
        return out;
    }
    a11y::Registry& registry = root_->registry();
    // TREE ORDER, not reading order, and measured rather than assumed: no
    // provider on Linux implements FLOW in `Collection::GetMatches`. Chrome
    // 157 (Blink), Firefox 153 (Gecko) and LibreOffice Writer (VCL/UNO) all
    // answer an EMPTY array for FLOW and TAB, and all three answer correctly
    // for CANONICAL (phase-6.md N90). Asking for FLOW here therefore bought a
    // guaranteed-empty round trip and nothing else.
    //
    // The KEYPRESS still asks for reading order, in find() below, because
    // `GetMatchesFrom` does honour FLOW in all three -- and the keypress is
    // what the user feels. T6.6 holds there.
    //
    // `AtspiBackend::matches` keeps a FLOW -> CANONICAL fallback for any
    // caller that still asks, so WebKitGTK or Qt (unmeasured) reverting this
    // line is a one-word change.
    const std::vector<a11y::Ref> refs =
        registry.backend().matches(root_->ref(), *rule, a11y::SortOrder::Canonical, limit);
    for (const a11y::Ref ref : refs) {
        a11y::ObjectPtr candidate = registry.objectFor(ref);
        registry.backend().release(ref);
        if (candidate == nullptr || !candidate->isAlive()) {
            continue;
        }
        if (!passesExtraConditions(type, *candidate)) {
            continue;
        }
        out.push_back(std::move(candidate));
        if (limit > 0 && static_cast<int>(out.size()) >= limit) {
            break;
        }
    }
    return out;
}

a11y::ObjectPtr QuickNav::findNotLinkBlock(const a11y::ObjectPtr& from, bool forwards) const {
    // NVDA's `n`: skip past a run of links to the next piece of real text.
    // Derived rather than matched, because "not inside a block of links" is a
    // property of a POSITION, not of an object.
    if (root_ == nullptr || from == nullptr) {
        return nullptr;
    }
    a11y::ObjectPtr leaf = DocumentLeaves::leafFor(root_, from);
    if (leaf == nullptr) {
        return nullptr;
    }
    bool sawLink = false;
    constexpr int kMaxSteps = 512;
    for (int step = 0; step < kMaxSteps; ++step) {
        leaf = forwards ? DocumentLeaves::next(root_, leaf)
                        : DocumentLeaves::previous(root_, leaf);
        if (leaf == nullptr) {
            return nullptr;
        }
        // Is this leaf inside a link?
        bool insideLink = false;
        a11y::ObjectPtr walk = leaf;
        for (int depth = 0; depth < 32 && walk != nullptr; ++depth) {
            if (walk->sameAs(*root_)) {
                break;
            }
            if (walk->role() == a11y::Role::Link) {
                insideLink = true;
                break;
            }
            walk = walk->parent();
        }
        if (insideLink) {
            sawLink = true;
            continue;
        }
        if (leaf->characterCount() == 0 || leaf->isTextEmpty()) {
            continue;
        }
        // Real text outside a link. Only interesting if we passed links on the
        // way -- otherwise `n` would behave like "next line".
        if (sawLink) {
            return leaf;
        }
        sawLink = false;
    }
    return nullptr;
}

}  // namespace alus::nav
