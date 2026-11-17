// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/browse_mode.hpp.
#include "alus/nav/browse_mode.hpp"

#include "alus/a11y/mapping.hpp"

#include "alus/text/regex.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_range.hpp"
#include "alus/core/cached.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/gesture.hpp"
#include "alus/nav/document_range.hpp"
#include "alus/nav/elements_dialog.hpp"
#include "alus/nav/paragraph_nav.hpp"
#include "alus/nav/service.hpp"
#include "alus/script/command_status.hpp"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

namespace alus::nav {
namespace {

using a11y::OutputReason;
using a11y::Role;
using a11y::State;

// The NVDA classes this one object answers to. A gesture map section naming
// any of them reaches these scripts, which is what makes the generated command
// table apply without a line of it being retyped (§5.4).
//
// `globalCommands.GlobalCommands` IS IN THE LIST, and it has to be: upstream
// puts the browse/focus mode toggle on GlobalCommands, where it reaches the
// focus object's tree interceptor, so the generated map assigns `alus+space`
// in THAT section. ALUS homes the implementation on the interceptor itself
// instead -- which is the right owner -- and the consequence is that the
// interceptor must also answer to the section name, or step 6 of the lookup
// rejects the map entry on the location and `alus+space` does nothing at all.
// It did nothing at all until this line was added (docs/history/phase-9.md N100).
//
// It shadows nothing: step 6 only accepts a map entry whose script THIS
// object owns, and `toggleVirtualBufferPassThrough` is the only name in the
// section that browse mode owns.
std::vector<std::string> browseModeClassChain() {
    return {
        "browseMode.BrowseModeDocumentTreeInterceptor",
        "browseMode.BrowseModeTreeInterceptor",
        "documentBase.DocumentWithTableNavigation",
        "cursorManager.CursorManager",
        "treeInterceptorHandler.TextInfoTreeInterceptor",
        "globalCommands.GlobalCommands",
        // Upstream's `virtualBuffers.VirtualBuffer` IS a
        // `BrowseModeDocumentTreeInterceptor`, so a map section naming it
        // addresses this same object. One key is assigned there (`alus+f5`,
        // refreshBuffer), and without this name it could be reached by
        // nothing at all.
        "virtualBuffers.VirtualBuffer",
    };
}

// One row of the cursor-movement table. NVDA's own script names, because the
// catalogue is keyed on them.
struct CursorRow {
    const char* name;
    text::TextUnit unit;
    int direction;
    bool select;
};

constexpr std::array<CursorRow, 24> kCursorRows{{
    {"moveByLine_back", text::TextUnit::Line, -1, false},
    {"moveByLine_forward", text::TextUnit::Line, 1, false},
    {"moveByCharacter_back", text::TextUnit::Character, -1, false},
    {"moveByCharacter_forward", text::TextUnit::Character, 1, false},
    {"moveByWord_back", text::TextUnit::Word, -1, false},
    {"moveByWord_forward", text::TextUnit::Word, 1, false},
    {"moveByParagraph_back", text::TextUnit::Paragraph, -1, false},
    {"moveByParagraph_forward", text::TextUnit::Paragraph, 1, false},
    {"moveBySentence_back", text::TextUnit::Sentence, -1, false},
    {"moveBySentence_forward", text::TextUnit::Sentence, 1, false},
    {"moveByPage_back", text::TextUnit::Page, -1, false},
    {"moveByPage_forward", text::TextUnit::Page, 1, false},
    {"selectCharacter_back", text::TextUnit::Character, -1, true},
    {"selectCharacter_forward", text::TextUnit::Character, 1, true},
    {"selectWord_back", text::TextUnit::Word, -1, true},
    {"selectWord_forward", text::TextUnit::Word, 1, true},
    {"selectLine_back", text::TextUnit::Line, -1, true},
    {"selectLine_forward", text::TextUnit::Line, 1, true},
    {"selectParagraph_back", text::TextUnit::Paragraph, -1, true},
    {"selectParagraph_forward", text::TextUnit::Paragraph, 1, true},
    {"selectPage_back", text::TextUnit::Page, -1, true},
    {"selectPage_forward", text::TextUnit::Page, 1, true},
    {"selectSentence_back", text::TextUnit::Sentence, -1, true},
    {"selectSentence_forward", text::TextUnit::Sentence, 1, true},
}};

// The roles whose focus means "the user is operating a control", so browse
// mode gets out of the way. NVDA's own list, plus the measured addition of
// SWITCH (Firefox's own role for an ARIA switch, which did not exist when
// NVDA's list was written).
bool isInteractiveRole(Role role) {
    switch (role) {
        case Role::EditableText:
        case Role::ComboBox:
        case Role::CheckBox:
        case Role::RadioButton:
        case Role::Slider:
        case Role::SpinButton:
        case Role::ToggleButton:
        case Role::Switch:
        case Role::TreeView:
        case Role::TreeViewItem:
        case Role::Tab:
        case Role::MenuItem:
        case Role::Table:
            return false;
        default:
            return false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction and the chooser
// ---------------------------------------------------------------------------

BrowseMode::BrowseMode(a11y::ObjectPtr root, Service& service,
                       const script::Catalog* catalog)
    : TreeInterceptor(browseModeClassChain(), root, service, catalog),
      quickNav_(root),
      cursor_(
          // The two factories. Both build a DOCUMENT range over this root --
          // never a stored one, which is the N49 rule (tree_interceptor.hpp).
          [this](text::Position position) -> text::TextRangePtr {
              return DocumentTextRange::make(root_, settings_, position);
          },
          [this](const text::Bookmark& mark) -> text::TextRangePtr {
              text::TextRangePtr range =
                  DocumentTextRange::make(root_, settings_, text::Position::First);
              if (range == nullptr || !range->moveToBookmark(mark)) {
                  return nullptr;
              }
              return range;
          }),
      settings_(service.formatSettings()) {
    // The class chain carries the catalogue rows, so the bindings below take
    // their keys, descriptions and categories from data/scripts.csv.
    bindQuickNavScripts();
    bindScript("nextTextParagraph",
               [this](input::Gesture&) { moveToTextParagraph(/*forwards=*/true); });
    bindScript("previousTextParagraph",
               [this](input::Gesture&) { moveToTextParagraph(/*forwards=*/false); });
    bindScript("toggleSingleLetterNav",
               [this](input::Gesture& gesture) { toggleSingleLetterNav(gesture); });
    bindCursorScripts();
    bindDocumentScripts();
    bindTableScripts();
    // LAST, so that nothing above is replaced: the browse-mode commands ALUS
    // has not written yet, bound to a spoken "not in this version" rather
    // than left silent (script/command_status.hpp). Here and not on a global
    // commands object, because `kb:p` and `kb:alus+f5` may only be taken from
    // the application while the focus is inside a document -- step 6 of the
    // lookup is what guarantees that, and the grab set follows it (N61).
    if (catalog != nullptr) {
        const std::size_t announced = script::bindAnnouncements(
            *this, *catalog, [this](const std::string& text) { service_.message(text); });
        if (announced > 0) {
            ALUS_DEBUG("browse mode: {} command(s) announce that they are not in this version",
                       announced);
        }
    }
}

BrowseMode::~BrowseMode() = default;

bool BrowseMode::isDocumentRoot(const a11y::Object& object) {
    // The role first and liveness only for a document: this runs for every
    // ancestor of every focus, and a dead object answers Role::Unknown anyway,
    // so asking `alive` first was one round trip per ancestor per key (N129).
    // THE ROLE IS THE WHOLE TEST, and it is one role: ALUS's role map folds
    // AT-SPI's six document roles (DOCUMENT_FRAME, DOCUMENT_WEB,
    // DOCUMENT_TEXT, DOCUMENT_SPREADSHEET, DOCUMENT_PRESENTATION,
    // DOCUMENT_EMAIL) onto `Role::Document`, so Gecko, Chromium and
    // LibreOffice Writer all answer the same thing here.
    //
    // THAT IS WHY §9.4's TWO CLASSES ARE ONE CLASS. The plan asks for a
    // `GeckoChromiumDocument` for browsers and a `CompoundDocument` for
    // Writer; since the range is compound for both (document_range.hpp), the
    // only real difference is quick navigation, and that difference needs no
    // code: LibreOffice exposes no Collection interface, so `matchesFrom`
    // answers empty and the user hears "no next heading" -- which is true.
    // The platform role answers for nearly every object without a round trip
    // (it is remembered per object); only the document roles map to
    // Role::Document, and no refinement or behaviour turns anything else into
    // one, so it is a safe first filter before the refined role.
    if (a11y::roleFromPlatform(object.rawPlatformRole()) != Role::Document) {
        return false;
    }
    if (object.role() != Role::Document || !object.isAlive()) {
        return false;
    }
    // A document the user cannot read is not worth intercepting -- an empty
    // about:blank is the common case and intercepting it makes the arrow keys
    // silent instead of passing them to the application.
    return object.childCount() > 0 || object.characterCount() > 0;
}

TreeInterceptorPtr BrowseMode::chooseForDocument(const a11y::ObjectPtr& object,
                                                 Service& service) {
    if (object == nullptr || !isDocumentRoot(*object)) {
        return nullptr;
    }
    // An EDITABLE document is not browse mode: it is a text editor that
    // happens to be a document (a rich-text compose field), and phase 5's
    // `EditableText` behaviour already handles it correctly. Intercepting it
    // would take the arrow keys away from someone writing.
    if (object->states().has(State::Editable)) {
        ALUS_DEBUG("browse mode: {} is editable; not intercepting", object->path());
        return nullptr;
    }
    // The catalogue, so the commands carry their descriptions and so the
    // [announce] list can reach them. Without it browse mode's commands are
    // nameless to input help and invisible to the gesture dialog (N101).
    return TreeInterceptorPtr(new BrowseMode(object, service, service.catalog()));
}

// ---------------------------------------------------------------------------
// Pass through -- the auto focus mode rule (T6.8)
// ---------------------------------------------------------------------------

bool BrowseMode::shouldPassThroughFor(const a11y::Object& object) {
    if (!object.isAlive()) {
        return false;
    }
    const a11y::StateSet& states = object.states();
    // THE DECISIVE TEST IS EDITABILITY, not the role: an ARIA widget can carry
    // any role and still be something the user types into, and a role list
    // alone is how a screen reader ends up eating someone's keystrokes.
    if (states.has(State::Editable)) {
        return true;
    }
    // A control the user operates with the arrow keys owns them: giving the
    // arrows to browse mode in a list box means the selection never moves.
    switch (object.role()) {
        case Role::ComboBox:
        case Role::Slider:
        case Role::SpinButton:
        case Role::TreeView:
        case Role::TreeViewItem:
        case Role::MenuItem:
        case Role::Tab:
            return true;
        case Role::List:
            // A FOCUSABLE list is a listbox the user operates; a plain <ul> is
            // not focusable and is content to read.
            return states.has(State::Focusable);
        case Role::TableRow:
        case Role::TableCell:
        case Role::TableRowHeader:
        case Role::TableColumnHeader:
            // N132, NVDA #5118: a row or cell that takes the focus belongs to
            // an ARIA grid the page operates itself -- Gmail's message list,
            // where the arrows go from message to message and x ticks one.
            // A plain table's cells never take the focus.
            return states.has(State::Focusable) || states.has(State::Focused);
        case Role::ListItem: {
            // The same for an item of a focusable list (a message list built
            // from a listbox, NVDA #13221).
            if (!states.has(State::Focused)) {
                return false;
            }
            const a11y::ObjectPtr parent = object.parent();
            return parent != nullptr && parent->role() == Role::List &&
                   parent->states().has(State::Focusable);
        }
        default:
            break;
    }
    (void)isInteractiveRole(object.role());
    return false;
}

bool BrowseMode::setPassThrough(bool value, bool announce) {
    const bool changed = TreeInterceptor::setPassThrough(value, announce);
    if (!changed) {
        return false;
    }
    // THIS IS WHAT MAKES FOCUS MODE REAL, and T6.9 with it. The grab set is
    // computed by asking the ten-step lookup whether each gesture would find a
    // script here and now; `passThrough` makes the answer no for every
    // single-letter one, so the letters are RELEASED to the application
    // instead of being tested for inside each of the 74 quick-nav scripts.
    // Without this call the letters stay grabbed and typing in a form field
    // navigates the page.
    service_.refreshGrabs();
    if (!value) {
        // Leaving focus mode: ALUS's caret is authoritative again, and it
        // stays where the application's caret ended up rather than where it
        // was when focus mode started -- otherwise typing in a field and
        // pressing escape reads the wrong line.
        if (text::TextRangePtr caret =
                DocumentTextRange::make(root_, settings_, text::Position::Caret)) {
            cursor_.setCaret(*caret);
        }
    } else {
        // Entering focus mode: push ALUS's caret into the application so the
        // user starts typing where they were reading.
        cursor_.applyToApplication();
    }
    return true;
}

void BrowseMode::onGainFocus(const a11y::ObjectPtr& object) {
    // alus+v pressed in another document: this one lays out its lines the
    // same way from the moment it is back (N142).
    settings_.useScreenLayout = service_.formatSettings().useScreenLayout;
    if (performPendingElement()) {
        return;
    }
    if (!isReady()) {
        ALUS_DEBUG("browse mode: document not ready; nothing announced yet");
        return;
    }
    // Arriving in the document ON a control that owns its keys (N132: Gmail
    // opens with the focus on the message list) starts in focus mode, as
    // moving onto it from inside the document would.
    if (object != nullptr && !object->sameAs(*root_) && shouldPassThroughFor(*object)) {
        lastFocusable_ = object;
        passThroughWasAutomatic_ = true;
        setPassThrough(true);
        return;
    }
    // The caret the browser already has, or the start of the document.
    text::TextRangePtr at = DocumentTextRange::make(root_, settings_, text::Position::Caret);
    if (at == nullptr) {
        at = DocumentTextRange::make(root_, settings_, text::Position::First);
    }
    if (at == nullptr) {
        return;
    }
    cursor_.setCaret(*at);
    text::TextRangePtr line = at->copy();
    if (line->expand(text::TextUnit::Line)) {
        service_.speakRange(*line, text::TextUnit::Line, OutputReason::Focus, root_);
    }
}

void BrowseMode::onInnerFocus(const a11y::ObjectPtr& object) {
    if (object == nullptr) {
        return;
    }
    if (performPendingElement()) {
        return;
    }
    lastFocusable_ = object;
    const bool wanted = shouldPassThroughFor(*object);
    if (wanted && !passThrough()) {
        passThroughWasAutomatic_ = true;
        setPassThrough(true);
        return;
    }
    if (!wanted && passThrough() && passThroughWasAutomatic_) {
        // The focus moved out of the control ALUS switched for, so the switch
        // is undone. A pass-through the USER asked for is left alone.
        passThroughWasAutomatic_ = false;
        setPassThrough(false);
        return;
    }
    if (!passThrough()) {
        // THE FOCUS AROUND THE CARET DOES NOT MOVE IT (N132, NVDA: "the caret
        // isn't within the focused node"). A page that puts the focus back on
        // itself or on a container -- YouTube after "skip ad" -- would
        // otherwise throw the reader to the top of the page or of the player.
        if (focusContainsCaret(object)) {
            return;
        }
        // Still reading: the caret follows the focus, so tab lands the review
        // and the caret in the same place.
        if (text::TextRangePtr at = DocumentTextRange::makeAt(
                root_, settings_,
                DocumentPosition{DocumentLeaves::leafFor(root_, object), 0},
                DocumentPosition{DocumentLeaves::leafFor(root_, object), 0})) {
            cursor_.setCaret(*at);
        }
    }
}

bool BrowseMode::focusContainsCaret(const a11y::ObjectPtr& object) const {
    if (object->sameAs(*root_)) {
        // The document itself holds every position, a vanished one included:
        // the caret is restored from its bookmark next time it is used.
        return true;
    }
    const text::TextRangePtr caret = cursor_.caretRange();
    const auto* range = dynamic_cast<const DocumentTextRange*>(caret.get());
    if (range == nullptr || !range->start().valid()) {
        return false;
    }
    if (range->start().leaf->sameAs(*object)) {
        // The caret is already on it: tab back onto the same link says it
        // again (the focus announcement) without moving anything.
        return true;
    }
    if (const a11y::ObjectPtr at = objectAtCaret(); at != nullptr && at->sameAs(*object)) {
        return true;
    }
    if (DocumentLeaves::isLeaf(*object)) {
        return false;
    }
    for (const a11y::ObjectPtr& ancestor :
         DocumentLeaves::ancestorChain(root_, range->start().leaf)) {
        if (ancestor != nullptr && ancestor->sameAs(*object)) {
            return true;
        }
    }
    return false;
}

void BrowseMode::onCaretMove(const a11y::ObjectPtr& object) {
    if (!passThrough() || object == nullptr) {
        return;
    }
    // In focus mode the application owns the caret; ALUS follows it so that
    // escaping back to browse mode lands where the user left off.
    if (text::TextRangePtr at =
            DocumentTextRange::make(root_, settings_, text::Position::Caret)) {
        cursor_.setCaret(*at);
    }
}

// ---------------------------------------------------------------------------
// The range
// ---------------------------------------------------------------------------

text::TextRangePtr BrowseMode::makeTextRange(text::Position position) const {
    if (position == text::Position::Caret || position == text::Position::Selection) {
        // The caret of a document is ALUS's, not the application's: that is
        // what browse mode means.
        if (position == text::Position::Selection) {
            if (text::TextRangePtr selection = cursor_.selection()) {
                return selection;
            }
        }
        if (text::TextRangePtr caret = cursor_.caretRange()) {
            return caret;
        }
    }
    return DocumentTextRange::make(root_, settings_, position);
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

void BrowseMode::report(const CursorMove& move, OutputReason reason) {
    if (move.atEdge) {
        // Translators: spoken when a movement command reaches the edge of the
        // document.
        service_.message(_("edge of document"));
        return;
    }
    if (move.toRead == nullptr) {
        return;
    }
    service_.speakRange(*move.toRead, move.unit, reason, root_);
}

void BrowseMode::reportNoNext(ElementType type, bool forwards) {
    const std::string_view label = elementTypeLabel(type);
    // Translators: spoken when there is no next element of a type; %s names
    // the type, e.g. "no next heading".
    // Translators: spoken when there is no previous element of a type; %s
    // names the type, e.g. "no previous heading".
    //
    // Two patterns and not a prefix plus a label: Serbian inflects the noun
    // after "next", so a translator who is handed "no next " and "heading"
    // separately cannot produce a correct sentence (phase-9.md N76).
    service_.message(i18n::substitute(
        forwards ? _("no next %s") : _("no previous %s"), label));
}

a11y::ObjectPtr BrowseMode::objectAtCaret() const {
    text::TextRangePtr caret = cursor_.caretRange();
    if (caret == nullptr) {
        return nullptr;
    }
    if (auto* document = dynamic_cast<DocumentTextRange*>(caret.get())) {
        return document->objectAtStart();
    }
    return nullptr;
}

bool BrowseMode::moveTo(const a11y::ObjectPtr& object, OutputReason reason,
                        std::optional<ElementType> type) {
    if (object == nullptr) {
        return false;
    }
    const a11y::ObjectPtr leaf = DocumentLeaves::leafFor(root_, object);
    if (leaf == nullptr) {
        ALUS_DEBUG("browse mode: {} is not inside this document", object->path());
        return false;
    }
    text::TextRangePtr at = DocumentTextRange::makeAt(root_, settings_,
                                                      DocumentPosition{leaf, 0},
                                                      DocumentPosition{leaf, 0});
    if (at == nullptr) {
        return false;
    }
    cursor_.setCaret(*at);
    object->scrollIntoView();
    // The navigator object follows the caret, so the numpad reviews what the
    // user just jumped to (phase 4's handover point 3).
    if (a11y::Registry* registry = service_.registry()) {
        registry->api().setNavigatorObject(object, /*isFocus=*/false);
    }
    if (type.has_value()) {
        if (text::TextRangePtr element = elementRange(*at, object, *type)) {
            service_.speakRange(*element, std::nullopt, reason, root_);
            return true;
        }
    }
    text::TextRangePtr line = at->copy();
    if (line->expand(text::TextUnit::Line)) {
        service_.speakRange(*line, text::TextUnit::Line, reason, root_);
    }
    return true;
}

namespace {

// NVDA's quick-nav types that read one line rather than the whole element
// (browseMode.py, `readUnit=textInfos.UNIT_LINE`): a table or a landmark read
// whole would be the page.
bool readsOneLine(ElementType type) {
    switch (type) {
        case ElementType::Table:
        case ElementType::List:
        case ElementType::Edit:
        case ElementType::Frame:
        case ElementType::NotLinkBlock:
        case ElementType::Landmark:
        case ElementType::Clickable:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool BrowseMode::leafInside(const a11y::ObjectPtr& leaf, const a11y::Object& object) const {
    if (leaf->sameAs(object)) {
        return true;
    }
    for (const a11y::ObjectPtr& ancestor : DocumentLeaves::ancestorChain(root_, leaf)) {
        if (ancestor != nullptr && ancestor->sameAs(object)) {
            return true;
        }
    }
    return false;
}

text::TextRangePtr BrowseMode::elementRange(const text::TextRange& at,
                                            const a11y::ObjectPtr& object,
                                            ElementType type) const {
    // N142, NVDA's TextInfoQuickNavItem.report: the element, not the line it
    // is on. A field with a label in front of it said the label and then its
    // name ("Clear Clear button"); a link in a sentence read the sentence.
    const auto* start = dynamic_cast<const DocumentTextRange*>(&at);
    if (start == nullptr || !start->start().valid()) {
        return nullptr;
    }
    // An editable field reads a line of itself, never its whole text.
    const a11y::Role role = object->role();
    const bool editable = type == ElementType::FormField &&
                          (role == a11y::Role::EditableText || role == a11y::Role::PasswordEdit);
    // The element's own end: its last leaf, walked from its first.
    constexpr int kMaxElementLeaves = 64;
    DocumentPosition end = start->start();
    bool whole = true;
    if (DocumentLeaves::isLeaf(*object) || end.leaf->sameAs(*object)) {
        end = DocumentPosition{end.leaf, DocumentTextRange::leafLengthOf(end.leaf)};
    } else {
        a11y::ObjectPtr leaf = end.leaf;
        int walked = 0;
        for (; walked < kMaxElementLeaves; ++walked) {
            end = DocumentPosition{leaf, DocumentTextRange::leafLengthOf(leaf)};
            const a11y::ObjectPtr next = DocumentLeaves::next(root_, leaf);
            if (next == nullptr || !leafInside(next, *object)) {
                break;
            }
            leaf = next;
        }
        whole = walked < kMaxElementLeaves;
    }
    text::TextRangePtr range =
        DocumentTextRange::makeAt(root_, settings_, start->start(), end);
    if (range == nullptr) {
        return nullptr;
    }
    if (whole && !readsOneLine(type) && !editable) {
        return range;
    }
    // One line from the element's start, and no further than the element.
    text::TextRangePtr line = at.copy();
    if (!line->expand(text::TextUnit::Line)) {
        return range;
    }
    line->setEndPoint(at, text::EndPointPair::StartToStart);
    if (line->compareEndPoints(*range, text::EndPointPair::EndToEnd) > 0) {
        line->setEndPoint(*range, text::EndPointPair::EndToEnd);
    }
    return line;
}

bool BrowseMode::isNestedIn(const a11y::Object& inner, const a11y::Object& outer) const {
    ParentMemo memo{root_->registry().backend()};
    return hasAncestorIn(inner, {outer.path()}, memo);
}

BrowseMode::ParentMemo::~ParentMemo() {
    for (const auto& [path, ref] : refOf) {
        backend.release(ref);
    }
}

bool BrowseMode::hasAncestorIn(const a11y::Object& object,
                               const std::unordered_set<std::string>& paths,
                               ParentMemo& memo) const {
    // N137: see ParentMemo. Measured on 300 links: the Object walk made 634
    // calls each of parent, role, states and attributes; a raw walk without
    // the memo made 1199 parent calls, because siblings no longer shared
    // their ancestors. A path costs no call at all.
    a11y::Backend& backend = memo.backend;
    const std::string& rootPath = root_->path();
    std::string path = object.path();
    a11y::Ref ref = object.ref();  // borrowed; the memo's after the first step
    for (int depth = 0; depth < kNestingDepth; ++depth) {
        std::string up;
        if (const auto known = memo.parentOf.find(path); known != memo.parentOf.end()) {
            up = known->second;
        } else {
            if (ref == 0) {
                const auto held = memo.refOf.find(path);
                ref = held != memo.refOf.end() ? held->second : 0;
            }
            if (ref == 0) {
                return false;
            }
            const a11y::Ref parentRef = backend.parent(ref);
            if (parentRef != 0) {
                up = backend.path(parentRef);
                if (!memo.refOf.emplace(up, parentRef).second) {
                    backend.release(parentRef);
                }
            }
            memo.parentOf.emplace(path, up);
        }
        if (up.empty() || up == rootPath) {
            return false;
        }
        if (paths.count(up) != 0) {
            return true;
        }
        path = std::move(up);
        ref = 0;
    }
    return false;
}

bool BrowseMode::moveToElement(ElementType type, bool forwards) {
    const a11y::ObjectPtr from = objectAtCaret();
    const a11y::ObjectPtr start = from != nullptr ? from : DocumentLeaves::first(root_);
    if (start == nullptr) {
        reportNoNext(type, forwards);
        return false;
    }
    a11y::ObjectPtr found = quickNav_.find(type, start, forwards, includeLayoutTables_);
    // N134: an element nested in the one the caret is on (YouTube's menu: a
    // link inside a link, same name) is the same place, and stopping on it
    // reads the same link twice. Forwards that is a descendant of `start`,
    // backwards an ancestor of it.
    for (int skipped = 0;
         found != nullptr && nestingIsSamePlace(type) && skipped < kMaxNestedSkips; ++skipped) {
        const bool samePlace = forwards ? isNestedIn(*found, *start) : isNestedIn(*start, *found);
        if (!samePlace) {
            break;
        }
        found = quickNav_.find(type, found, forwards, includeLayoutTables_);
    }
    if (found == nullptr) {
        reportNoNext(type, forwards);
        return false;
    }
    // QUICKNAV, not CARET: #2591, hearing "out of list" on every jump makes
    // navigation slower rather than clearer, and the reason is what switches
    // that off (see getTextRangeSpeech).
    return moveTo(found, OutputReason::QuickNav, type);
}

// ---------------------------------------------------------------------------
// The elements list (alus+f7)
// ---------------------------------------------------------------------------

namespace {

struct ElementKind {
    const char* id;
    ElementType type;
};

// The dialog's types, in NVDA's order.
constexpr std::array<ElementKind, 5> kElementKinds{{
    {"link", ElementType::Link},
    {"heading", ElementType::Heading},
    {"formField", ElementType::FormField},
    {"button", ElementType::Button},
    {"landmark", ElementType::Landmark},
}};

}  // namespace

void BrowseMode::showElementsList() {
    std::vector<ElementsDialogEntry> entries;
    elementObjects_.clear();
    ParentMemo memo{root_->registry().backend()};
    for (const ElementKind& kind : kElementKinds) {
        std::vector<a11y::ObjectPtr>& objects = elementObjects_[kind.id];
        for (ElementListItem& item : elementsList(kind.type, 0, memo)) {
            std::string label = std::move(item.label);
            if (kind.type == ElementType::FormField || kind.type == ElementType::Button ||
                kind.type == ElementType::Landmark) {
                // NVDA lists a field as its name and its type, so a row of
                // unnamed fields still says what each one is.
                const char* role = a11y::roleLabel(item.object->role());
                if (role != nullptr && *role != '\0') {
                    label = label.empty() ? std::string{role} : label + " " + role;
                }
            }
            entries.push_back(ElementsDialogEntry{kind.id, item.level, std::move(label)});
            objects.push_back(std::move(item.object));
        }
        // The list opens on the element at the caret, as NVDA's does, rather
        // than on the first one of the page.
        const int initial = initialElementIndex(kind.type, objects);
        if (initial >= 0) {
            const std::size_t first = entries.size() - objects.size();
            entries[first + static_cast<std::size_t>(initial)].atCaret = true;
        }
    }
    const std::weak_ptr<int> alive = lifetime_;
    const bool opened = openElementsDialog(
        entries, lastElementKind_, [this, alive](const ElementsDialogChoice& choice) {
            if (alive.expired()) {
                return;
            }
            chooseElement(choice);
        });
    if (!opened) {
        // Translators: spoken when alus+f7 cannot show the elements list
        // (alus-elements is not installed, or there is no display).
        service_.message(_("the elements list is not available"));
    }
}

void BrowseMode::chooseElement(const ElementsDialogChoice& choice) {
    const auto found = elementObjects_.find(choice.kind);
    if (found == elementObjects_.end() || choice.index < 0 ||
        choice.index >= static_cast<int>(found->second.size())) {
        return;
    }
    lastElementKind_ = choice.kind;
    std::optional<ElementType> type;
    for (const ElementKind& kind : kElementKinds) {
        if (choice.kind == kind.id) {
            type = kind.type;
        }
    }
    pendingElement_ = PendingElement{found->second[static_cast<std::size_t>(choice.index)],
                                     choice.action == "activate", type};
    elementObjects_.clear();
    // Done when the focus comes back to the document, which is when the
    // browser is listening again and when a focus event can no longer move
    // the caret off the element; or a moment later if no focus event comes.
    const std::weak_ptr<int> alive = lifetime_;
    service_.loop().callLater(
        kPendingElementWait,
        [this, alive] {
            if (!alive.expired()) {
                performPendingElement();
            }
        },
        "elementsListChoice");
}

bool BrowseMode::performPendingElement() {
    if (!pendingElement_) {
        return false;
    }
    const PendingElement pending = std::move(*pendingElement_);
    pendingElement_.reset();
    if (pending.object == nullptr || !pending.object->isAlive()) {
        return false;
    }
    if (!pending.activate) {
        // Move to: the caret goes there and the element is read, as a jump.
        moveTo(pending.object, OutputReason::QuickNav, pending.type);
        return true;
    }
    // Activate: on the element first, so activatePosition acts on it and the
    // user is there afterwards.
    if (const a11y::ObjectPtr leaf = DocumentLeaves::leafFor(root_, pending.object)) {
        if (text::TextRangePtr at = DocumentTextRange::makeAt(
                root_, settings_, DocumentPosition{leaf, 0}, DocumentPosition{leaf, 0})) {
            cursor_.setCaret(*at);
        }
    }
    if (pending.object->actionCount() > 0) {
        if (!pending.object->doAction(0)) {
            ALUS_DEBUG("browse mode: action refused on {}", pending.object->path());
        }
    } else {
        pending.object->setFocus();
    }
    if (shouldPassThroughFor(*pending.object)) {
        passThroughWasAutomatic_ = true;
        setPassThrough(true);
    }
    return true;
}

std::vector<ElementListItem> BrowseMode::elementsList(ElementType type, int limit) const {
    ParentMemo memo{root_->registry().backend()};
    return elementsList(type, limit, memo);
}

std::vector<ElementListItem> BrowseMode::elementsList(ElementType type, int limit,
                                                      ParentMemo& memo) const {
    std::vector<ElementListItem> out;
    // N134: an element inside one already listed (YouTube nests a link in a
    // link) is the same entry; listing it made every menu item appear twice.
    // Document order puts the outer one first, so the inner is checked
    // against what is already in the list.
    std::unordered_set<std::string> listed;
    const bool checkNesting = nestingIsSamePlace(type);
    for (const a11y::ObjectPtr& object :
         quickNav_.all(type, limit, includeLayoutTables_)) {
        if (!checkNesting) {
            out.push_back(makeElementListItem(object));
            continue;
        }
        const bool nested = !listed.empty() && hasAncestorIn(*object, listed, memo);
        listed.insert(object->path());
        if (nested) {
            continue;
        }
        out.push_back(makeElementListItem(object));
    }
    return out;
}

int BrowseMode::initialElementIndex(ElementType type,
                                    const std::vector<a11y::ObjectPtr>& listed) const {
    if (listed.empty()) {
        return -1;
    }
    const a11y::ObjectPtr at = objectAtCaret();
    if (at == nullptr) {
        return 0;
    }
    const auto indexOf = [&listed](const a11y::Object& object) {
        for (std::size_t index = 0; index < listed.size(); ++index) {
            if (listed[index] != nullptr && listed[index]->sameAs(object)) {
                return static_cast<int>(index);
            }
        }
        return -1;
    };
    // The innermost listed element `from` is, or is inside; -1 for none.
    const auto listedAround = [this, &indexOf](a11y::ObjectPtr from) {
        constexpr int kMaxDepth = 64;
        for (int depth = 0; depth < kMaxDepth && from != nullptr; ++depth) {
            if (from->sameAs(*root_)) {
                break;
            }
            if (const int index = indexOf(*from); index >= 0) {
                return index;
            }
            from = from->parent();
        }
        return -1;
    };
    // On a link, in a heading or a landmark: that one.
    if (const int around = listedAround(at); around >= 0) {
        return around;
    }
    // Otherwise the one before the next: the next is the quick-nav key's own
    // search, which already answers in reading order, so no position is
    // compared per element (that walks the document's leaves). A nested
    // match that is not listed itself counts as the entry around it.
    const a11y::ObjectPtr next = quickNav_.find(type, at, true, includeLayoutTables_);
    if (next == nullptr) {
        return static_cast<int>(listed.size()) - 1;
    }
    const int nextIndex = listedAround(next);
    return nextIndex > 0 ? nextIndex - 1 : 0;
}

ElementListItem BrowseMode::makeElementListItem(const a11y::ObjectPtr& object) {
    ElementListItem item;
    item.object = object;
    item.label = object->name();
    if (item.label.empty()) {
        // A link with no accessible name is its text, which for a leaf is
        // the text itself -- a list of blank rows is useless.
        item.label = object->textRange(0, -1);
    }
    item.level = object->positionInfo().level;
    return item;
}

bool BrowseMode::nestingIsSamePlace(ElementType type) {
    // A list in a list, a table in a table, a landmark in a landmark are two
    // things; a link in a link or a heading in a heading is one.
    switch (type) {
        case ElementType::Link:
        case ElementType::VisitedLink:
        case ElementType::UnvisitedLink:
        case ElementType::Button:
        case ElementType::Clickable:
        case ElementType::Heading:
        case ElementType::Heading1:
        case ElementType::Heading2:
        case ElementType::Heading3:
        case ElementType::Heading4:
        case ElementType::Heading5:
        case ElementType::Heading6:
        case ElementType::Heading7:
        case ElementType::Heading8:
        case ElementType::Heading9:
            return true;
        default:
            return false;
    }
}

bool BrowseMode::activatePosition() {
    const a11y::ObjectPtr target = objectAtCaret();
    if (target == nullptr) {
        return false;
    }
    if (target->actionCount() > 0) {
        if (!target->doAction(0)) {
            ALUS_DEBUG("browse mode: action refused on {}", target->path());
        }
    } else {
        target->setFocus();
    }
    // Activating something the user must then type into switches modes: NVDA
    // does this and it is why pressing enter on a text box lets you type.
    if (shouldPassThroughFor(*target)) {
        passThroughWasAutomatic_ = true;
        setPassThrough(true);
    }
    return true;
}


// ---------------------------------------------------------------------------
// The bindings -- four loops over four tables, no retyped keystrokes
// ---------------------------------------------------------------------------

void BrowseMode::bindQuickNavScripts() {
    // 37 types x 2 directions = 74 scripts, from one table. Writing them out
    // would be 74 chances to bind the wrong key, and the catalogue already
    // knows every one of them.
    for (const ElementType type : allElementTypes()) {
        const std::string suffix(elementTypeScriptName(type));
        if (suffix.empty()) {
            continue;
        }
        bindScript("next" + suffix,
                   [this, type](input::Gesture&) { moveToElement(type, /*forwards=*/true); });
        bindScript("previous" + suffix,
                   [this, type](input::Gesture&) { moveToElement(type, /*forwards=*/false); });
    }
}

void BrowseMode::bindCursorScripts() {
    for (const CursorRow& row : kCursorRows) {
        bindScript(row.name, [this, row](input::Gesture&) {
            report(cursor_.move(row.unit, row.direction, row.select),
                   row.select ? OutputReason::Change : OutputReason::Caret);
        });
    }

    bindScript("startOfLine", [this](input::Gesture&) {
        report(cursor_.moveToEdge(text::TextUnit::Line, /*toEnd=*/false), OutputReason::Caret);
    });
    bindScript("endOfLine", [this](input::Gesture&) {
        report(cursor_.moveToEdge(text::TextUnit::Line, /*toEnd=*/true), OutputReason::Caret);
    });
    bindScript("selectToBeginningOfLine", [this](input::Gesture&) {
        report(cursor_.moveToEdge(text::TextUnit::Line, false, /*select=*/true),
               OutputReason::Change);
    });
    bindScript("selectToEndOfLine", [this](input::Gesture&) {
        report(cursor_.moveToEdge(text::TextUnit::Line, true, /*select=*/true),
               OutputReason::Change);
    });
    bindScript("topOfDocument", [this](input::Gesture&) {
        report(cursor_.moveToDocumentEdge(/*toEnd=*/false), OutputReason::Caret);
    });
    bindScript("bottomOfDocument", [this](input::Gesture&) {
        report(cursor_.moveToDocumentEdge(/*toEnd=*/true), OutputReason::Caret);
    });
    bindScript("selectToTopOfDocument", [this](input::Gesture&) {
        report(cursor_.moveToDocumentEdge(false, /*select=*/true), OutputReason::Change);
    });
    bindScript("selectToBottomOfDocument", [this](input::Gesture&) {
        report(cursor_.moveToDocumentEdge(true, /*select=*/true), OutputReason::Change);
    });
    bindScript("selectAll", [this](input::Gesture&) {
        const CursorMove move = cursor_.selectAll();
        if (move.toRead != nullptr) {
            // Translators: spoken when the whole document is selected.
            service_.message(_("selected all"));
        }
    });
    bindScript("copyToClipboard", [this](input::Gesture&) {
        text::TextRangePtr selection = cursor_.selection();
        if (selection == nullptr || selection->isCollapsed()) {
            // Translators: spoken when there is nothing selected to copy.
            service_.message(_("no selection"));
            return;
        }
        // The clipboard itself is phase 9 (it needs a Wayland data source);
        // what phase 6 owes is the SELECTION being right, and that is what is
        // reported here so the manual test can hear it.
        service_.message(_("copied"));
        ALUS_INFO("browse mode: copy requested for {} bytes",
                  selection->text().size());
    });
    bindScript("findNext", [this](input::Gesture&) {
        if (!cursor_.findNext(/*reverse=*/false)) {
            // Translators: spoken when a search finds nothing.
            service_.message(_("text not found"));
            return;
        }
        report(CursorMove{true, cursor_.caretRange(), text::TextUnit::Line, false, false,
                          nullptr},
               OutputReason::Caret);
    });
    bindScript("findPrevious", [this](input::Gesture&) {
        if (!cursor_.findNext(/*reverse=*/true)) {
            service_.message(_("text not found"));
            return;
        }
        report(CursorMove{true, cursor_.caretRange(), text::TextUnit::Line, false, false,
                          nullptr},
               OutputReason::Caret);
    });
    bindScript("find", [this](input::Gesture&) {
        // The dialog that asks for the term is phase 9. Until then the key
        // says so rather than doing nothing, which is the difference between
        // an unfinished feature and a broken one.
        // Translators: spoken when a feature is not available yet.
        service_.message(_("find dialog not available yet"));
    });
}

void BrowseMode::bindDocumentScripts() {
    // N148: found in focus mode too, or alus+space could turn focus mode on
    // and never off -- NVDA's script lives in globalCommands, which focus mode
    // does not hide. Turned on by hand, focus mode is the user's: Escape then
    // belongs to the application (NVDA: disableAutoPassThrough).
    script::ScriptSpec toggleSpec;
    toggleSpec.ignoreTreeInterceptorPassThrough = true;
    bindScript(
        "toggleVirtualBufferPassThrough",
        [this](input::Gesture&) {
            passThroughWasAutomatic_ = false;
            setPassThrough(!passThrough());
        },
        toggleSpec);
    // NVDA sets ignoreTreeInterceptorPassThrough on this one script: it is
    // the way OUT of focus mode, so it must be found while keys pass through.
    // Without the flag Escape never left focus mode; outside focus mode
    // alternativeScript releases the key to the application (N144).
    script::ScriptSpec escapeSpec;
    escapeSpec.ignoreTreeInterceptorPassThrough = true;
    bindScript(
        "disablePassThrough",
        [this](input::Gesture&) {
            if (!passThrough() || !passThroughWasAutomatic_) {
                return;
            }
            passThroughWasAutomatic_ = false;
            setPassThrough(false);
        },
        escapeSpec);
    // NVDA: script_refreshBuffer, which unloads and reloads the buffer and
    // then says "Refreshed". ALUS reads the live tree and has no buffer; what
    // can be stale is its caches of names, states and text, so those are
    // dropped and the line at the caret is read again from the application
    // (N145). Before, the key said "not in this version".
    bindScript("refreshBuffer", [this](input::Gesture&) {
        core::invalidateAllCaches();
        // Translators: Reported when a page reloads (example: after
        // refreshing a webpage).
        service_.message(_("Refreshed"));
        if (text::TextRangePtr caret = cursor_.caretRange()) {
            text::TextRangePtr line = caret->copy();
            if (line->expand(text::TextUnit::Line)) {
                service_.speakRange(*line, text::TextUnit::Line, OutputReason::Caret, root_);
            }
        }
    });
    bindScript("activatePosition", [this](input::Gesture&) { activatePosition(); });
    bindScript("elementsList", [this](input::Gesture&) { showElementsList(); });
    bindScript("moveToStartOfContainer", [this](input::Gesture&) {
        const a11y::ObjectPtr at = objectAtCaret();
        if (at == nullptr) {
            return;
        }
        const a11y::ObjectPtr container = at->parent();
        if (container == nullptr || container->sameAs(*root_)) {
            // Translators: spoken when the caret is not inside a container.
            service_.message(_("not in a container"));
            return;
        }
        moveTo(container, OutputReason::QuickNav);
    });
    bindScript("movePastEndOfContainer", [this](input::Gesture&) {
        const a11y::ObjectPtr at = objectAtCaret();
        if (at == nullptr) {
            return;
        }
        const a11y::ObjectPtr container = at->parent();
        if (container == nullptr || container->sameAs(*root_)) {
            service_.message(_("not in a container"));
            return;
        }
        const a11y::ObjectPtr after = DocumentLeaves::next(
            root_, DocumentLeaves::last(container));
        if (after == nullptr) {
            service_.message(_("edge of document"));
            return;
        }
        moveTo(after, OutputReason::QuickNav);
    });
    bindScript("collapseOrExpandControl", [this](input::Gesture&) {
        const a11y::ObjectPtr at = objectAtCaret();
        if (at == nullptr || at->actionCount() == 0) {
            return;
        }
        at->doAction(0);
    });
}

// ---------------------------------------------------------------------------
// Table navigation (documentBase.DocumentWithTableNavigation)
// ---------------------------------------------------------------------------

a11y::ObjectPtr BrowseMode::cellAtCaret() const {
    a11y::ObjectPtr walk = objectAtCaret();
    for (int depth = 0; depth < 32 && walk != nullptr; ++depth) {
        if (walk->sameAs(*root_)) {
            return nullptr;
        }
        switch (walk->role()) {
            case Role::TableCell:
            case Role::TableColumnHeader:
            case Role::TableRowHeader:
                return walk;
            default:
                break;
        }
        walk = walk->parent();
    }
    return nullptr;
}

bool BrowseMode::moveInTable(int rowDelta, int columnDelta, bool toEdge) {
    const a11y::ObjectPtr cell = cellAtCaret();
    if (cell == nullptr) {
        // Translators: spoken when a table command is used outside a table.
        service_.message(_("not in a table"));
        return false;
    }
    const a11y::ObjectPtr table = cell->table();
    if (table == nullptr) {
        service_.message(_("not in a table"));
        return false;
    }
    const a11y::CellInfo& where = cell->cellInfo();
    const a11y::TableInfo& shape = table->tableInfo();
    int row = where.row;
    int column = where.column;
    if (toEdge) {
        if (rowDelta < 0) {
            row = 0;
        } else if (rowDelta > 0) {
            row = shape.rowCount - 1;
        }
        if (columnDelta < 0) {
            column = 0;
        } else if (columnDelta > 0) {
            column = shape.columnCount - 1;
        }
    } else {
        row += rowDelta;
        column += columnDelta;
    }
    if (row < 0 || column < 0 || row >= shape.rowCount || column >= shape.columnCount) {
        // Translators: spoken when a table movement reaches the table's edge.
        service_.message(_("edge of table"));
        return false;
    }
    // AT-SPI answers the cell at coordinates directly, which is why §8.4 says
    // the platform gives all of this: no row/column walking, one call.
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return false;
    }
    const a11y::Ref ref =
        registry->backend().childAt(table->ref(), row * shape.columnCount + column);
    if (ref == 0) {
        service_.message(_("edge of table"));
        return false;
    }
    a11y::ObjectPtr target = registry->objectFor(ref);
    registry->backend().release(ref);
    if (target == nullptr) {
        return false;
    }
    // CARET, not QuickNav: moving between cells SHOULD announce the headers
    // and coordinates, which is exactly what the table-cell branch of
    // getControlFieldSpeech produces. T6.10.
    return moveTo(target, OutputReason::Caret);
}

bool BrowseMode::speakAxis(bool column) {
    const a11y::ObjectPtr cell = cellAtCaret();
    if (cell == nullptr) {
        service_.message(_("not in a table"));
        return false;
    }
    const a11y::ObjectPtr table = cell->table();
    if (table == nullptr) {
        return false;
    }
    const a11y::CellInfo& where = cell->cellInfo();
    const a11y::TableInfo& shape = table->tableInfo();
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return false;
    }
    const int count = column ? shape.rowCount : shape.columnCount;
    std::string spoken;
    for (int index = 0; index < count; ++index) {
        const int row = column ? index : where.row;
        const int col = column ? where.column : index;
        const a11y::Ref ref =
            registry->backend().childAt(table->ref(), row * shape.columnCount + col);
        if (ref == 0) {
            continue;
        }
        a11y::ObjectPtr one = registry->objectFor(ref);
        registry->backend().release(ref);
        if (one == nullptr) {
            continue;
        }
        std::string text = one->name();
        if (text.empty()) {
            text = one->textRange(0, -1);
        }
        if (text.empty()) {
            continue;
        }
        if (!spoken.empty()) {
            spoken += ", ";
        }
        spoken += text;
    }
    if (spoken.empty()) {
        // Translators: spoken when a table row or column has no content.
        service_.message(_("blank"));
        return false;
    }
    service_.message(spoken);
    return true;
}

void BrowseMode::bindTableScripts() {
    bindScript("nextRow", [this](input::Gesture&) { moveInTable(1, 0, false); });
    bindScript("previousRow", [this](input::Gesture&) { moveInTable(-1, 0, false); });
    bindScript("nextColumn", [this](input::Gesture&) { moveInTable(0, 1, false); });
    bindScript("previousColumn", [this](input::Gesture&) { moveInTable(0, -1, false); });
    bindScript("firstRow", [this](input::Gesture&) { moveInTable(-1, 0, true); });
    bindScript("lastRow", [this](input::Gesture&) { moveInTable(1, 0, true); });
    bindScript("firstColumn", [this](input::Gesture&) { moveInTable(0, -1, true); });
    bindScript("lastColumn", [this](input::Gesture&) { moveInTable(0, 1, true); });
    bindScript("speakRow", [this](input::Gesture&) { speakAxis(/*column=*/false); });
    bindScript("speakColumn", [this](input::Gesture&) { speakAxis(/*column=*/true); });
    bindScript("sayAllRow", [this](input::Gesture&) { speakAxis(false); });
    bindScript("sayAllColumn", [this](input::Gesture&) { speakAxis(true); });
    // NVDA: script_toggleScreenLayout (virtualBuffers._toggleScreenLayout).
    bindScript("toggleScreenLayout", [this](input::Gesture&) {
        settings_.useScreenLayout = service_.toggleScreenLayout();
        service_.message(settings_.useScreenLayout
                             // Translators: Presented when use screen layout
                             // option is toggled.
                             ? _("Use screen layout on")
                             // Translators: Presented when use screen layout
                             // option is toggled.
                             : _("Use screen layout off"));
    });
    bindScript("toggleIncludeLayoutTables", [this](input::Gesture&) {
        includeLayoutTables_ = !includeLayoutTables_;
        // Translators: spoken when layout table reporting is turned on or off.
        service_.message(includeLayoutTables_ ? _("layout tables on")
                                          : _("layout tables off"));
    });
}


const script::Script* BrowseMode::alternativeScript(const input::Gesture& gesture,
                                                    const script::Script* found) const {
    // Upstream's order, and each test earns its place:
    //   * in focus mode every key is the application's anyway,
    //   * a key that types no character is a command key and never affected,
    //   * and only then does the switch apply.
    // N144: Escape leaves focus mode, and in browse mode it is the
    // application's -- NVDA sends it on (`gesture.send()`), so Escape closes a
    // Thunderbird message or a dialog. ALUS cannot hand a grabbed key on
    // (N18), so outside focus mode the script is simply not there and the key
    // is not grabbed at all; the grab set follows setPassThrough.
    // N148, NVDA's disableAutoPassThrough: focus mode turned on with
    // alus+space is left with alus+space, and Escape goes to the application
    // -- it leaves full screen, closes a menu. Only focus mode that ALUS
    // entered by itself (the focus landed in a field) is left with Escape.
    if (found != nullptr && found->info != nullptr &&
        found->info->name == "disablePassThrough" &&
        (!passThrough() || !passThroughWasAutomatic_)) {
        return nullptr;
    }
    if (passThrough() || !gesture.isCharacter()) {
        return found;
    }
    if (!singleLetterNav_) {
        return nullptr;
    }
    return found;
}

void BrowseMode::toggleSingleLetterNav(input::Gesture& /*gesture*/) {
    singleLetterNav_ = !singleLetterNav_;
    // The grab set is recomputed for the same reason setPassThrough does it:
    // without this the letters stay grabbed and the switch changes nothing a
    // user can hear or type.
    service_.refreshGrabs();
    service_.message(singleLetterNav_
                         // Translators: reported when the single letters of
                         // browse mode jump between elements again.
                         ? _("Single letter navigation on")
                         // Translators: reported when the single letters of
                         // browse mode are handed to the application instead.
                         : _("Single letter navigation off"));
}


// ---------------------------------------------------------------------------
// `p` / `shift+p`: the next or previous TEXT paragraph.
//
// This is the one quick-navigation type that is NOT a role to match. Every
// other one ("next heading", "next link") is an AT-SPI Collection query and
// costs one round trip; a TEXT paragraph is a paragraph whose TEXT looks like
// prose, so it has to be read to be recognised. Upstream decides it with a
// regular expression over the paragraph's text, and ALUS uses upstream's own
// expression, assembled below from upstream's own pieces -- because what the
// key means to a user ("skip the navigation and the link lists, take me to
// the article") is entirely a property of that expression.
//
// Source: NVDA's config/configDefaults.py, DEFAULT_TEXT_PARAGRAPH_REGEX,
// which is a format string. Its parts, named as upstream names them:
//
//   lookBehind  (?<=\w)(?<!\d)   a word character that is not a digit, so
//                                 "3." in a numbered list is not a sentence
//   optQuote    ["”»)]?           a closing quote, BEFORE or AFTER the full
//                                 stop -- upstream's template has this piece
//                                 twice, which is what makes both `ovako".`
//                                 and `ovako."` the end of a sentence
//   punc        [.…]{1,3}         a full stop, or an ellipsis
//   optWiki     (\[\d+\])*        "[1][2]" footnote markers
//   lookAhead   (?=[\r\n ...]|$)   end of line, or a space
//   punc2       [?!]              a question or an exclamation anywhere
//   cjk         [．！？：；]          the CJK full-width equivalents
// ---------------------------------------------------------------------------

namespace {

// Compiled once. A regular expression with two lookbehinds is not something to
// build on every key press, and this one never changes while ALUS runs.
const text::Regex& textParagraphRegex() {
    static const text::Regex* compiled = [] {
        auto* regex = new text::Regex();
        std::string error;
        if (!regex->compile(kTextParagraphPattern, text::RegexOption::None, &error)) {
            // Said rather than swallowed, and then the command reports that it
            // cannot work instead of silently matching nothing.
            ALUS_ERROR("the text-paragraph pattern did not compile: {}", error);
        }
        return regex;
    }();
    return *compiled;
}

}  // namespace

bool BrowseMode::moveToTextParagraph(bool forwards) {
    const text::Regex& regex = textParagraphRegex();
    text::TextRangePtr range = cursor_.caretRange();
    if (range == nullptr || !regex.valid()) {
        reportNoTextParagraph(forwards);
        return false;
    }

    // The search is done on a COPY of the caret, and the caret is moved only
    // once a paragraph has been found: a failed search must leave the user
    // exactly where they were, which is the difference between "no next text
    // paragraph" and losing your place in the article.
    text::TextRangePtr probe = range->copy();
    for (int step = 0; step < kMaxParagraphSearchLines; ++step) {
        if (probe->move(text::TextUnit::Paragraph, forwards ? 1 : -1) == 0) {
            break;  // the end of the document
        }
        text::TextRangePtr paragraph = probe->copy();
        if (!paragraph->expand(text::TextUnit::Paragraph)) {
            continue;
        }
        if (!regex.matches(paragraph->text())) {
            continue;
        }
        cursor_.setCaret(*probe);
        // QUICKNAV, like every other jump: the ancestor announcements that a
        // caret move makes ("out of list") make navigation slower rather than
        // clearer when the user is jumping (see moveToElement).
        text::TextRangePtr toRead = paragraph->copy();
        service_.speakRange(*toRead, text::TextUnit::Paragraph, a11y::OutputReason::QuickNav,
                            root_);
        return true;
    }
    reportNoTextParagraph(forwards);
    return false;
}

void BrowseMode::reportNoTextParagraph(bool forwards) {
    service_.message(forwards
                         // Translators: reported when there is no next text
                         // paragraph in the document (`p`).
                         ? _("no next text paragraph")
                         // Translators: reported when there is no previous
                         // text paragraph in the document (`shift+p`).
                         : _("no previous text paragraph"));
}

}  // namespace alus::nav
