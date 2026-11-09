// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Browse mode: reading a document as a document.
//
// This is the object that owns a document's keys. It is ONE class and not five
// because a `script::ScriptableObject` is what the script lookup finds and
// what a gesture map section addresses, while NVDA's five
// (`BrowseModeTreeInterceptor`, `BrowseModeDocumentTreeInterceptor`,
// `CursorManager`, `DocumentWithTableNavigation`, `TextInfoTreeInterceptor`)
// are a Python inheritance chain, not five objects. So this object ANSWERS TO
// ALL FIVE NAMES, and the generated command table (data/scripts.csv: 110
// browseMode rows, 35 cursorManager rows, 13 documentBase rows) supplies every
// key, description and category. NOTHING IN THIS FILE RETYPES A KEYSTROKE --
// §5.4's rule, which is also why the quick-navigation and cursor scripts are
// bound FROM A TABLE in a loop rather than written out 64 times.
//
// WHAT THE USER GETS, which is the manual test of 02-FAZE.md:
//
//     alus+space       browse <-> focus mode
//     arrows           read by line / character
//     h / shift+h      headings;  1-9 by level
//     k / u / v        links, unvisited, visited
//     t l i b f e x r c g q s d o m p a w n   the other quick-nav types
//     enter / space    activate what the caret is on
//     escape           back to browse mode
//     alus+f7          the elements list
//     alus+downArrow   say all
//     control+alt+arrows   the table
//
// THE TWO RULES THAT MAKE IT USABLE RATHER THAN MERELY WORKING:
//
//  1. AUTO FOCUS MODE. Tabbing into an edit field must switch to focus mode by
//     itself, or the user types "h" and jumps to a heading instead of typing a
//     letter. NVDA decides from the role and the states of the newly focused
//     object; escape always comes back. T6.8.
//  2. SINGLE-LETTER NAVIGATION IS OFF IN FOCUS MODE, and that is enforced by
//     STEP 6 OF THE SCRIPT LOOKUP, not by a test inside each script:
//     `passThrough()` is what phase 3 built that step for, and T6.9 measures
//     that the ten-step lookup really filters it.
//
// THREADING: main thread only.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/a11y/output_reason.hpp"
#include "alus/nav/cursor_manager.hpp"
#include "alus/nav/quick_nav.hpp"
#include "alus/nav/tree_interceptor.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/unit.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace alus::input {
class Gesture;
}

namespace alus::script {
class Catalog;
}

namespace alus::nav {

// One row of the elements list (alus+f7).
struct ElementListItem {
    a11y::ObjectPtr object;
    std::string label;
    // For a heading, its level, so the dialog can indent the list.
    int level = 0;
};

class BrowseMode : public TreeInterceptor {
public:
    BrowseMode(a11y::ObjectPtr root, Service& service,
               const script::Catalog* catalog = nullptr);
    ~BrowseMode() override;

    // The chooser `TreeInterceptorRegistry` installs. §9.4: ONE overlay covers
    // Gecko and Chromium, keyed on the toolkit name, because the AT-SPI bridge
    // is generated from the same internal tree in both -- and because NVDA has
    // no `firefox.py` or `chrome.py` either.
    static TreeInterceptorPtr chooseForDocument(const a11y::ObjectPtr& object,
                                                Service& service);
    // Whether this object is a document root worth intercepting. Public so the
    // test can assert on the decision without building an interceptor.
    static bool isDocumentRoot(const a11y::Object& object);

    // NVDA: `shouldPassThrough`. Whether the focus landing on this object
    // means the user is operating a control rather than reading it. Static and
    // public: T6.8 asserts on the decision per role with no document at all.
    static bool shouldPassThroughFor(const a11y::Object& object);

    // ---- TreeInterceptor ----
    text::TextRangePtr makeTextRange(text::Position position) const override;
    bool setPassThrough(bool value, bool announce = true) override;
    void onGainFocus() override;
    void onInnerFocus(const a11y::ObjectPtr& object) override;
    void onCaretMove(const a11y::ObjectPtr& object) override;

    CursorManager& cursor() noexcept { return cursor_; }
    const QuickNav& quickNav() const noexcept { return quickNav_; }

    // NVDA: `script_elementsList`'s data. Returned rather than shown: the
    // dialog is phase 9, and the list is the part that can be measured now.
    std::vector<ElementListItem> elementsList(ElementType type, int limit = 0) const;

    // A quick-nav move, exposed so the elements list and the tests make the
    // same move a key makes. False when there is no next element of the type,
    // which is also announced.
    bool moveToElement(ElementType type, bool forwards);
    // Puts the caret on an object and reads it: what every jump ends with.
    bool moveTo(const a11y::ObjectPtr& object, a11y::OutputReason reason);
    // NVDA: `script_activatePosition`. Activates what the caret is on, and
    // switches to focus mode when that object is one the user must then type
    // into.
    bool activatePosition();

    const text::FormatReportSettings& documentSettings() const noexcept { return settings_; }

    // NVDA: `singleLetterNavEnabled`. With it off, a letter key is not
    // ALUS's: `h` types an h instead of jumping to the next heading.
    //
    // It is enforced in `alternativeScript`, which is the same seam upstream
    // uses and the only one that works here: the grab set is computed by
    // asking the lookup whether a gesture would find a script, and
    // `alternativeScript` is part of that answer -- so turning this off
    // RELEASES the letters to the application rather than swallowing them
    // (N18: a grabbed key cannot be handed on).
    bool singleLetterNavEnabled() const noexcept { return singleLetterNav_; }
    const script::Script* alternativeScript(const input::Gesture& gesture,
                                            const script::Script* found) const override;

private:
    // The three binding loops. Each reads a table; see the file comment.
    void bindQuickNavScripts();
    void bindCursorScripts();
    void bindDocumentScripts();
    void bindTableScripts();

    void toggleSingleLetterNav(input::Gesture& gesture);
    // `p` and `shift+p`: the next or previous TEXT paragraph -- a paragraph
    // that reads like prose rather than a heading, a menu item or a row of
    // links. See kTextParagraphRegex in the .cpp for what decides that.
    bool moveToTextParagraph(bool forwards);
    void reportNoTextParagraph(bool forwards);

    // Reads what a move landed on, with the control fields a document needs.
    void report(const CursorMove& move, a11y::OutputReason reason);
    void reportNoNext(ElementType type, bool forwards);
    // The object the caret is on, through the hypertext path -- a link inside
    // a paragraph, not the paragraph.
    a11y::ObjectPtr objectAtCaret() const;

    // ---- table navigation (documentBase.DocumentWithTableNavigation) ----
    // The cell the caret is in, and its table. Null when the caret is not in
    // a table, which is what the "not in a table" announcement is for.
    a11y::ObjectPtr cellAtCaret() const;
    bool moveInTable(int rowDelta, int columnDelta, bool toEdge);
    bool speakAxis(bool column);

    QuickNav quickNav_;
    CursorManager cursor_;
    // The settings this document is read with, captured at construction: a
    // SIGHUP mid-document must not change the meaning of a field stack halfway
    // through a say-all.
    text::FormatReportSettings settings_;
    // NVDA: `_lastFocusableObject`.
    a11y::ObjectPtr lastFocusable_;
    // Whether ALUS chose the pass-through state or the user did. Escape only
    // undoes ALUS's own choice, so escape inside a form the user deliberately
    // entered does not throw them out of it.
    bool passThroughWasAutomatic_ = false;
    // documentBase: whether layout tables are offered to table navigation.
    bool includeLayoutTables_ = false;
    // NVDA: `singleLetterNavEnabled`, on by default as it is upstream.
    bool singleLetterNav_ = true;
};

}  // namespace alus::nav
