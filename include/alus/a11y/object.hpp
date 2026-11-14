// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `alus::a11y::Object` -- one accessible, with NVDA's property set.
//
// Port of `NVDAObjects/__init__.py`'s property set, with the AT-SPI sources of
// architecture.md §6.2. Three things about this class are load-bearing:
//
//  1. EVERY PROPERTY IS A `Cached<T>` (§3.5). The getter runs at most once per
//     pump cycle, so the fact that speech, braille and review all ask the same
//     object for `name`, `role` and `states` costs one D-Bus round trip each
//     per cycle instead of three. T4.2 measures it: 30 property reads in one
//     cycle must be one batch of platform calls.
//  2. PROPERTIES GO THROUGH THE BEHAVIOURS FIRST (§6.2). The behaviour list
//     is the composition that replaces NVDA's synthesised overlay class, and
//     the first behaviour with an answer wins.
//  3. IDENTITY IS THE PLATFORM REFERENCE. Two Object instances for the same
//     accessible are never created: Registry hands out the same one, which is
//     what lets the event limiter and the event dispatcher key on `id()`.
//
// THREADING: main thread only (§13).
#pragma once

#include "alus/a11y/backend.hpp"
#include "alus/a11y/behavior.hpp"
#include "alus/a11y/mapping.hpp"
#include "alus/a11y/role.hpp"
#include "alus/a11y/state.hpp"
#include "alus/core/cached.hpp"
#include "alus/core/event_dispatcher.hpp"
#include "alus/script/scriptable.hpp"
#include "alus/text/range.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace alus::a11y {

class Registry;
class Object;

using ObjectPtr = std::shared_ptr<Object>;

// NVDA: `positionInfo` -- "3 of 7, level 2".
struct PositionInfo {
    int indexInGroup = 0;
    int similarItemsInGroup = 0;
    int level = 0;

    bool empty() const noexcept {
        return indexInGroup == 0 && similarItemsInGroup == 0 && level == 0;
    }
};

// NVDA: `presentationType`. Decides whether an object is worth announcing at
// all, and it is why a GTK dialog does not announce four nested panes.
enum class PresentationType { Unavailable, Layout, Content };

const char* presentationTypeName(PresentationType type);

class Object final : public script::ScriptableObject,
                     public std::enable_shared_from_this<Object> {
public:
    // Takes a reference to `ref`; releases it on destruction. Use
    // Registry::objectFor() rather than constructing directly, or object
    // identity is lost.
    Object(Registry& registry, Ref ref);
    ~Object() override;

    // ---- identity ----
    Ref ref() const noexcept { return ref_; }
    core::ObjectId id() const noexcept { return static_cast<core::ObjectId>(ref_); }
    bool sameAs(const Object& other) const noexcept { return ref_ == other.ref_; }
    // False once the application is gone or the object is DEFUNCT. Checked by
    // every setter, and by the focus event path before anything is announced.
    bool isAlive() const;
    const std::string& path() const;

    // ---- the property set (§6.2) ----
    const std::string& name() const;
    const std::string& description() const;
    // The platform's own name and description, before the behaviours and
    // before the LABELLED_BY/DESCRIBED_BY relations. A behaviour that
    // overrides `description` needs this to ask "does the object already say
    // something useful" without recursing into its own override.
    const std::string& platformName() const;
    const std::string& platformDescription() const;
    Role role() const;
    // aria-roledescription: a role name the author gave. When it is set, it
    // REPLACES the role label in speech.
    const std::string& roleText() const;
    const StateSet& states() const;
    const std::string& value() const;
    const std::string& keyboardShortcut() const;
    const Rect& location() const;
    const Attributes& attributes() const;
    InterfaceSet interfaces() const;
    bool hasInterface(Interface one) const { return has(interfaces(), one); }
    // From the `level` attribute: a heading's number, a tree item's depth.
    int level() const;
    const std::string& landmark() const;
    IsCurrent isCurrent() const;
    const std::string& placeholder() const;
    const PositionInfo& positionInfo() const;
    bool isFocusable() const;
    bool hasFocus() const;
    // NVDA: `isProtected` -- a password field. The typed-character echo asks
    // this before it says anything (api::isTypingProtected).
    bool isProtected() const;
    // NVDA: `_hasNavigableText` -- is there text to read, rather than a value.
    bool hasNavigableText() const;
    bool isTextEmpty() const;
    PresentationType presentationType() const;
    bool isPresentableFocusAncestor() const;
    // The ERROR_MESSAGE relation, spoken only when the entry is invalid.
    const std::string& errorMessage() const;
    // The DETAILS relation: "has details", and the roles of what it points to.
    const std::vector<ObjectPtr>& details() const;

    // ---- the tree ----
    ObjectPtr parent() const;
    // The FLOWS_TO relation's first target: where reading continues after
    // this object. LibreOffice links its paragraphs this way, across pages
    // (Orca and NVDA follow it for say-all too). Null when there is none.
    ObjectPtr flowsTo() const;
    // NVDA's `container`: the parent by default, but a behaviour may say
    // otherwise (a table cell's container is its table, not its row).
    ObjectPtr container() const;
    const std::vector<ObjectPtr>& children() const;
    int childCount() const;
    ObjectPtr firstChild() const;
    ObjectPtr lastChild() const;
    ObjectPtr next() const;
    ObjectPtr previous() const;
    int indexInParent() const;
    // The first focusable object in this subtree, depth first. Used by the
    // FocusableUnfocusableContainer behaviour, and by the initial focus scan.
    ObjectPtr firstFocusableDescendant() const;

    // ---- the application ----
    ObjectPtr application() const;
    const AppInfo& appInfo() const;
    int processId() const;
    // Phase 8 fills this in through Registry; null until then.
    script::ScriptableObject* appModule() const;

    // ---- selection ----
    // NVDA: `selectionContainer.getSelectedItemsCount(2)`. The presentation
    // layer needs to know whether exactly one item is selected, and nothing
    // more, so the count is capped (`max`) to keep it one cheap call.
    int selectedItemCount(int max) const;
    ObjectPtr selectionContainer() const;

    // ---- tables ----
    const TableInfo& tableInfo() const;
    const CellInfo& cellInfo() const;
    // NVDA: `cellCoordsText` -- the toolkit's own wording where it has one.
    const std::string& cellCoordsText() const;
    const std::string& rowHeaderText() const;
    const std::string& columnHeaderText() const;
    // The table this cell is in, as an object -- which is what owns the
    // platform reference for the cycle.
    ObjectPtr table() const;
    // NVDA: `tableID`. Identity, not a number to speak: it is how the
    // presentation layer knows it is still in the same table.
    Ref tableId() const;

    // ---- text ----
    int characterCount() const;
    std::string textRange(int start, int end) const;
    int caretOffset() const;
    // NVDA: `makeTextInfo(position)`. A text range over this object's own
    // text, or null when it has no Text interface -- which is the normal
    // answer for most objects and is why every caller tests it.
    //
    // Defined in src/a11y/text_range.cpp, beside the class it builds.
    text::TextRangePtr makeTextRange(text::Position position) const;
    // At an explicit byte range, which is how a bookmark is turned back into a
    // range (say-all and the review cursor both do it every line).
    text::TextRangePtr makeTextRange(int start, int end) const;

    // ---- actions and movement (the setters return bool, §6.4) ----
    int actionCount() const;
    std::string actionName(int index) const;
    bool doAction(int index = 0);
    bool setFocus();
    bool scrollIntoView();

    // ---- links ----
    // Where a link points, as the application reports it. Empty when this
    // object is not a link, or is one whose destination the toolkit does not
    // expose -- which are two different things to the caller and are
    // distinguished with `role()` and State::Linked, exactly as upstream's
    // `script_reportLinkDestination` does.
    //
    // NOT CACHED. Unlike every property above it, this one costs two platform
    // calls and is asked for by exactly one command on exactly one key press,
    // so caching it would buy nothing and hold a reference per link in every
    // document the user walks through.
    std::string linkDestination() const;

    // ---- diagnostics ----
    // NVDA: `NVDAObject.devInfo`. What `alus+f1` writes to the log: the
    // platform's own answers about this object, in the order a bug report
    // needs them. Deliberately NOT translated and deliberately not spoken as
    // a whole: it is for the developer reading the log, which is what
    // upstream uses it for too.
    std::vector<std::string> devInfo() const;

    // ---- events ----
    // Runs the behaviour chain and then the object's own handling. This is
    // level 4 of the event dispatcher (the terminal level).
    void handleEvent(const core::Event& event);

    // ---- behaviours ----
    const std::vector<std::unique_ptr<Behavior>>& behaviors() const noexcept {
        return behaviors_;
    }
    bool hasBehavior(std::string_view className) const;
    // Called by Registry right after construction. Also rebuilds the script
    // class chain, so gesture maps addressing an NVDA behaviour class work,
    // and then lets each behaviour bind its own scripts.
    void setBehaviors(std::vector<std::unique_ptr<Behavior>> behaviors);
    // How a behaviour contributes a script (Behavior::bindScripts). Public
    // only for that: ScriptableObject::bindScript is protected, and a
    // behaviour is not a subclass of this object.
    void bindBehaviorScript(std::string name, script::ScriptFunction function,
                            script::ScriptSpec spec = {});
    // The role BEFORE any behaviour override: the role from the platform plus
    // the attribute refinement of §6.5. This is the key the overlay choosers
    // use, and it has to be reachable without asking the behaviours that are
    // being chosen.
    Role platformRole() const;

    // ---- tree interceptor (phase 6) ----
    script::ScriptableObject* treeInterceptor() const noexcept { return treeInterceptor_; }
    bool treeInterceptorReady() const noexcept { return treeInterceptorReady_; }
    void setTreeInterceptor(script::ScriptableObject* interceptor, bool ready);

    Registry& registry() const noexcept { return registry_; }
    Backend& backend() const noexcept;
    core::CacheScope& cacheScope() noexcept { return scope_; }

    // NVDA: `_speakObjectPropertiesCache`. What was last said about this
    // object, so that OutputReason::Change can say only what changed. Owned
    // here because it must outlive the one-cycle property cache.
    struct SpokenCache {
        bool valid = false;
        std::string name;
        std::string value;
        std::string description;
        Role role = Role::Unknown;
        StateSet states;
    };
    SpokenCache& spokenCache() noexcept { return spoken_; }
    const SpokenCache& spokenCache() const noexcept { return spoken_; }

private:
    ObjectPtr objectFrom(Ref ref) const;
    std::vector<ObjectPtr> objectsFrom(const std::vector<Ref>& refs) const;
    const Refined& refined() const;
    // Asks the behaviours, in order, for an override.
    template <class Answer, class Ask>
    std::optional<Answer> askBehaviors(const Ask& ask) const;

    Registry& registry_;
    Ref ref_;
    core::CacheScope scope_;
    std::vector<std::unique_ptr<Behavior>> behaviors_;
    script::ScriptableObject* treeInterceptor_ = nullptr;
    bool treeInterceptorReady_ = false;
    SpokenCache spoken_;

    // The platform reads. Everything else is derived from these, so the
    // number of these that ran in a cycle IS the cost of that cycle (T4.2).
    core::Cached<std::string> rawName_;
    core::Cached<std::string> rawDescription_;
    core::Cached<PlatformRole> rawRole_;
    core::Cached<std::uint64_t> rawStates_;
    core::Cached<Attributes> rawAttributes_;
    core::Cached<InterfaceSet> rawInterfaces_;
    core::Cached<Rect> rawExtents_;
    core::Cached<int> rawChildCount_;
    core::Cached<int> rawIndexInParent_;
    core::Cached<std::optional<ValueInfo>> rawValue_;
    core::Cached<bool> rawAlive_;
    core::Cached<std::string> rawPath_;
    core::Cached<int> rawActionCount_;
    core::Cached<std::string> rawKeyboardShortcut_;
    core::Cached<TableInfo> rawTableInfo_;
    core::Cached<CellInfo> rawCellInfo_;
    core::Cached<std::string> rawRowHeaderText_;
    core::Cached<std::string> rawColumnHeaderText_;
    core::Cached<AppInfo> rawAppInfo_;
    core::Cached<int> rawCharacterCount_;

    // The derived values, cached as well: `refine()` is pure but not free,
    // and `states()` is asked a dozen times per cycle.
    core::Cached<Refined> refined_;
    core::Cached<std::string> name_;
    core::Cached<std::string> description_;
    core::Cached<std::string> value_;
    core::Cached<Role> role_;
    core::Cached<PositionInfo> positionInfo_;
    core::Cached<PresentationType> presentationType_;
    core::Cached<std::string> errorMessage_;
    core::Cached<std::string> cellCoordsText_;

    // The tree, cached as ObjectPtrs. The cycle parent <-> child that this
    // creates is broken at the end of every pump cycle, when the caches are
    // invalidated (core::invalidateAllCaches).
    core::Cached<ObjectPtr> parent_;
    core::Cached<std::vector<ObjectPtr>> children_;
    core::Cached<ObjectPtr> application_;
    core::Cached<ObjectPtr> table_;
    core::Cached<std::vector<ObjectPtr>> details_;
};

}  // namespace alus::a11y
