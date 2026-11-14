// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The platform boundary of the object model: one interface, every AT-SPI call
// behind it.
//
// architecture.md §6.1 says ALUS has ONE API layer, `alus::a11y` over
// `libatspi`. This header is where that layer meets the platform, and it is an
// interface for one reason that is worth stating plainly: EVERY CALL HERE IS A
// BLOCKING D-BUS ROUND TRIP to another process. That has two consequences
// which shape the whole object model:
//
//   1. The number of calls per pump cycle is the performance model (§3.5), so
//      `Object` wraps every one of them in a `Cached<T>` and the count is
//      measured in the tests (T4.2).
//   2. A frozen application must not freeze the screen reader. The AT-SPI
//      backend bounds the call timeout (atspi_set_timeout) instead of using
//      libatspi's default, which is what phase 3 learned the hard way from
//      N19/N26: a synchronous call with no deadline, made from the main loop,
//      is how a screen reader hangs holding the user's keyboard.
//
// Being an interface also means the entire object model is testable without
// libatspi, a session bus or a desktop: tests/a11y/mock_backend.hpp implements
// it over a hand-built tree, which is the "mock AT-SPI server" the phase 4
// exit criteria ask for.
//
// THREADING: main thread only, like everything else in the object model (§13).
#pragma once

#include "alus/a11y/platform.hpp"
#include "alus/core/geometry.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace alus::a11y {

// An opaque reference to one accessible, owned by the backend. For the AT-SPI
// backend this is an `AtspiAccessible*`, which libatspi guarantees to be
// unique per (bus name, object path) for as long as anyone holds a reference —
// so reference identity IS object identity, and that is what lets the event
// limiter and the event dispatcher key on it (`core::ObjectId`).
//
// 0 means "no object". A reference is counted: whoever gets one from the
// backend owns it and must `unref` it.
using Ref = std::uintptr_t;

// Screen coordinates, as `Component::get_extents(ATSPI_COORD_TYPE_SCREEN)`
// gives them. The shape itself moved to core::Rect in phase 5, because the
// text layer returns rectangles too and screen review sorts by them (§10);
// the name stays here, where phase 4 put it.
using Rect = core::Rect;
using Point = core::Point;

// `Accessible::get_attributes()`. The refinement keys of §6.5 live in here:
// `xml-roles`, `level`, `tag`, `id`, `placeholder-text`, `current`...
using Attributes = std::map<std::string, std::string, std::less<>>;

// Which AT-SPI interfaces an accessible exposes. This is the tertiary key for
// choosing overlay behaviours (§6.2) and the answer to NVDA's
// `hasattr(obj, "IAccessibleTable2Object")`.
enum class Interface : std::uint32_t {
    Accessible = 1U << 0,
    Action = 1U << 1,
    Collection = 1U << 2,
    Component = 1U << 3,
    Document = 1U << 4,
    EditableText = 1U << 5,
    Hypertext = 1U << 6,
    Hyperlink = 1U << 7,
    Image = 1U << 8,
    Selection = 1U << 9,
    Table = 1U << 10,
    TableCell = 1U << 11,
    Text = 1U << 12,
    Value = 1U << 13,
    Application = 1U << 14,
};

using InterfaceSet = std::uint32_t;

constexpr InterfaceSet operator|(Interface a, Interface b) {
    return static_cast<InterfaceSet>(a) | static_cast<InterfaceSet>(b);
}
constexpr InterfaceSet operator|(InterfaceSet a, Interface b) {
    return a | static_cast<InterfaceSet>(b);
}
constexpr InterfaceSet& operator|=(InterfaceSet& set, Interface one) {
    set |= static_cast<InterfaceSet>(one);
    return set;
}
constexpr bool has(InterfaceSet set, Interface one) {
    return (set & static_cast<InterfaceSet>(one)) != 0;
}

// `Value::get_current_value` and friends. Absent when the object has no Value
// interface.
struct ValueInfo {
    double current = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double minimumIncrement = 0.0;
    // `Value::get_text` where the toolkit provides it (GTK4 does for some
    // widgets); empty means "format the number".
    std::string text;
};

// `Table::*` on the table itself.
struct TableInfo {
    int rowCount = 0;
    int columnCount = 0;
    int selectedRowCount = 0;
    int selectedColumnCount = 0;
};

// `TableCell::*` on a cell. NO REFERENCE LIVES IN HERE: the cell's table is
// asked for separately (`Backend::tableOf`), because a reference inside a
// cached struct is a reference nobody owns -- and a table cell is read once
// per cycle for every cell the user moves over.
struct CellInfo {
    int row = -1;
    int column = -1;
    int rowSpan = 1;
    int columnSpan = 1;
};

// What `Application` and `/proc/<pid>` say about the process an object lives
// in. The toolkit name is the PRIMARY key for choosing overlay behaviours and
// app modules on Linux — it replaces NVDA's `windowClassName`, which is a
// Windows concept (§6.2).
struct AppInfo {
    std::string name;
    std::string toolkitName;
    std::string toolkitVersion;
    std::string atspiVersion;
    // The D-Bus name of the application's bus connection. The event limiter
    // keys its per-application cap on this (§3.4).
    std::string busName;
    int processId = 0;
};

// `Text::get_string_at_offset` granularity, and the one place AT-SPI's own
// enum is named. ALUS's text units are richer (text::TextUnit); these are the
// four the platform can answer directly.
enum class TextGranularity { Character, Word, Sentence, Line, Paragraph };

const char* textGranularityName(TextGranularity granularity);

// A piece of text with the offsets it occupies. Used for
// `get_string_at_offset` and for the selection, both of which return a range
// and its contents in one call.
struct TextSegment {
    int start = 0;
    int end = 0;
    std::string text;

    bool empty() const noexcept { return end <= start; }
};

// `Text::get_attribute_run`: the attributes at an offset, and the range they
// hold over. The RANGE IS THE POINT -- it is what lets the format reporting
// walk a line in as many steps as it has distinct formats, instead of one step
// per character.
struct AttributeRun {
    Attributes attributes;
    int start = 0;
    int end = 0;
};

// One hyperlink of a Hypertext: the text offsets it covers, the object it
// embeds and its URI where there is one.
//
// THE OBJECT IS A NEW REFERENCE the caller owns -- the same rule as everywhere
// else here, and the one N44 was about.
struct LinkInfo {
    int startIndex = 0;
    int endIndex = 0;
    Ref object = 0;
    std::string uri;
};

// ---------------------------------------------------------------------------
// Collection: the search service the browse mode is built on
// ---------------------------------------------------------------------------

// `org.a11y.atspi.Collection.MatchRule`. architecture.md §9.1 point 2: this
// is the DIRECT REPLACEMENT for NVDA's `VBuf_findNodeByAttributes`, and the
// reason ALUS does not port the virtual buffers at all. One D-Bus round trip
// answers "the next heading", instead of a tree walk over D-Bus.
//
// THE MATCH TYPES ARE THE POINT. AT-SPI's ANY/ALL/NONE per criterion is what
// makes NVDA's own convention expressible: a quick-nav type is
// "role IS ONE OF these" (ANY over roles), "AND has ALL of these states",
// "AND has NONE of those" -- see `nav::QuickNav`, which is nothing but a
// table of these rules.
enum class MatchType {
    // The criterion is not used at all.
    Invalid,
    // At least one of the listed values is present.
    Any,
    // Every listed value is present.
    All,
    // None of the listed values is present.
    None,
    // Exactly the listed values and no others.
    Empty,
};

// `AtspiCollectionSortOrder`. FLOW is the one that matters and the one T6.6 is
// about: CANONICAL is the order of the TREE, and FLOW is the order of READING.
// A heading that comes later in the document but earlier in the tree is a
// real case (a floated sidebar), and quick navigation that uses CANONICAL
// sends the user backwards.
enum class SortOrder { Invalid, Canonical, Flow, Tab, ReverseCanonical, ReverseFlow, ReverseTab };

// `AtspiCollectionTreeTraversalType`.
enum class TreeTraversal { RestrictChildren, RestrictSibling, InOrder };

struct MatchRule {
    // States the object must (not) have, as a bitset over PlatformState.
    std::uint64_t states = 0;
    MatchType stateMatch = MatchType::Invalid;
    // `Accessible::get_attributes` keys and values. A value may hold several
    // alternatives separated by ':', which is AT-SPI's own convention and is
    // what makes "xml-roles contains any landmark token" one criterion.
    Attributes attributes;
    MatchType attributeMatch = MatchType::Invalid;
    // Roles the object must (not) have.
    std::vector<PlatformRole> roles;
    MatchType roleMatch = MatchType::Invalid;
    // Interface names ("Text", "Table"), as AT-SPI spells them.
    std::vector<std::string> interfaces;
    MatchType interfaceMatch = MatchType::Invalid;
    // AT-SPI's `invert`: the whole rule's sense is flipped.
    bool invert = false;
};

// ---------------------------------------------------------------------------
// The event stream
// ---------------------------------------------------------------------------

// One platform event, as AT-SPI delivers it. The names are AT-SPI's own
// ("object:state-changed:focused"); Registry translates them into the event
// names the rest of ALUS uses ("gainFocus"), because the core knows nothing
// about AT-SPI (§3.3, §3.4).
struct PlatformEvent {
    std::string type;
    // A reference the SINK owns and must release.
    Ref source = 0;
    int detail1 = 0;
    int detail2 = 0;
    // `any_data` when it carries a string: the new name, the inserted text.
    std::string text;
    // `any_data` when it carries an accessible: the new active descendant.
    // A reference the sink owns and must release.
    Ref object = 0;
    // The application the event came from, as an identity to count per -- not
    // a name to show. The event limiter caps events PER APPLICATION (§3.4),
    // and on AT-SPI the cheapest honest identity is the event's `sender`,
    // which the platform hands over for free; asking for the application
    // object instead would be a tree walk per event, and a flood is a thousand
    // events.
    std::uintptr_t application = 0;
};

// Called for every event that survives the platform. Main thread.
using EventSink = std::function<void(PlatformEvent)>;

class Backend {
public:
    virtual ~Backend();

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    // How many platform calls were made so far; 0 where nobody counts. The
    // registry's "slow event" line reports the calls one event cost.
    virtual std::uint64_t callCount() const noexcept { return 0; }

    // A name for the log: "atspi" or "mock".
    virtual const char* backendName() const = 0;

    // ---- reference counting ----
    // Adds a reference and returns the same value, for convenience.
    virtual Ref addRef(Ref ref) = 0;
    virtual void release(Ref ref) = 0;

    // ---- identity and liveness ----
    // False for an object that is gone or marked DEFUNCT. Every property
    // getter may also fail on a dead object; see the error contract below.
    virtual bool alive(Ref ref) = 0;
    // The AT-SPI object path, for the log and for phase 5's bookmarks.
    virtual std::string path(Ref ref) = 0;

    // ---- tree ----
    // Each returns a NEW reference the caller owns (0 when there is none).
    virtual Ref parent(Ref ref) = 0;
    virtual Ref childAt(Ref ref, int index) = 0;
    virtual int childCount(Ref ref) = 0;
    virtual int indexInParent(Ref ref) = 0;

    // ---- properties ----
    virtual std::string name(Ref ref) = 0;
    virtual std::string description(Ref ref) = 0;
    virtual PlatformRole role(Ref ref) = 0;
    // A bitset over PlatformState: bit N is PlatformState(N).
    virtual std::uint64_t states(Ref ref) = 0;
    virtual Attributes attributes(Ref ref) = 0;
    virtual InterfaceSet interfaces(Ref ref) = 0;
    virtual Rect extents(Ref ref) = 0;
    // Each returned reference is a new one the caller owns.
    virtual std::vector<Ref> relation(Ref ref, PlatformRelation relation) = 0;

    // ---- value ----
    virtual std::optional<ValueInfo> value(Ref ref) = 0;

    // ---- text ----
    //
    // EVERY OFFSET HERE IS A UTF-8 BYTE OFFSET, which is what AT-SPI counts in
    // (architecture.md §7.3). The text layer above keeps those offsets
    // unchanged and converts to character indices only where a human-facing
    // count is needed; see include/alus/text/offsets.hpp for why that is the
    // decision and not the other way round.
    virtual int characterCount(Ref ref) = 0;
    // `end` of -1 means "to the end".
    virtual std::string text(Ref ref, int start, int end) = 0;
    virtual int caretOffset(Ref ref) = 0;
    // `Text::set_caret_offset`. False when the toolkit refused, which it does
    // for a read-only widget -- and the review cursor has to cope rather than
    // assume the caret moved.
    virtual bool setCaretOffset(Ref ref, int offset) = 0;
    // `Text::get_string_at_offset`. An unset optional means the toolkit has no
    // answer for this granularity, and the text layer falls back to its own
    // segmentation (§7.2). An EMPTY segment with valid offsets is a different
    // thing and is returned as such: that is an empty line.
    virtual std::optional<TextSegment> stringAtOffset(Ref ref, int offset,
                                                      TextGranularity granularity) = 0;
    // `Text::get_n_selections` and `get_selection(index)`.
    virtual int selectionCount(Ref ref) = 0;
    virtual std::optional<TextSegment> selection(Ref ref, int index) = 0;
    // `Text::set_selection` / `add_selection`. Setting selection 0 when there
    // is none has to add one, which is two different AT-SPI calls; the backend
    // hides that, because every toolkit disagrees about which one works.
    virtual bool setSelection(Ref ref, int start, int end) = 0;
    // `Text::get_attribute_run`. The attributes in force at this offset and
    // the range over which they do not change. `includeDefaults` asks for the
    // object's default attributes to be merged in, which is how a font name
    // that was never overridden is still reported.
    virtual AttributeRun attributeRun(Ref ref, int offset, bool includeDefaults) = 0;
    // `Text::get_offset_at_point`, in screen coordinates. -1 for "not in the
    // text", which is not an error: the mouse is over the widget's border.
    virtual int offsetAtPoint(Ref ref, int x, int y) = 0;
    // `Text::get_character_extents` / `get_range_extents`, screen coordinates.
    virtual Rect characterExtents(Ref ref, int offset) = 0;
    virtual Rect rangeExtents(Ref ref, int start, int end) = 0;

    // ---- hypertext ----
    // The link at a text offset (`Hypertext::get_link_index` -> `get_link`),
    // and the object it points at. This is how a text range answers "what
    // object is at this position" for an embedded link or image (§7.2's
    // `_getNVDAObjectFromOffset`).
    virtual int hyperlinkCount(Ref ref) = 0;
    virtual int linkIndexAtOffset(Ref ref, int offset) = 0;
    virtual std::optional<LinkInfo> hyperlink(Ref ref, int index) = 0;

    // ---- action ----
    virtual int actionCount(Ref ref) = 0;
    virtual std::string actionName(Ref ref, int index) = 0;
    virtual std::string actionKeyBinding(Ref ref, int index) = 0;
    virtual bool doAction(Ref ref, int index) = 0;

    // ---- component ----
    virtual bool grabFocus(Ref ref) = 0;
    virtual bool scrollTo(Ref ref) = 0;
    // The deepest child at this screen point, or 0. A new reference.
    virtual Ref accessibleAtPoint(Ref ref, int x, int y) = 0;

    // ---- selection ----
    virtual int selectedChildCount(Ref ref) = 0;
    virtual Ref selectedChildAt(Ref ref, int index) = 0;

    // ---- table ----
    virtual TableInfo tableInfo(Ref ref) = 0;
    virtual CellInfo cellInfo(Ref ref) = 0;
    // The table a cell belongs to; a new reference the caller owns, 0 when the
    // toolkit does not say.
    virtual Ref tableOf(Ref ref) = 0;
    // Header text of the cell's row and column, already flattened to a string
    // (AT-SPI returns accessibles; the object layer only wants the words).
    virtual std::string rowHeaderText(Ref ref) = 0;
    virtual std::string columnHeaderText(Ref ref) = 0;

    // ---- collection ----
    // `Collection::GetMatchesFrom`. The matches at or after `from` in the
    // given order, at most `count` of them (0 means "no limit"), searching the
    // subtree of `root`. `forwards` is AT-SPI's own `current_match_order`
    // direction argument folded into the sort order.
    //
    // EACH RETURNED REFERENCE IS A NEW ONE THE CALLER OWNS -- the same rule as
    // the tree getters, and the rule N44 was about.
    //
    // An EMPTY RESULT IS NOT AN ERROR: it is "there is no next heading", which
    // is the normal case at the end of a document. A backend with no
    // Collection interface on the root returns empty too, and `QuickNav` then
    // says so to the user rather than walking the tree over D-Bus.
    virtual std::vector<Ref> matchesFrom(Ref root, Ref from, const MatchRule& rule,
                                         SortOrder order, TreeTraversal traversal, int count,
                                         bool forwards) = 0;
    // `Collection::GetMatches`: the matches in the whole subtree, from the
    // beginning. What the elements list (alus+f7) asks for.
    virtual std::vector<Ref> matches(Ref root, const MatchRule& rule, SortOrder order,
                                     int count) = 0;

    // ---- application and desktop ----
    // The application object this one belongs to; a new reference.
    virtual Ref application(Ref ref) = 0;
    virtual AppInfo appInfo(Ref ref) = 0;
    // The one desktop object (AT-SPI: the registry's root). A new reference.
    virtual Ref desktop() = 0;

    // ---- the event stream ----
    // Subscribes to exactly these AT-SPI event types. `properties` in AT-SPI's
    // `RegisterEvent` is how a listener asks for property values to be
    // delivered WITH the event instead of fetched afterwards (§6.1), and the
    // backend passes the set it needs; the sink sees them as
    // PlatformEvent::text. Returns false and fills `error` when the platform
    // refuses, which must be reported rather than swallowed: a screen reader
    // with no events is a screen reader that says nothing.
    virtual bool subscribe(const std::vector<std::string>& eventTypes, EventSink sink,
                           std::string* error) = 0;
    virtual void unsubscribe() = 0;

protected:
    Backend() = default;
};

// THE ERROR CONTRACT. A dead or unreachable object is NOT an exception: it is
// the normal case in a screen reader, because an application can close a
// window between an event arriving and the object being asked about it (§3.8:
// expected errors make no error sound). Getters therefore return an empty or
// zero value, and `alive()` is how a caller distinguishes "empty name" from
// "gone". The one exception is `Object::setFocus()` and the other setters,
// which return bool, because the caller must be able to react (§6.4).

}  // namespace alus::a11y
