// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The mock accessibility platform: a hand-built tree behind the real Backend
// interface.
//
// The phase 4 exit criteria ask for "a mock AT-SPI server in the tests". This
// is that mock, one layer higher than a D-Bus server: it implements
// alus::a11y::Backend, which is the whole of what the object model knows about
// AT-SPI (see backend.hpp). What that buys, and why it is the right layer:
//
//   * every test runs with no session bus, no registry daemon and no desktop,
//     so the suite is the same on a developer's machine and in CI;
//   * EVERY PLATFORM CALL IS COUNTED, which is what makes T4.2 a real
//     measurement rather than an assertion about code shape -- a D-Bus mock
//     could count messages, but not tell a cache hit from a miss;
//   * an object can be KILLED at an exact moment (`kill()`), which is how
//     T4.9 and the focus fallback are tested at all. Racing a real
//     application into dying between an event and a property read is not a
//     test, it is a coin toss.
//
// What it does NOT cover, and what therefore has to be measured live (see
// tests/a11y/test_live_a11y.cpp and tools/phase4-object-check.sh): libatspi's
// own behaviour -- the call timeout, the event delivery, the cache, and
// whether a real toolkit says what this mock assumes it says.
#pragma once

#include "alus/a11y/backend.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace alus::testing {

using namespace alus::a11y;

// The states a healthy, visible control has on AT-SPI. Named because almost
// every node in every test wants them, and because the four inversions in
// src/a11y/state_map.cpp make their ABSENCE meaningful: a node built without
// them is unavailable, invisible and off-screen.
inline std::uint64_t healthyStates() {
    return platformStateBit(PlatformState::Enabled) |
           platformStateBit(PlatformState::Sensitive) |
           platformStateBit(PlatformState::Visible) | platformStateBit(PlatformState::Showing);
}

struct MockNode {
    Ref id = 0;
    Ref parent = 0;
    std::vector<Ref> children;

    std::string name;
    std::string description;
    PlatformRole role = PlatformRole::Unknown;
    std::uint64_t states = healthyStates();
    Attributes attributes;
    InterfaceSet interfaces =
        static_cast<InterfaceSet>(Interface::Accessible) |
        static_cast<InterfaceSet>(Interface::Component);
    Rect extents{0, 0, 100, 20};
    std::optional<ValueInfo> value;
    // Where this node sits in the READING order, when that differs from the
    // tree order. -1 means "same as the tree", which is the usual case; a test
    // that floats a sidebar sets it, and that is what T6.6 measures.
    int flowIndex = -1;

    std::string text;
    int caret = 0;
    // The selection, as Text::get_selection reports it. An empty range means
    // "no selection", which is how every toolkit reports a plain caret.
    int selectionStart = 0;
    int selectionEnd = 0;
    // Whether set_caret_offset and set_selection are allowed to work. A
    // read-only widget refuses both, and the review cursor has to cope.
    bool caretSettable = true;
    bool selectionSettable = true;
    // Which granularities the toolkit answers. The DEFAULT IS NONE, because
    // that is the interesting case: it makes the text layer segment the text
    // itself, which is the code phase 5 has to get right. A test that wants
    // the platform's answer asks for it explicitly.
    bool answersLineGranularity = false;
    bool answersWordGranularity = false;
    bool answersSentenceGranularity = false;
    bool answersParagraphGranularity = false;
    // Text attribute runs, as (startOffset -> attributes). The run extends to
    // the next entry's offset, or to the end of the text.
    std::map<int, Attributes> attributeRuns;
    Attributes defaultTextAttributes;
    // The character box width and height used by characterExtents, so a test
    // can assert per-line bounding rectangles without inventing a font.
    int characterWidth = 8;
    int characterHeight = 16;
    // Hyperlinks, in offset order.
    std::vector<LinkInfo> links;

    std::vector<std::string> actionNames;
    std::vector<std::string> actionBindings;
    // When false, doAction() refuses -- a button that is there and does not
    // work, which happens.
    bool actionsWork = true;
    // When false, grab_focus() refuses.
    bool focusWorks = true;

    std::map<PlatformRelation, std::vector<Ref>> relations;

    TableInfo table;
    CellInfo cell;
    // The table a cell belongs to, as Backend::tableOf answers it.
    Ref table_ = 0;
    std::string rowHeader;
    std::string columnHeader;

    std::vector<Ref> selected;

    // The application this node belongs to; 0 means "ask the parent".
    Ref application = 0;
    AppInfo app;

    // What the node CLAIMS to have, when that differs from what it has. -1
    // means "tell the truth", which is every node but the one case this
    // exists for: a LibreOffice Calc sheet reports 2147483647 children
    // (measured; see kMaxChildren in src/a11y/object.cpp).
    int claimedChildCount = -1;

    // DEFUNCT, or gone entirely.
    bool dead = false;
    // Outstanding references, so a test can assert the object model gives
    // every one of them back (which is what makes the flood test a leak test).
    int refs = 0;
};

class MockBackend final : public Backend {
public:
    MockBackend() {
        // The desktop, as AT-SPI has exactly one.
        desktop_ = add(0, PlatformRole::DesktopFrame, "main");
        node(desktop_).app.name = "desktop";
    }

    const char* backendName() const override { return "mock"; }

    // ---- building a tree ----
    Ref add(Ref parent, PlatformRole role, std::string name = {}) {
        const Ref id = nextId_++;
        MockNode created;
        created.id = id;
        created.parent = parent;
        created.role = role;
        created.name = std::move(name);
        nodes_.emplace(id, std::move(created));
        if (parent != 0) {
            nodes_.at(parent).children.push_back(id);
        }
        return id;
    }

    // An application node with a toolkit name: the primary key for choosing
    // overlay behaviours and app modules on Linux (§6.2).
    Ref addApplication(std::string name, std::string toolkit = "gtk") {
        const Ref id = add(desktop_, PlatformRole::Application, name);
        MockNode& created = node(id);
        created.interfaces |= Interface::Application;
        created.app.name = std::move(name);
        created.app.toolkitName = std::move(toolkit);
        created.app.toolkitVersion = "4.18.0";
        // A D-Bus connection name, because the real one always has one and
        // phase 6's node identity is (bus name, object path): a mock with no
        // bus name would let a uniqueID that used only the path pass (T6.4).
        created.app.busName = ":1." + std::to_string(id);
        created.app.processId = 4242;
        created.application = id;
        return id;
    }

    MockNode& node(Ref ref) { return nodes_.at(ref); }
    const MockNode& node(Ref ref) const { return nodes_.at(ref); }
    bool exists(Ref ref) const { return nodes_.find(ref) != nodes_.end(); }

    void addState(Ref ref, PlatformState state) {
        node(ref).states |= platformStateBit(state);
    }
    void removeState(Ref ref, PlatformState state) {
        node(ref).states &= ~platformStateBit(state);
    }
    void addInterface(Ref ref, Interface one) { node(ref).interfaces |= one; }

    // The object is now DEFUNCT: every getter answers emptily and alive() is
    // false. This is the moment T4.9 needs to be able to choose.
    void kill(Ref ref) {
        MockNode& target = node(ref);
        target.dead = true;
        target.states = platformStateBit(PlatformState::Defunct);
    }

    Ref desktopRef() const noexcept { return desktop_; }

    // ---- the event stream ----
    // Hands an event to whatever subscribed, exactly as libatspi would. The
    // references are added here, because the sink owns them (see
    // PlatformEvent).
    void emit(const std::string& type, Ref source, int detail1 = 0, int detail2 = 0,
              std::string text = {}, Ref related = 0) {
        if (!sink_) {
            return;
        }
        PlatformEvent event;
        event.type = type;
        event.source = source != 0 ? addRef(source) : 0;
        event.detail1 = detail1;
        event.detail2 = detail2;
        event.text = std::move(text);
        event.object = related != 0 ? addRef(related) : 0;
        event.application = applicationOf(source);
        sink_(std::move(event));
    }

    const std::vector<std::string>& subscribedTypes() const noexcept { return subscribed_; }
    bool subscribeFails = false;

    // ---- the meters ----
    // Every call that would have been a D-Bus round trip. T4.2 is this number.
    std::size_t calls() const noexcept { return calls_; }
    std::size_t callsFor(const std::string& what) const {
        const auto it = perCall_.find(what);
        return it == perCall_.end() ? 0 : it->second;
    }
    // The same, for ONE object. Needed because a property of one object is
    // often read by asking another (positionInfo asks the parent for its
    // child count), and a global counter cannot tell that from a cache miss.
    std::size_t callsFor(Ref ref, const std::string& what) const {
        const auto it = perObject_.find(std::make_pair(ref, what));
        return it == perObject_.end() ? 0 : it->second;
    }
    void resetCalls() {
        calls_ = 0;
        perCall_.clear();
        perObject_.clear();
    }
    // The sum of outstanding references. Zero after the object model has let
    // go of everything.
    int outstandingRefs() const {
        int total = 0;
        for (const auto& [id, entry] : nodes_) {
            total += entry.refs;
        }
        return total;
    }

    // ---- Backend ----
    Ref addRef(Ref ref) override {
        if (ref != 0 && exists(ref)) {
            ++node(ref).refs;
        }
        return ref;
    }

    void release(Ref ref) override {
        if (ref != 0 && exists(ref)) {
            --node(ref).refs;
        }
    }

    bool alive(Ref ref) override {
        note("alive", ref);
        return ref != 0 && exists(ref) && !node(ref).dead;
    }

    std::string path(Ref ref) override {
        note("path", ref);
        return "/org/a11y/atspi/accessible/" + std::to_string(ref);
    }

    Ref parent(Ref ref) override {
        note("parent", ref);
        if (!valid(ref)) {
            return 0;
        }
        return addRef(node(ref).parent);
    }

    Ref childAt(Ref ref, int index) override {
        note("childAt", ref);
        if (!valid(ref) || index < 0) {
            return 0;
        }
        const std::vector<Ref>& children = node(ref).children;
        if (static_cast<std::size_t>(index) >= children.size()) {
            return 0;
        }
        return addRef(children[static_cast<std::size_t>(index)]);
    }

    int childCount(Ref ref) override {
        note("childCount", ref);
        if (!valid(ref)) {
            return 0;
        }
        const MockNode& target = node(ref);
        return target.claimedChildCount >= 0 ? target.claimedChildCount
                                             : static_cast<int>(target.children.size());
    }

    int indexInParent(Ref ref) override {
        note("indexInParent", ref);
        if (!valid(ref)) {
            return -1;
        }
        const Ref parentRef = node(ref).parent;
        if (parentRef == 0 || !exists(parentRef)) {
            return -1;
        }
        const std::vector<Ref>& siblings = node(parentRef).children;
        const auto it = std::find(siblings.begin(), siblings.end(), ref);
        return it == siblings.end() ? -1 : static_cast<int>(it - siblings.begin());
    }

    std::string name(Ref ref) override {
        note("name", ref);
        return valid(ref) ? node(ref).name : std::string{};
    }

    std::string description(Ref ref) override {
        note("description", ref);
        return valid(ref) ? node(ref).description : std::string{};
    }

    PlatformRole role(Ref ref) override {
        note("role", ref);
        return valid(ref) ? node(ref).role : PlatformRole::Invalid;
    }

    std::uint64_t states(Ref ref) override {
        note("states", ref);
        if (!exists(ref)) {
            return 0;
        }
        return node(ref).states;
    }

    Attributes attributes(Ref ref) override {
        note("attributes", ref);
        return valid(ref) ? node(ref).attributes : Attributes{};
    }

    InterfaceSet interfaces(Ref ref) override {
        note("interfaces", ref);
        return valid(ref) ? node(ref).interfaces : 0;
    }

    Rect extents(Ref ref) override {
        note("extents", ref);
        return valid(ref) ? node(ref).extents : Rect{};
    }

    std::vector<Ref> relation(Ref ref, PlatformRelation wanted) override {
        note("relation", ref);
        std::vector<Ref> result;
        if (!valid(ref)) {
            return result;
        }
        const auto it = node(ref).relations.find(wanted);
        if (it == node(ref).relations.end()) {
            return result;
        }
        for (const Ref target : it->second) {
            result.push_back(addRef(target));
        }
        return result;
    }

    std::optional<ValueInfo> value(Ref ref) override {
        note("value", ref);
        if (!valid(ref)) {
            return std::nullopt;
        }
        return node(ref).value;
    }

    // EVERY OFFSET IN AND OUT OF THIS MOCK IS A CODEPOINT, as it is in
    // AT-SPI -- measured on Chrome / AT-SPI 2.56.2 (FAZA10.md N113). The text
    // is stored as UTF-8 and converted at the edge.
    //
    // It counted BYTES until N113, and that is how the defect survived: the
    // text layer also counted bytes, so every test with a Serbian letter in it
    // was a byte-counting mock agreeing with byte-counting code, while the
    // real platform answered in characters.
    static int codepoints(const std::string& text) {
        return static_cast<int>(text::OffsetConverter(text).codepointLength());
    }
    static std::size_t toByte(const std::string& text, int codepoint) {
        return text::OffsetConverter(text).toByte(
            static_cast<std::size_t>(std::max(0, codepoint)));
    }
    static int toCodepoint(const std::string& text, std::size_t byte) {
        return static_cast<int>(text::OffsetConverter(text).toCodepoint(byte));
    }

    int characterCount(Ref ref) override {
        note("characterCount", ref);
        return valid(ref) ? codepoints(node(ref).text) : 0;
    }

    std::string text(Ref ref, int start, int end) override {
        note("text", ref);
        if (!valid(ref)) {
            return {};
        }
        const std::string& whole = node(ref).text;
        if (start < 0 || start > codepoints(whole)) {
            return {};
        }
        const std::size_t from = toByte(whole, start);
        const std::size_t to = end < 0 ? whole.size() : toByte(whole, end);
        return to > from ? whole.substr(from, to - from) : std::string{};
    }

    int caretOffset(Ref ref) override {
        note("caretOffset", ref);
        return valid(ref) ? node(ref).caret : -1;
    }

    bool setCaretOffset(Ref ref, int offset) override {
        note("setCaretOffset", ref);
        if (!valid(ref) || !node(ref).caretSettable) {
            return false;
        }
        MockNode& target = node(ref);
        if (offset < 0 || offset > codepoints(target.text)) {
            return false;
        }
        target.caret = offset;
        caretsSet.push_back({ref, offset});
        return true;
    }

    std::optional<TextSegment> stringAtOffset(Ref ref, int offset,
                                              TextGranularity granularity) override {
        note("stringAtOffset", ref);
        if (!valid(ref)) {
            return std::nullopt;
        }
        const MockNode& target = node(ref);
        const bool answers = granularity == TextGranularity::Line
                                 ? target.answersLineGranularity
                                 : granularity == TextGranularity::Word
                                       ? target.answersWordGranularity
                                       : granularity == TextGranularity::Sentence
                                             ? target.answersSentenceGranularity
                                             : granularity == TextGranularity::Paragraph
                                                   ? target.answersParagraphGranularity
                                                   : false;
        if (!answers) {
            return std::nullopt;
        }
        const std::string& whole = target.text;
        if (offset < 0 || offset > codepoints(whole)) {
            return std::nullopt;
        }
        // Lines and paragraphs break on \n; words on whitespace; sentences on
        // ". ". Deliberately simple, and deliberately DIFFERENT from what the
        // text layer would compute, so a test can tell which answer was used.
        std::size_t position = toByte(whole, offset);
        if (position >= whole.size() && !whole.empty()) {
            position = whole.size() - 1;
        }
        std::size_t start = 0;
        std::size_t end = whole.size();
        if (granularity == TextGranularity::Line ||
            granularity == TextGranularity::Paragraph) {
            const std::size_t previous =
                position == 0 ? std::string::npos : whole.rfind('\n', position - 1);
            start = previous == std::string::npos ? 0 : previous + 1;
            const std::size_t next = whole.find('\n', position);
            end = next == std::string::npos ? whole.size() : next + 1;
        } else if (granularity == TextGranularity::Word) {
            start = whole.find_last_of(" \t\n", position == 0 ? 0 : position - 1);
            start = start == std::string::npos ? 0 : start + 1;
            const std::size_t next = whole.find_first_of(" \t\n", position);
            end = next == std::string::npos ? whole.size() : next + 1;
        } else if (granularity == TextGranularity::Sentence) {
            const std::size_t previous =
                position == 0 ? std::string::npos : whole.rfind(". ", position - 1);
            start = previous == std::string::npos ? 0 : previous + 2;
            const std::size_t next = whole.find(". ", position);
            end = next == std::string::npos ? whole.size() : next + 2;
        }
        TextSegment segment;
        segment.start = toCodepoint(whole, start);
        segment.end = toCodepoint(whole, end);
        segment.text = whole.substr(start, end - start);
        return segment;
    }

    int selectionCount(Ref ref) override {
        note("selectionCount", ref);
        if (!valid(ref)) {
            return 0;
        }
        return node(ref).selectionEnd > node(ref).selectionStart ? 1 : 0;
    }

    std::optional<TextSegment> selection(Ref ref, int index) override {
        note("selection", ref);
        if (!valid(ref) || index != 0) {
            return std::nullopt;
        }
        const MockNode& target = node(ref);
        if (target.selectionEnd <= target.selectionStart) {
            // No selection: AT-SPI reports the caret as an empty range, and
            // the text layer relies on that to make a collapsed range.
            return TextSegment{target.caret, target.caret, {}};
        }
        TextSegment segment;
        segment.start = target.selectionStart;
        segment.end = target.selectionEnd;
        const std::size_t from = toByte(target.text, segment.start);
        const std::size_t to = toByte(target.text, segment.end);
        segment.text = target.text.substr(from, to > from ? to - from : 0);
        return segment;
    }

    bool setSelection(Ref ref, int start, int end) override {
        note("setSelection", ref);
        if (!valid(ref) || !node(ref).selectionSettable) {
            return false;
        }
        node(ref).selectionStart = start;
        node(ref).selectionEnd = end;
        selectionsSet.push_back({ref, start, end});
        return true;
    }

    AttributeRun attributeRun(Ref ref, int offset, bool includeDefaults) override {
        note("attributeRun", ref);
        AttributeRun run;
        if (!valid(ref)) {
            return run;
        }
        const MockNode& target = node(ref);
        run.start = 0;
        run.end = codepoints(target.text);
        if (includeDefaults) {
            run.attributes = target.defaultTextAttributes;
        }
        for (auto it = target.attributeRuns.begin(); it != target.attributeRuns.end(); ++it) {
            if (it->first > offset) {
                run.end = it->first;
                break;
            }
            run.start = it->first;
            for (const auto& [key, value] : it->second) {
                run.attributes[key] = value;
            }
        }
        return run;
    }

    int offsetAtPoint(Ref ref, int x, int y) override {
        note("offsetAtPoint", ref);
        if (!valid(ref)) {
            return -1;
        }
        // One line of fixed-width characters per text line, laid out from the
        // object's own extents, so a test can address a point and get back an
        // offset without a font.
        const MockNode& target = node(ref);
        const int relativeX = x - target.extents.x;
        const int relativeY = y - target.extents.y;
        if (relativeX < 0 || relativeY < 0 || target.characterWidth <= 0 ||
            target.characterHeight <= 0) {
            return -1;
        }
        const int line = relativeY / target.characterHeight;
        const int column = relativeX / target.characterWidth;
        int lineStart = 0;
        for (int index = 0; index < line; ++index) {
            const std::size_t next = target.text.find('\n', static_cast<std::size_t>(lineStart));
            if (next == std::string::npos) {
                return -1;
            }
            lineStart = static_cast<int>(next) + 1;
        }
        const int offset = toCodepoint(target.text, static_cast<std::size_t>(lineStart)) + column;
        return offset <= codepoints(target.text) ? offset : -1;
    }

    Rect characterExtents(Ref ref, int offset) override {
        note("characterExtents", ref);
        if (!valid(ref)) {
            return Rect{};
        }
        const MockNode& target = node(ref);
        if (offset < 0 || offset > codepoints(target.text)) {
            return Rect{};
        }
        int line = 0;
        int column = 0;
        const std::size_t limit = toByte(target.text, offset);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto byte = static_cast<unsigned char>(target.text[index]);
            if ((byte & 0xC0U) == 0x80U) {
                continue;  // a continuation byte: not a character of its own
            }
            if (byte == '\n') {
                ++line;
                column = 0;
            } else {
                ++column;
            }
        }
        return Rect{target.extents.x + column * target.characterWidth,
                    target.extents.y + line * target.characterHeight, target.characterWidth,
                    target.characterHeight};
    }

    Rect rangeExtents(Ref ref, int start, int end) override {
        note("rangeExtents", ref);
        const Rect first = characterExtents(ref, start);
        const Rect last = characterExtents(ref, end > start ? end - 1 : start);
        return first.united(last);
    }

    int hyperlinkCount(Ref ref) override {
        note("hyperlinkCount", ref);
        return valid(ref) ? static_cast<int>(node(ref).links.size()) : 0;
    }

    int linkIndexAtOffset(Ref ref, int offset) override {
        note("linkIndexAtOffset", ref);
        if (!valid(ref)) {
            return -1;
        }
        const std::vector<LinkInfo>& links = node(ref).links;
        for (std::size_t index = 0; index < links.size(); ++index) {
            if (offset >= links[index].startIndex && offset < links[index].endIndex) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    std::optional<LinkInfo> hyperlink(Ref ref, int index) override {
        note("hyperlink", ref);
        if (!valid(ref) || index < 0) {
            return std::nullopt;
        }
        const std::vector<LinkInfo>& links = node(ref).links;
        if (static_cast<std::size_t>(index) >= links.size()) {
            return std::nullopt;
        }
        LinkInfo info = links[static_cast<std::size_t>(index)];
        // A NEW reference, as the contract says -- and counted, so a test can
        // assert the text layer gives it back (N44).
        if (info.object != 0) {
            addRef(info.object);
        }
        return info;
    }

    int actionCount(Ref ref) override {
        note("actionCount", ref);
        return valid(ref) ? static_cast<int>(node(ref).actionNames.size()) : 0;
    }

    std::string actionName(Ref ref, int index) override {
        note("actionName", ref);
        if (!valid(ref) || index < 0) {
            return {};
        }
        const std::vector<std::string>& names = node(ref).actionNames;
        return static_cast<std::size_t>(index) < names.size()
                   ? names[static_cast<std::size_t>(index)]
                   : std::string{};
    }

    std::string actionKeyBinding(Ref ref, int index) override {
        note("actionKeyBinding", ref);
        if (!valid(ref) || index < 0) {
            return {};
        }
        const std::vector<std::string>& bindings = node(ref).actionBindings;
        return static_cast<std::size_t>(index) < bindings.size()
                   ? bindings[static_cast<std::size_t>(index)]
                   : std::string{};
    }

    bool doAction(Ref ref, int index) override {
        note("doAction", ref);
        if (!valid(ref) || !node(ref).actionsWork) {
            return false;
        }
        if (index < 0 || static_cast<std::size_t>(index) >= node(ref).actionNames.size()) {
            return false;
        }
        performed.push_back({ref, index});
        return true;
    }

    bool grabFocus(Ref ref) override {
        note("grabFocus", ref);
        if (!valid(ref) || !node(ref).focusWorks) {
            return false;
        }
        focused.push_back(ref);
        return true;
    }

    bool scrollTo(Ref ref) override {
        note("scrollTo", ref);
        if (!valid(ref)) {
            return false;
        }
        scrolled.push_back(ref);
        return true;
    }

    Ref accessibleAtPoint(Ref ref, int x, int y) override {
        note("accessibleAtPoint", ref);
        if (!valid(ref)) {
            return 0;
        }
        // Depth first, deepest match wins, as AT-SPI does it.
        for (const Ref child : node(ref).children) {
            if (!exists(child)) {
                continue;
            }
            if (node(child).extents.contains(x, y)) {
                const Ref deeper = accessibleAtPoint(child, x, y);
                return deeper != 0 ? deeper : addRef(child);
            }
        }
        return 0;
    }

    int selectedChildCount(Ref ref) override {
        note("selectedChildCount", ref);
        return valid(ref) ? static_cast<int>(node(ref).selected.size()) : 0;
    }

    Ref selectedChildAt(Ref ref, int index) override {
        note("selectedChildAt", ref);
        if (!valid(ref) || index < 0) {
            return 0;
        }
        const std::vector<Ref>& selection = node(ref).selected;
        if (static_cast<std::size_t>(index) >= selection.size()) {
            return 0;
        }
        return addRef(selection[static_cast<std::size_t>(index)]);
    }

    TableInfo tableInfo(Ref ref) override {
        note("tableInfo", ref);
        return valid(ref) ? node(ref).table : TableInfo{};
    }

    CellInfo cellInfo(Ref ref) override {
        note("cellInfo", ref);
        return valid(ref) ? node(ref).cell : CellInfo{};
    }

    Ref tableOf(Ref ref) override {
        note("tableOf", ref);
        if (!valid(ref) || node(ref).table_ == 0) {
            return 0;
        }
        return addRef(node(ref).table_);
    }

    std::string rowHeaderText(Ref ref) override {
        note("rowHeaderText", ref);
        return valid(ref) ? node(ref).rowHeader : std::string{};
    }

    std::string columnHeaderText(Ref ref) override {
        note("columnHeaderText", ref);
        return valid(ref) ? node(ref).columnHeader : std::string{};
    }

    // ---- collection ----
    //
    // A REAL implementation, not a stub, and that is the point: the quick
    // navigation of phase 6 is nothing but a table of MatchRules, and a mock
    // that always answered "no match" would let every one of those 22 rules be
    // wrong without a test noticing. So the rule is evaluated here with
    // AT-SPI's own ANY/ALL/NONE semantics, over the subtree flattened in
    // document order -- which is also what makes `collectionOrder` below able
    // to answer FLOW differently from CANONICAL (T6.6).
    std::vector<Ref> matchesFrom(Ref root, Ref from, const MatchRule& rule, SortOrder order,
                                 TreeTraversal traversal, int count, bool forwards) override {
        note("matchesFrom", root);
        (void)traversal;
        if (!valid(root) || !exists(from)) {
            return {};
        }
        if (!has(node(root).interfaces, Interface::Collection)) {
            return {};
        }
        std::vector<Ref> order_ = flattenFor(root, order);
        const auto start = std::find(order_.begin(), order_.end(), from);
        std::vector<Ref> result;
        if (start == order_.end()) {
            return result;
        }
        // AT-SPI excludes the object itself from both directions.
        if (forwards) {
            for (auto it = start + 1; it != order_.end(); ++it) {
                if (!collect(result, *it, rule, count)) {
                    break;
                }
            }
        } else {
            for (auto it = std::make_reverse_iterator(start); it != order_.rend(); ++it) {
                if (!collect(result, *it, rule, count)) {
                    break;
                }
            }
        }
        return result;
    }

    std::vector<Ref> matches(Ref root, const MatchRule& rule, SortOrder order,
                             int count) override {
        note("matches", root);
        if (!valid(root) || !has(node(root).interfaces, Interface::Collection)) {
            return {};
        }
        std::vector<Ref> result;
        for (const Ref ref : flattenFor(root, order)) {
            if (ref == root) {
                continue;
            }
            if (!collect(result, ref, rule, count)) {
                break;
            }
        }
        return result;
    }

    // Does this node satisfy the rule? One criterion at a time, each with its
    // own match type, exactly as `org.a11y.atspi.Collection` defines them.
    bool matchesRule(Ref ref, const MatchRule& rule) const {
        const auto found = nodes_.find(ref);
        if (found == nodes_.end()) {
            return false;
        }
        const MockNode& subject = found->second;
        bool ok = true;
        if (rule.roleMatch != MatchType::Invalid && !rule.roles.empty()) {
            const bool present = std::find(rule.roles.begin(), rule.roles.end(),
                                           subject.role) != rule.roles.end();
            switch (rule.roleMatch) {
                case MatchType::Any:
                case MatchType::All:
                    ok = ok && present;
                    break;
                case MatchType::None:
                    ok = ok && !present;
                    break;
                default:
                    break;
            }
        }
        if (rule.stateMatch != MatchType::Invalid && rule.states != 0) {
            const std::uint64_t have = subject.states & rule.states;
            switch (rule.stateMatch) {
                case MatchType::Any:
                    ok = ok && have != 0;
                    break;
                case MatchType::All:
                    ok = ok && have == rule.states;
                    break;
                case MatchType::None:
                    ok = ok && have == 0;
                    break;
                default:
                    break;
            }
        }
        if (rule.attributeMatch != MatchType::Invalid && !rule.attributes.empty()) {
            std::size_t hits = 0;
            for (const auto& [key, wanted] : rule.attributes) {
                const auto it = subject.attributes.find(key);
                if (it == subject.attributes.end()) {
                    continue;
                }
                // AT-SPI's own convention: alternatives separated by ':'.
                bool hit = false;
                std::size_t at = 0;
                while (at <= wanted.size()) {
                    const std::size_t next = wanted.find(':', at);
                    const std::string one =
                        wanted.substr(at, next == std::string::npos ? std::string::npos
                                                                    : next - at);
                    if (!one.empty() && it->second == one) {
                        hit = true;
                        break;
                    }
                    if (next == std::string::npos) {
                        break;
                    }
                    at = next + 1;
                }
                if (hit) {
                    ++hits;
                }
            }
            switch (rule.attributeMatch) {
                case MatchType::Any:
                    ok = ok && hits > 0;
                    break;
                case MatchType::All:
                    ok = ok && hits == rule.attributes.size();
                    break;
                case MatchType::None:
                    ok = ok && hits == 0;
                    break;
                default:
                    break;
            }
        }
        if (rule.interfaceMatch != MatchType::Invalid && !rule.interfaces.empty()) {
            std::size_t hits = 0;
            for (const std::string& name : rule.interfaces) {
                if (hasInterfaceNamed(ref, name)) {
                    ++hits;
                }
            }
            switch (rule.interfaceMatch) {
                case MatchType::Any:
                    ok = ok && hits > 0;
                    break;
                case MatchType::All:
                    ok = ok && hits == rule.interfaces.size();
                    break;
                case MatchType::None:
                    ok = ok && hits == 0;
                    break;
                default:
                    break;
            }
        }
        return rule.invert ? !ok : ok;
    }

    Ref application(Ref ref) override {
        note("application", ref);
        const Ref app = applicationRefOf(ref);
        return app != 0 ? addRef(app) : 0;
    }

    AppInfo appInfo(Ref ref) override {
        note("appInfo", ref);
        const Ref app = applicationRefOf(ref);
        return app != 0 ? node(app).app : AppInfo{};
    }

    Ref desktop() override {
        note("desktop", desktop_);
        return addRef(desktop_);
    }

    bool subscribe(const std::vector<std::string>& eventTypes, EventSink sink,
                   std::string* error) override {
        if (subscribeFails) {
            if (error != nullptr) {
                *error = "the mock was told to refuse";
            }
            return false;
        }
        subscribed_ = eventTypes;
        sink_ = std::move(sink);
        return true;
    }

    void unsubscribe() override {
        subscribed_.clear();
        sink_ = nullptr;
    }

    // What the object model did, for the assertions.
    struct Action {
        Ref ref;
        int index;
    };
    std::vector<Action> performed;
    std::vector<Ref> focused;
    std::vector<Ref> scrolled;
    // What the text layer asked the platform to do, so a test can assert that
    // `updateCaret()` really reached the toolkit.
    struct CaretSet {
        Ref ref = 0;
        int offset = 0;
    };
    struct SelectionSet {
        Ref ref = 0;
        int start = 0;
        int end = 0;
    };
    std::vector<CaretSet> caretsSet;
    std::vector<SelectionSet> selectionsSet;

private:
    bool valid(Ref ref) { return ref != 0 && exists(ref) && !node(ref).dead; }

    void note(const char* what, Ref ref = 0) {
        ++calls_;
        ++perCall_[what];
        ++perObject_[std::make_pair(ref, std::string{what})];
    }

    // The subtree of `root`, in tree order (AT-SPI's CANONICAL), depth first.
    void flattenInto(Ref ref, std::vector<Ref>& out) const {
        out.push_back(ref);
        const auto it = nodes_.find(ref);
        if (it == nodes_.end()) {
            return;
        }
        for (const Ref child : it->second.children) {
            if (nodes_.count(child) != 0) {
                flattenInto(child, out);
            }
        }
    }

    // The same subtree in the order the sort asks for. FLOW differs from
    // CANONICAL exactly where a node carries a `flowIndex`.
    std::vector<Ref> flattenFor(Ref root, SortOrder order) const {
        std::vector<Ref> out;
        flattenInto(root, out);
        const bool flow = order == SortOrder::Flow || order == SortOrder::ReverseFlow;
        if (flow) {
            // A stable sort on the reading index, with the tree position as
            // the fallback for every node that has none.
            std::vector<std::pair<int, Ref>> keyed;
            keyed.reserve(out.size());
            for (std::size_t index = 0; index < out.size(); ++index) {
                const auto it = nodes_.find(out[index]);
                const int key = (it != nodes_.end() && it->second.flowIndex >= 0)
                                    ? it->second.flowIndex
                                    : static_cast<int>(index);
                keyed.emplace_back(key, out[index]);
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            out.clear();
            for (const auto& [key, ref] : keyed) {
                out.push_back(ref);
            }
        }
        if (order == SortOrder::ReverseCanonical || order == SortOrder::ReverseFlow ||
            order == SortOrder::ReverseTab) {
            std::reverse(out.begin(), out.end());
        }
        return out;
    }

    bool hasInterfaceNamed(Ref ref, const std::string& name) const {
        static const std::map<std::string, Interface, std::less<>> kByName{
            {"Action", Interface::Action},     {"Collection", Interface::Collection},
            {"Component", Interface::Component}, {"Document", Interface::Document},
            {"EditableText", Interface::EditableText}, {"Hypertext", Interface::Hypertext},
            {"Hyperlink", Interface::Hyperlink}, {"Image", Interface::Image},
            {"Selection", Interface::Selection}, {"Table", Interface::Table},
            {"TableCell", Interface::TableCell}, {"Text", Interface::Text},
            {"Value", Interface::Value},
        };
        const auto it = kByName.find(name);
        const auto found = nodes_.find(ref);
        if (it == kByName.end() || found == nodes_.end()) {
            return false;
        }
        return has(found->second.interfaces, it->second);
    }

    // Appends `ref` when it matches. False means "stop": the count is full.
    bool collect(std::vector<Ref>& out, Ref ref, const MatchRule& rule, int count) {
        const auto it = nodes_.find(ref);
        if (it == nodes_.end() || it->second.dead) {
            return true;
        }
        if (matchesRule(ref, rule)) {
            out.push_back(addRef(ref));
            if (count > 0 && static_cast<int>(out.size()) >= count) {
                return false;
            }
        }
        return true;
    }

    Ref applicationRefOf(Ref ref) {
        Ref current = ref;
        for (int depth = 0; current != 0 && exists(current) && depth < 64; ++depth) {
            if (node(current).application != 0) {
                return node(current).application;
            }
            if (node(current).role == PlatformRole::Application) {
                return current;
            }
            current = node(current).parent;
        }
        return 0;
    }

    std::uintptr_t applicationOf(Ref ref) {
        // Not counted as a call: the real backend reads this off the event.
        Ref current = ref;
        for (int depth = 0; current != 0 && exists(current) && depth < 64; ++depth) {
            if (node(current).role == PlatformRole::Application) {
                return static_cast<std::uintptr_t>(current);
            }
            current = node(current).parent;
        }
        return 0;
    }

    std::map<Ref, MockNode> nodes_;
    Ref nextId_ = 1;
    Ref desktop_ = 0;
    EventSink sink_;
    std::vector<std::string> subscribed_;
    std::size_t calls_ = 0;
    std::map<std::string, std::size_t> perCall_;
    std::map<std::pair<Ref, std::string>, std::size_t> perObject_;
};

}  // namespace alus::testing
