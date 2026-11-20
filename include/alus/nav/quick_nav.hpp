// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Quick navigation: one key, one element type, one D-Bus round trip.
//
// Port of NVDA's `_iterNodesByType` and its `_searchableAttribsForNodeType`
// tables (`virtualBuffers/gecko_ia2.py`), rewritten against
// `org.a11y.atspi.Collection` exactly as architecture.md §9.2 specifies:
//
//     _iterNodesByType(itemType, direction, pos)
//       -> Collection::GetMatchesFrom(current, matchRule, sortBy = FLOW,
//                                     tree = INORDER, count, traverse)
//
// THIS FILE IS A TABLE, and that is deliberate. The 22 rows of §9.2 are the
// whole of quick navigation; every one of them is `role OR aria token OR html
// tag`, which is AT-SPI's ANY/ALL/NONE over roles and attributes. There is no
// algorithm to get wrong -- only rules to get right, one test per row (T6.5).
//
// WHY `sortBy = FLOW` AND NOT `CANONICAL`, which is T6.6: CANONICAL is the
// order of the TREE and FLOW is the order of READING. A floated sidebar comes
// early in the tree and late on the page; quick navigation in tree order
// sends the user backwards through a document they are reading forwards.
//
// WHAT IS NOT IN THE TABLE, and why:
//   * `n` (not-link-block) is DERIVED, as NVDA derives it: it is "the next
//     piece of text that is not inside a run of links", which no match rule
//     can express. It walks the result of the link rule instead.
//   * nothing here uses `OffsetsTextRange`. That is the phase 5 handover's
//     point 6 and it is a measurement, not a preference: N45 found that
//     Chromium answers no line granularity, so a position computed from
//     offsets would be a search through the whole document, per keystroke.
//
// THREADING: main thread only.
#pragma once

#include "alus/a11y/backend.hpp"
#include "alus/a11y/object.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::nav {

// The element types the user can jump between. The order is the order of the
// elements-list dialog and of the command table, and the names are NVDA's
// own, because `data/scripts.csv` addresses the scripts by them
// (`nextHeading`, `previousCheckBox`).
enum class ElementType {
    Heading,
    Heading1,
    Heading2,
    Heading3,
    Heading4,
    Heading5,
    Heading6,
    Heading7,
    Heading8,
    Heading9,
    Link,
    VisitedLink,
    UnvisitedLink,
    FormField,
    ListItem,
    List,
    Table,
    Button,
    CheckBox,
    RadioButton,
    ComboBox,
    Edit,
    Graphic,
    BlockQuote,
    Separator,
    Frame,
    Landmark,
    EmbeddedObject,
    Annotation,
    Error,
    Paragraph,
    Article,
    Grouping,
    Figure,
    Clickable,
    Tab,
    // N156: upstream's types that have no key, only a command.
    ToggleButton,
    MenuItem,
    ProgressBar,
    Slider,
    // Derived, not a match rule. See the file comment.
    NotLinkBlock,
};

// The script-name suffix ("Heading", "CheckBox"), which is how a command
// binds: `nextHeading` / `previousHeading`.
std::string_view elementTypeScriptName(ElementType type);
// What is spoken when there is no next one ("no next heading").
// The user-visible name of an element type ("heading at level 2"), TRANSLATED
// into the language in effect: it is what "no next heading" and the elements
// list are built from.
std::string_view elementTypeLabel(ElementType type);
// The same label as its English msgid, for logs and for tests that must not
// depend on the catalogue.
std::string_view elementTypeLabelUntranslated(ElementType type);
// Every type, in the order above. What the command group iterates to bind its
// scripts, and what the elements list offers.
const std::vector<ElementType>& allElementTypes();
std::optional<ElementType> parseElementType(std::string_view scriptName);

class QuickNav {
public:
    // `root` is the document root -- the object that carries the Collection
    // interface. A root without one is not an error and is reported as "no
    // next X" (see Backend::matchesFrom).
    QuickNav(a11y::ObjectPtr root);

    // The rule for a type. Static and public because T6.5 asserts on the 22
    // rules directly, with no desktop and no document: a rule is a value, and
    // a wrong one is a bug a test can see.
    //
    // Nothing for ElementType::NotLinkBlock, which has no rule.
    static std::optional<a11y::MatchRule> ruleFor(ElementType type,
                                                  bool includeLayoutTables = false);

    // The next (or previous) element of this type after `from`. Null when
    // there is none, which is the normal case at the end of a document.
    //
    // ONE D-BUS ROUND TRIP, which is what T6.7 measures (p95 < 20 ms).
    a11y::ObjectPtr find(ElementType type, const a11y::ObjectPtr& from, bool forwards,
                         bool includeLayoutTables = false) const;

    // Every element of this type in the document, for the elements list
    // (alus+f7). `limit` caps it, because a long page has thousands of links
    // and the dialog shows a screenful.
    std::vector<a11y::ObjectPtr> all(ElementType type, int limit = 0,
                                     bool includeLayoutTables = false) const;

    const a11y::ObjectPtr& root() const noexcept { return root_; }

private:
    // The derived type. NVDA's own approach: find the next text that is not
    // in a block of links.
    a11y::ObjectPtr findNotLinkBlock(const a11y::ObjectPtr& from, bool forwards) const;

    a11y::ObjectPtr root_;
};

// The ARIA landmark tokens, as `xml-roles` spells them. One place, because the
// landmark rule and `ControlField::landmark` must agree about what a landmark
// is or `d` will jump to things that are never announced as landmarks.
const std::vector<std::string>& ariaLandmarkTokens();

}  // namespace alus::nav
