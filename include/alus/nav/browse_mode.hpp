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
// WHAT THE USER GETS, which is the manual test of phase-plan.md:
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
#include "alus/nav/elements_dialog.hpp"
#include "alus/nav/quick_nav.hpp"
#include "alus/nav/tree_interceptor.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/unit.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    void onGainFocus(const a11y::ObjectPtr& object) override;
    void onInnerFocus(const a11y::ObjectPtr& object) override;
    void onCaretMove(const a11y::ObjectPtr& object) override;

    CursorManager& cursor() noexcept { return cursor_; }
    const QuickNav& quickNav() const noexcept { return quickNav_; }

    // NVDA: `script_elementsList`'s data. Returned rather than shown: the
    // dialog is phase 9, and the list is the part that can be measured now.
    std::vector<ElementListItem> elementsList(ElementType type, int limit = 0) const;
    // Where the elements list opens among `listed` (that type's entries, in
    // order): the element the caret is on or in, else the last one before
    // the caret, else the first. NVDA's `_initialElement`. -1 when empty.
    int initialElementIndex(ElementType type, const std::vector<a11y::ObjectPtr>& listed) const;
    // What the elements list dialog answered. The choice is kept until the
    // focus comes back to the document (or a moment passes) and only then
    // performed: the browser's own focus event on returning would otherwise
    // move the caret off the element. Public for the dialog's callback and
    // the tests; `setElementObjectsForTest` stands in for the dialog's lists.
    void chooseElement(const ElementsDialogChoice& choice);
    void setElementObjectsForTest(std::map<std::string, std::vector<a11y::ObjectPtr>> objects) {
        elementObjects_ = std::move(objects);
    }

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

    // N134: whether `inner` sits inside `outer`, a few levels at most -- an
    // element nested in another of its kind is the same place to the user.
    static constexpr int kNestingDepth = 4;
    static constexpr int kMaxNestedSkips = 3;
    bool isNestedIn(const a11y::Object& inner, const a11y::Object& outer) const;
    // Whether an ancestor of `object` (kNestingDepth at most, below the
    // document) has one of these paths.
    //
    // N137: walked by raw references and paths, with each parent asked once
    // and remembered: Object::parent() would make an Object of every
    // ancestor, which reads its role, states and attributes. One memo serves
    // the whole elements list, so the list items around a link and its
    // "More" button are asked about once.
    struct ParentMemo {
        explicit ParentMemo(a11y::Backend& owner) : backend(owner) {}
        ~ParentMemo();
        ParentMemo(const ParentMemo&) = delete;
        ParentMemo& operator=(const ParentMemo&) = delete;
        a11y::Backend& backend;
        // An object's path -> its parent's path ("" for none).
        std::unordered_map<std::string, std::string> parentOf;
        // The references held for the ancestors met, released at the end.
        std::unordered_map<std::string, a11y::Ref> refOf;
    };
    bool hasAncestorIn(const a11y::Object& object, const std::unordered_set<std::string>& paths,
                       ParentMemo& memo) const;
    std::vector<ElementListItem> elementsList(ElementType type, int limit,
                                              ParentMemo& memo) const;
    static bool nestingIsSamePlace(ElementType type);
    static ElementListItem makeElementListItem(const a11y::ObjectPtr& object);

    // ---- the elements list (alus+f7) ----
    void showElementsList();
    // Runs the choice that is waiting, if one is. True when it did.
    bool performPendingElement();
    // Whether the focus landing on `object` leaves the caret where it is: the
    // document itself, the object the caret is on, or a container around it.
    bool focusContainsCaret(const a11y::ObjectPtr& object) const;

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
    // The elements list. The objects behind the labels the dialog shows, by
    // kind; the choice waiting for the focus to come back; the kind shown
    // first next time (NVDA remembers it).
    struct PendingElement {
        a11y::ObjectPtr object;
        bool activate = false;
    };
    static constexpr std::chrono::milliseconds kPendingElementWait{600};
    std::map<std::string, std::vector<a11y::ObjectPtr>> elementObjects_;
    std::optional<PendingElement> pendingElement_;
    // Shared by every document, as NVDA remembers it across pages.
    static inline std::string lastElementKind_ = "link";
    // Expires with this object: what a callback from the dialog or a timer
    // checks before touching it.
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    // documentBase: whether layout tables are offered to table navigation.
    bool includeLayoutTables_ = false;
    // NVDA: `singleLetterNavEnabled`, on by default as it is upstream.
    bool singleLetterNav_ = true;
};

}  // namespace alus::nav
