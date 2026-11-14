// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/atspi_backend.hpp"

#include "alus/core/log.hpp"

#include <atspi/atspi.h>
#include <glib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

namespace alus::a11y {
namespace {

// The mirrored enumerations must agree with the real AT-SPI ones value for
// value: that is the whole reason PlatformRole and PlatformState exist (see
// platform.hpp). A new AT-SPI that renumbers anything breaks the build here
// instead of mislabelling every object on the desktop. The ends of the three
// enumerations are checked, plus the values that moved between AT-SPI versions.
static_assert(static_cast<int>(PlatformRole::Invalid) == ATSPI_ROLE_INVALID);
static_assert(static_cast<int>(PlatformRole::CheckBox) == ATSPI_ROLE_CHECK_BOX);
static_assert(static_cast<int>(PlatformRole::Button) == ATSPI_ROLE_PUSH_BUTTON);
static_assert(static_cast<int>(PlatformRole::Heading) == ATSPI_ROLE_HEADING);
static_assert(static_cast<int>(PlatformRole::Static) == ATSPI_ROLE_STATIC);
static_assert(static_cast<int>(PlatformRole::Switch) == ATSPI_ROLE_SWITCH);
static_assert(kPlatformRoleCount == ATSPI_ROLE_LAST_DEFINED);
static_assert(static_cast<int>(PlatformState::Invalid) == ATSPI_STATE_INVALID);
static_assert(static_cast<int>(PlatformState::Focused) == ATSPI_STATE_FOCUSED);
static_assert(static_cast<int>(PlatformState::Showing) == ATSPI_STATE_SHOWING);
static_assert(static_cast<int>(PlatformState::ReadOnly) == ATSPI_STATE_READ_ONLY);
static_assert(kPlatformStateCount == ATSPI_STATE_LAST_DEFINED);
static_assert(static_cast<int>(PlatformRelation::LabelledBy) == ATSPI_RELATION_LABELLED_BY);
static_assert(static_cast<int>(PlatformRelation::ErrorMessage) == ATSPI_RELATION_ERROR_MESSAGE);
static_assert(kPlatformRelationCount == ATSPI_RELATION_LAST_DEFINED);
// A state set is a bitset of 64 bits here; AT-SPI must fit in it.
static_assert(kPlatformStateCount <= 64);

AtspiAccessible* toAccessible(Ref ref) { return reinterpret_cast<AtspiAccessible*>(ref); }

Ref toRef(AtspiAccessible* accessible) { return reinterpret_cast<Ref>(accessible); }

// RAII for the three GLib shapes this file deals with.
struct GErrorScope {
    GError* error = nullptr;
    ~GErrorScope() {
        if (error != nullptr) {
            g_error_free(error);
        }
    }
    const char* message() const {
        return error != nullptr ? error->message : "no error message";
    }
    bool failed() const { return error != nullptr; }
};

struct CharPtr {
    gchar* value = nullptr;
    explicit CharPtr(gchar* v) : value(v) {}
    ~CharPtr() { g_free(value); }
    CharPtr(const CharPtr&) = delete;
    CharPtr& operator=(const CharPtr&) = delete;
    std::string str() const { return value != nullptr ? std::string{value} : std::string{}; }
};

template <class T>
struct ObjectPtrScope {
    T* value = nullptr;
    explicit ObjectPtrScope(T* v) : value(v) {}
    ~ObjectPtrScope() {
        if (value != nullptr) {
            g_object_unref(value);
        }
    }
    ObjectPtrScope(const ObjectPtrScope&) = delete;
    ObjectPtrScope& operator=(const ObjectPtrScope&) = delete;
    explicit operator bool() const { return value != nullptr; }
};

struct InterfaceRow {
    const char* name;
    Interface flag;
};

// `atspi_accessible_get_interfaces` answers with the short names.
constexpr std::array<InterfaceRow, 15> kInterfaces{{
    {"Accessible", Interface::Accessible},
    {"Action", Interface::Action},
    {"Collection", Interface::Collection},
    {"Component", Interface::Component},
    {"Document", Interface::Document},
    {"EditableText", Interface::EditableText},
    {"Hypertext", Interface::Hypertext},
    {"Hyperlink", Interface::Hyperlink},
    {"Image", Interface::Image},
    {"Selection", Interface::Selection},
    {"Table", Interface::Table},
    {"TableCell", Interface::TableCell},
    {"Text", Interface::Text},
    {"Value", Interface::Value},
    {"Application", Interface::Application},
}};

// The properties asked for with each event subscription (§6.1's
// `RegisterEvent(event, properties, ...)`).
//
// MEASURED LIMIT, worth knowing before relying on this: AT-SPI can only
// deliver what the application side exposes as a D-BUS PROPERTY. Role and
// State are METHODS (GetRole, GetState), so they cannot travel with an event
// at all -- and of what can, libatspi only puts `Attributes`, `interfaces` and
// `Component.ScreenExtents` into its cache. Worse, that cache is valid ONLY
// FOR THE DURATION OF THE CALLBACK (atspi-event-listener.c). So this list is
// short on purpose, and the saving it buys is taken in the callback itself,
// for focus events, where the latency budget lives.
constexpr std::array<const char*, 2> kEventProperties{{
    "Attributes",
    "Component.ScreenExtents",
}};

}  // namespace

// The GObject listener wrapper. Kept out of the header so that no AT-SPI type
// appears in it.
class AtspiBackend::Listener {
public:
    explicit Listener(AtspiBackend& backend) : backend_(backend) {
        listener_ = atspi_event_listener_new(&Listener::onEvent, this, nullptr);
        properties_ = g_array_new(FALSE, FALSE, sizeof(gchar*));
        for (const char* property : kEventProperties) {
            gchar* copy = g_strdup(property);
            g_array_append_val(properties_, copy);
        }
    }

    ~Listener() {
        if (listener_ != nullptr) {
            g_object_unref(listener_);
        }
        if (properties_ != nullptr) {
            for (guint index = 0; index < properties_->len; ++index) {
                g_free(g_array_index(properties_, gchar*, index));
            }
            g_array_free(properties_, TRUE);
        }
    }

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    bool registerType(const std::string& type, std::string* error) {
        GErrorScope scope;
        if (atspi_event_listener_register_full(listener_, type.c_str(), properties_,
                                               &scope.error) == FALSE) {
            if (error != nullptr) {
                *error = "could not subscribe to " + type + ": " + scope.message();
            }
            return false;
        }
        return true;
    }

    void deregisterType(const std::string& type) {
        GErrorScope scope;
        atspi_event_listener_deregister(listener_, type.c_str(), &scope.error);
    }

private:
    static void onEvent(AtspiEvent* event, void* data) {
        auto* self = static_cast<Listener*>(data);
        self->backend_.deliver(event);
        // The callback owns the event.
        g_boxed_free(ATSPI_TYPE_EVENT, event);
    }

    AtspiBackend& backend_;
    AtspiEventListener* listener_ = nullptr;
    GArray* properties_ = nullptr;
};

Backend::~Backend() = default;

const char* textGranularityName(TextGranularity granularity) {
    switch (granularity) {
        case TextGranularity::Character:
            return "character";
        case TextGranularity::Word:
            return "word";
        case TextGranularity::Sentence:
            return "sentence";
        case TextGranularity::Line:
            return "line";
        case TextGranularity::Paragraph:
            return "paragraph";
    }
    return "character";
}

AtspiBackend::AtspiBackend(Options options) : options_(options) {}

AtspiBackend::~AtspiBackend() {
    unsubscribe();
    // atspi_exit() is NOT called. libatspi's own shutdown tears down the D-Bus
    // connection its worker is still using, and phase 3 measured what that
    // costs at shutdown (N32): the process can hang on exit. The connection
    // goes away with the process, which is the moment this destructor runs.
}

std::string AtspiBackend::platformVersion() {
    gint major = 0;
    gint minor = 0;
    gint micro = 0;
    atspi_get_version(&major, &minor, &micro);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d.%d.%d", major, minor, micro);
    return std::string{buffer};
}

bool AtspiBackend::initialize(std::string* error) {
    if (initialized_) {
        return true;
    }
    // Decision 1: bound every call. Before init, so that even the start-up
    // handshake cannot hang.
    atspi_set_timeout(static_cast<gint>(options_.callTimeout.count()),
                      static_cast<gint>(options_.startupTimeout.count()));

    // atspi_init() returns 0 on success, 1 if already initialised, 2 when
    // there is no accessibility bus.
    const int result = atspi_init();
    if (result != 0 && result != 1) {
        if (error != nullptr) {
            *error = "no accessibility bus (atspi_init returned " + std::to_string(result) +
                     "). Is at-spi2-registryd running, and is "
                     "org.gnome.desktop.interface toolkit-accessibility true?";
        }
        return false;
    }

    // Decision 2: our context -- AFTER atspi_init, and the order is not a
    // preference. atspi_init() ends by binding the accessibility bus to
    // g_main_context_default() (atspi-misc.c:1070), so a context set before it
    // is silently thrown away and NO EVENT EVER ARRIVES. There is no error, no
    // warning, and every other criterion still passes; the screen reader is
    // simply deaf.
    //
    // The same file also binds each application connection it later opens to
    // the default context (atspi-misc.c:374), which a single call here cannot
    // catch up with. That is why the daemon's core loop runs on the DEFAULT
    // context (phase 1 left LoopConfig::context null for exactly this), and
    // why passing a private context is for tests, which pass it knowingly.
    if (options_.mainContext != nullptr) {
        atspi_set_main_context(options_.mainContext);
    }
    initialized_ = true;
    ALUS_INFO("AT-SPI {} ready (call timeout {} ms)", platformVersion(),
              options_.callTimeout.count());
    return true;
}

AtspiBackend::CallTimer::CallTimer(AtspiBackend& backend, const char* callName) noexcept
    : name(callName),
      startedNs(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()) {
    backend.noteCall();
}

AtspiBackend::CallTimer::~CallTimer() {
    const std::int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
    const std::int64_t tookMs = (nowNs - startedNs) / 1000000;
    if (tookMs >= 20) {
        ALUS_DEBUG("slow AT-SPI call: {} took {} ms", name, tookMs);
    }
}

void AtspiBackend::noteError(const char* what, const char* detail) {
    ++errors_;
    // §3.8: an application that went away is an EXPECTED error. It must not
    // play an error sound and must not be logged as an error, or a session
    // with one crashy application becomes unusable.
    ALUS_DEBUG_WARNING("AT-SPI {} failed: {}", what, detail);
}

void AtspiBackend::resetCounters() noexcept {
    calls_ = 0;
    errors_ = 0;
}

Ref AtspiBackend::addRef(Ref ref) {
    if (ref != 0) {
        g_object_ref(toAccessible(ref));
    }
    return ref;
}

void AtspiBackend::release(Ref ref) {
    if (ref != 0) {
        g_object_unref(toAccessible(ref));
    }
}

bool AtspiBackend::alive(Ref ref) {
    if (ref == 0) {
        return false;
    }
    const CallTimer callTimer{*this, __func__};
    AtspiStateSet* set = atspi_accessible_get_state_set(toAccessible(ref));
    if (set == nullptr) {
        return false;
    }
    const ObjectPtrScope<AtspiStateSet> scope{set};
    return atspi_state_set_contains(set, ATSPI_STATE_DEFUNCT) == FALSE;
}

std::string AtspiBackend::path(Ref ref) {
    if (ref == 0) {
        return {};
    }
    // THE OBJECT'S ADDRESS ON THE ACCESSIBILITY BUS: the application's bus
    // name and the object path, which together are unique for as long as the
    // object exists. Both are public fields of AtspiObject and
    // AtspiApplication, so this costs no D-Bus round trip at all.
    //
    // It was `atspi_accessible_get_accessible_id()`, and that is a different
    // thing: AccessibleId is an identifier the APPLICATION chooses -- on the
    // web, the element's HTML `id` -- and most objects have none. Every
    // paragraph, heading and link without an `id` answered the same empty
    // string, so a browse-mode bookmark (DocumentTextRange::moveToBookmark
    // matches leaves by this path) restored to the FIRST leaf with an empty
    // path: the top of the page. Every caret move therefore started from the
    // top -- `h` found the first heading again and again, the down arrow read
    // the second line forever, the up arrow said "edge of document" -- which
    // is what the second live pass heard in Chrome (docs/history/orca-comparison.md,
    // phase-10.md N112). The mock backend had always answered a unique path, so
    // no test could see it.
    auto* object = ATSPI_OBJECT(toAccessible(ref));
    if (object == nullptr || object->path == nullptr) {
        return {};
    }
    std::string out;
    if (object->app != nullptr && object->app->bus_name != nullptr) {
        out = object->app->bus_name;
    }
    out += object->path;
    return out;
}

Ref AtspiBackend::parent(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiAccessible* parentObject = atspi_accessible_get_parent(toAccessible(ref), &scope.error);
    if (scope.failed()) {
        noteError("get_parent", scope.message());
        return 0;
    }
    return toRef(parentObject);
}

Ref AtspiBackend::childAt(Ref ref, int index) {
    if (ref == 0 || index < 0) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiAccessible* child =
        atspi_accessible_get_child_at_index(toAccessible(ref), index, &scope.error);
    if (scope.failed()) {
        noteError("get_child_at_index", scope.message());
        return 0;
    }
    return toRef(child);
}

int AtspiBackend::childCount(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint count = atspi_accessible_get_child_count(toAccessible(ref), &scope.error);
    if (scope.failed()) {
        noteError("get_child_count", scope.message());
        return 0;
    }
    return count;
}

int AtspiBackend::indexInParent(Ref ref) {
    if (ref == 0) {
        return -1;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint index = atspi_accessible_get_index_in_parent(toAccessible(ref), &scope.error);
    if (scope.failed()) {
        noteError("get_index_in_parent", scope.message());
        return -1;
    }
    return index;
}

std::string AtspiBackend::name(Ref ref) {
    if (ref == 0) {
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const CharPtr value{atspi_accessible_get_name(toAccessible(ref), &scope.error)};
    if (scope.failed()) {
        noteError("get_name", scope.message());
        return {};
    }
    return value.str();
}

std::string AtspiBackend::description(Ref ref) {
    if (ref == 0) {
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const CharPtr value{atspi_accessible_get_description(toAccessible(ref), &scope.error)};
    if (scope.failed()) {
        noteError("get_description", scope.message());
        return {};
    }
    return value.str();
}

PlatformRole AtspiBackend::role(Ref ref) {
    if (ref == 0) {
        return PlatformRole::Invalid;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const AtspiRole value = atspi_accessible_get_role(toAccessible(ref), &scope.error);
    if (scope.failed()) {
        noteError("get_role", scope.message());
        return PlatformRole::Invalid;
    }
    return static_cast<PlatformRole>(static_cast<int>(value));
}

std::uint64_t AtspiBackend::states(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    AtspiStateSet* set = atspi_accessible_get_state_set(toAccessible(ref));
    if (set == nullptr) {
        return 0;
    }
    const ObjectPtrScope<AtspiStateSet> scope{set};
    GArray* states = atspi_state_set_get_states(set);
    if (states == nullptr) {
        return 0;
    }
    std::uint64_t bits = 0;
    for (guint index = 0; index < states->len; ++index) {
        const AtspiStateType state = g_array_index(states, AtspiStateType, index);
        const auto value = static_cast<std::uint64_t>(static_cast<int>(state));
        if (value < 64) {
            bits |= std::uint64_t{1} << value;
        }
    }
    g_array_free(states, TRUE);
    return bits;
}

Attributes AtspiBackend::attributes(Ref ref) {
    Attributes result;
    if (ref == 0) {
        return result;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    GHashTable* table = atspi_accessible_get_attributes(toAccessible(ref), &scope.error);
    if (table == nullptr) {
        if (scope.failed()) {
            noteError("get_attributes", scope.message());
        }
        return result;
    }
    GHashTableIter iter;
    void* key = nullptr;
    void* value = nullptr;
    g_hash_table_iter_init(&iter, table);
    while (g_hash_table_iter_next(&iter, &key, &value) != FALSE) {
        if (key == nullptr) {
            continue;
        }
        result.emplace(static_cast<const char*>(key),
                       value != nullptr ? static_cast<const char*>(value) : "");
    }
    g_hash_table_unref(table);
    return result;
}

InterfaceSet AtspiBackend::interfaces(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    // Not a bus call: libatspi knows the interface list from the cache.
    GArray* names = atspi_accessible_get_interfaces(toAccessible(ref));
    if (names == nullptr) {
        return 0;
    }
    InterfaceSet set = 0;
    for (guint index = 0; index < names->len; ++index) {
        gchar* entry = g_array_index(names, gchar*, index);
        if (entry == nullptr) {
            continue;
        }
        const std::string_view name{entry};
        for (const InterfaceRow& row : kInterfaces) {
            if (name == row.name) {
                set |= static_cast<InterfaceSet>(row.flag);
                break;
            }
        }
        g_free(entry);
    }
    g_array_free(names, TRUE);
    return set;
}

Rect AtspiBackend::extents(Ref ref) {
    Rect result;
    if (ref == 0) {
        return result;
    }
    const ObjectPtrScope<AtspiComponent> component{
        atspi_accessible_get_component_iface(toAccessible(ref))};
    if (!component) {
        return result;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiRect* rect = atspi_component_get_extents(component.value, ATSPI_COORD_TYPE_SCREEN,
                                                  &scope.error);
    if (rect == nullptr) {
        if (scope.failed()) {
            noteError("get_extents", scope.message());
        }
        return result;
    }
    result.x = rect->x;
    result.y = rect->y;
    result.width = rect->width;
    result.height = rect->height;
    g_free(rect);
    return result;
}

std::vector<Ref> AtspiBackend::relation(Ref ref, PlatformRelation wanted) {
    std::vector<Ref> result;
    if (ref == 0) {
        return result;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    GArray* relations = atspi_accessible_get_relation_set(toAccessible(ref), &scope.error);
    if (relations == nullptr) {
        if (scope.failed()) {
            noteError("get_relation_set", scope.message());
        }
        return result;
    }
    for (guint index = 0; index < relations->len; ++index) {
        AtspiRelation* relationObject = g_array_index(relations, AtspiRelation*, index);
        if (relationObject == nullptr) {
            continue;
        }
        if (static_cast<int>(atspi_relation_get_relation_type(relationObject)) ==
            static_cast<int>(wanted)) {
            const gint targets = atspi_relation_get_n_targets(relationObject);
            for (gint target = 0; target < targets; ++target) {
                if (AtspiAccessible* object =
                        atspi_relation_get_target(relationObject, target)) {
                    result.push_back(toRef(object));
                }
            }
        }
        g_object_unref(relationObject);
    }
    g_array_free(relations, TRUE);
    return result;
}

std::optional<ValueInfo> AtspiBackend::value(Ref ref) {
    if (ref == 0) {
        return std::nullopt;
    }
    const ObjectPtrScope<AtspiValue> iface{atspi_accessible_get_value_iface(toAccessible(ref))};
    if (!iface) {
        return std::nullopt;
    }
    ValueInfo info;
    GErrorScope scope;
    const CallTimer callTimer{*this, __func__};
    info.current = atspi_value_get_current_value(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_current_value", scope.message());
        return std::nullopt;
    }
    noteCall();
    info.minimum = atspi_value_get_minimum_value(iface.value, nullptr);
    noteCall();
    info.maximum = atspi_value_get_maximum_value(iface.value, nullptr);
    noteCall();
    info.minimumIncrement = atspi_value_get_minimum_increment(iface.value, nullptr);
    noteCall();
    const CharPtr text{atspi_value_get_text(iface.value, nullptr)};
    info.text = text.str();
    return info;
}

int AtspiBackend::characterCount(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint count = atspi_text_get_character_count(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_character_count", scope.message());
        return 0;
    }
    return count;
}

std::string AtspiBackend::text(Ref ref, int start, int end) {
    if (ref == 0) {
        return {};
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const CharPtr value{atspi_text_get_text(iface.value, start, end, &scope.error)};
    if (scope.failed()) {
        noteError("get_text", scope.message());
        return {};
    }
    return value.str();
}

int AtspiBackend::caretOffset(Ref ref) {
    if (ref == 0) {
        return -1;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return -1;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint offset = atspi_text_get_caret_offset(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_caret_offset", scope.message());
        return -1;
    }
    return offset;
}

// ---------------------------------------------------------------------------
// Phase 5: the text interface
//
// Every offset below is a UTF-8 BYTE offset, which is what AT-SPI counts in.
// The text layer keeps those offsets as they are (architecture.md §7.3), so
// nothing in this file converts anything.
// ---------------------------------------------------------------------------

bool AtspiBackend::setCaretOffset(Ref ref, int offset) {
    if (ref == 0) {
        return false;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return false;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gboolean ok = atspi_text_set_caret_offset(iface.value, offset, &scope.error);
    if (scope.failed()) {
        noteError("set_caret_offset", scope.message());
        return false;
    }
    return ok != FALSE;
}

std::optional<TextSegment> AtspiBackend::stringAtOffset(Ref ref, int offset,
                                                        TextGranularity granularity) {
    if (ref == 0) {
        return std::nullopt;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return std::nullopt;
    }
    AtspiTextGranularity platform = ATSPI_TEXT_GRANULARITY_CHAR;
    switch (granularity) {
        case TextGranularity::Character:
            platform = ATSPI_TEXT_GRANULARITY_CHAR;
            break;
        case TextGranularity::Word:
            platform = ATSPI_TEXT_GRANULARITY_WORD;
            break;
        case TextGranularity::Sentence:
            platform = ATSPI_TEXT_GRANULARITY_SENTENCE;
            break;
        case TextGranularity::Line:
            platform = ATSPI_TEXT_GRANULARITY_LINE;
            break;
        case TextGranularity::Paragraph:
            platform = ATSPI_TEXT_GRANULARITY_PARAGRAPH;
            break;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiTextRange* range =
        atspi_text_get_string_at_offset(iface.value, offset, platform, &scope.error);
    if (range == nullptr) {
        if (scope.failed()) {
            noteError("get_string_at_offset", scope.message());
        }
        return std::nullopt;
    }
    TextSegment segment;
    segment.start = range->start_offset;
    segment.end = range->end_offset;
    if (range->content != nullptr) {
        segment.text = range->content;
    }
    // AT-SPI EXPORTS NO FREE FUNCTION FOR AtspiTextRange: the header declares
    // only `atspi_text_range_get_type`, and the library's symbol table has
    // nothing else (`strings libatspi.so.0 | grep atspi_text_range`). Calling
    // g_free on the struct would leak its `content` string on every line the
    // user reads, so the boxed type's own registered free function is used --
    // that is what g_boxed_free is for, and it is the only correct answer here.
    g_boxed_free(ATSPI_TYPE_TEXT_RANGE, range);
    // A toolkit that does not implement this granularity answers (-1, -1) --
    // measured, not assumed, and reported as "no answer" so the text layer
    // segments the text itself rather than believing a negative offset.
    if (segment.start < 0 || segment.end < 0) {
        return std::nullopt;
    }
    return segment;
}

int AtspiBackend::selectionCount(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint count = atspi_text_get_n_selections(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_n_selections", scope.message());
        return 0;
    }
    return count;
}

std::optional<TextSegment> AtspiBackend::selection(Ref ref, int index) {
    if (ref == 0) {
        return std::nullopt;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return std::nullopt;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiRange* range = atspi_text_get_selection(iface.value, index, &scope.error);
    if (range == nullptr) {
        if (scope.failed()) {
            noteError("get_selection", scope.message());
        }
        return std::nullopt;
    }
    TextSegment segment;
    segment.start = range->start_offset;
    segment.end = range->end_offset;
    // AtspiRange is two ints with no owned members, but it is still a boxed
    // type and freed as one, for the same reason as above.
    g_boxed_free(ATSPI_TYPE_RANGE, range);
    if (segment.start < 0 || segment.end < 0) {
        return std::nullopt;
    }
    return segment;
}

bool AtspiBackend::setSelection(Ref ref, int start, int end) {
    if (ref == 0) {
        return false;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return false;
    }
    // AT-SPI has two calls and toolkits disagree about which one works when
    // there is no selection yet: `set_selection` needs an existing one, and
    // `add_selection` refuses when one is already there. So the count decides,
    // and the other call is tried when the first fails -- which is the only
    // way the selection commands work in both GTK and Qt.
    GErrorScope scope;
    const CallTimer callTimer{*this, __func__};
    const gint count = atspi_text_get_n_selections(iface.value, nullptr);
    if (count > 0) {
        noteCall();
        if (atspi_text_set_selection(iface.value, 0, start, end, &scope.error) != FALSE) {
            return true;
        }
        if (scope.failed()) {
            noteError("set_selection", scope.message());
        }
    }
    GErrorScope addScope;
    noteCall();
    if (atspi_text_add_selection(iface.value, start, end, &addScope.error) != FALSE) {
        return true;
    }
    if (addScope.failed()) {
        noteError("add_selection", addScope.message());
    }
    return false;
}

AttributeRun AtspiBackend::attributeRun(Ref ref, int offset, bool includeDefaults) {
    AttributeRun run;
    if (ref == 0) {
        return run;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return run;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    gint start = 0;
    gint end = 0;
    GHashTable* table = atspi_text_get_attribute_run(iface.value, offset, includeDefaults,
                                                     &start, &end, &scope.error);
    if (table == nullptr) {
        if (scope.failed()) {
            noteError("get_attribute_run", scope.message());
        }
        return run;
    }
    GHashTableIter iter;
    void* key = nullptr;
    void* value = nullptr;
    g_hash_table_iter_init(&iter, table);
    while (g_hash_table_iter_next(&iter, &key, &value) != FALSE) {
        if (key == nullptr) {
            continue;
        }
        run.attributes.emplace(static_cast<const char*>(key),
                               value != nullptr ? static_cast<const char*>(value) : "");
    }
    g_hash_table_unref(table);
    run.start = start;
    run.end = end;
    return run;
}

int AtspiBackend::offsetAtPoint(Ref ref, int x, int y) {
    if (ref == 0) {
        return -1;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return -1;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint offset =
        atspi_text_get_offset_at_point(iface.value, x, y, ATSPI_COORD_TYPE_SCREEN,
                                       &scope.error);
    if (scope.failed()) {
        noteError("get_offset_at_point", scope.message());
        return -1;
    }
    return offset;
}

Rect AtspiBackend::characterExtents(Ref ref, int offset) {
    Rect result;
    if (ref == 0) {
        return result;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return result;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiRect* rect = atspi_text_get_character_extents(iface.value, offset,
                                                       ATSPI_COORD_TYPE_SCREEN, &scope.error);
    if (rect == nullptr) {
        if (scope.failed()) {
            noteError("get_character_extents", scope.message());
        }
        return result;
    }
    result.x = rect->x;
    result.y = rect->y;
    result.width = rect->width;
    result.height = rect->height;
    g_free(rect);
    return result;
}

Rect AtspiBackend::rangeExtents(Ref ref, int start, int end) {
    Rect result;
    if (ref == 0) {
        return result;
    }
    const ObjectPtrScope<AtspiText> iface{atspi_accessible_get_text_iface(toAccessible(ref))};
    if (!iface) {
        return result;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiRect* rect = atspi_text_get_range_extents(iface.value, start, end,
                                                   ATSPI_COORD_TYPE_SCREEN, &scope.error);
    if (rect == nullptr) {
        if (scope.failed()) {
            noteError("get_range_extents", scope.message());
        }
        return result;
    }
    result.x = rect->x;
    result.y = rect->y;
    result.width = rect->width;
    result.height = rect->height;
    g_free(rect);
    return result;
}

int AtspiBackend::hyperlinkCount(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiHypertext> iface{
        atspi_accessible_get_hypertext_iface(toAccessible(ref))};
    if (!iface) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint count = atspi_hypertext_get_n_links(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_n_links", scope.message());
        return 0;
    }
    return count;
}

int AtspiBackend::linkIndexAtOffset(Ref ref, int offset) {
    if (ref == 0) {
        return -1;
    }
    const ObjectPtrScope<AtspiHypertext> iface{
        atspi_accessible_get_hypertext_iface(toAccessible(ref))};
    if (!iface) {
        return -1;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint index = atspi_hypertext_get_link_index(iface.value, offset, &scope.error);
    if (scope.failed()) {
        noteError("get_link_index", scope.message());
        return -1;
    }
    return index;
}

std::optional<LinkInfo> AtspiBackend::hyperlink(Ref ref, int index) {
    if (ref == 0 || index < 0) {
        return std::nullopt;
    }
    const ObjectPtrScope<AtspiHypertext> iface{
        atspi_accessible_get_hypertext_iface(toAccessible(ref))};
    if (!iface) {
        return std::nullopt;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const ObjectPtrScope<AtspiHyperlink> link{
        atspi_hypertext_get_link(iface.value, index, &scope.error)};
    if (!link) {
        if (scope.failed()) {
            noteError("get_link", scope.message());
        }
        return std::nullopt;
    }
    LinkInfo info;
    noteCall();
    info.startIndex = atspi_hyperlink_get_start_index(link.value, nullptr);
    noteCall();
    info.endIndex = atspi_hyperlink_get_end_index(link.value, nullptr);
    noteCall();
    const CharPtr uri{atspi_hyperlink_get_uri(link.value, 0, nullptr)};
    info.uri = uri.str();
    noteCall();
    // A NEW reference, which the caller owns -- the rule N44 is about.
    AtspiAccessible* object = atspi_hyperlink_get_object(link.value, 0, nullptr);
    info.object = toRef(object);
    return info;
}

int AtspiBackend::actionCount(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiAction> iface{
        atspi_accessible_get_action_iface(toAccessible(ref))};
    if (!iface) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint count = atspi_action_get_n_actions(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_n_actions", scope.message());
        return 0;
    }
    return count;
}

std::string AtspiBackend::actionName(Ref ref, int index) {
    if (ref == 0 || index < 0) {
        return {};
    }
    const ObjectPtrScope<AtspiAction> iface{
        atspi_accessible_get_action_iface(toAccessible(ref))};
    if (!iface) {
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    // The localized name is what the user hears ("pritisni" rather than
    // "press"), and the toolkit is the one that knows it.
    const CharPtr value{atspi_action_get_localized_name(iface.value, index, &scope.error)};
    if (scope.failed() || value.value == nullptr) {
        const CharPtr fallback{atspi_action_get_action_name(iface.value, index, nullptr)};
        return fallback.str();
    }
    return value.str();
}

std::string AtspiBackend::actionKeyBinding(Ref ref, int index) {
    if (ref == 0 || index < 0) {
        return {};
    }
    const ObjectPtrScope<AtspiAction> iface{
        atspi_accessible_get_action_iface(toAccessible(ref))};
    if (!iface) {
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const CharPtr value{atspi_action_get_key_binding(iface.value, index, &scope.error)};
    if (scope.failed()) {
        noteError("get_key_binding", scope.message());
        return {};
    }
    return value.str();
}

bool AtspiBackend::doAction(Ref ref, int index) {
    if (ref == 0 || index < 0) {
        return false;
    }
    const ObjectPtrScope<AtspiAction> iface{
        atspi_accessible_get_action_iface(toAccessible(ref))};
    if (!iface) {
        return false;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gboolean done = atspi_action_do_action(iface.value, index, &scope.error);
    if (scope.failed()) {
        noteError("do_action", scope.message());
        return false;
    }
    return done != FALSE;
}

bool AtspiBackend::grabFocus(Ref ref) {
    if (ref == 0) {
        return false;
    }
    const ObjectPtrScope<AtspiComponent> component{
        atspi_accessible_get_component_iface(toAccessible(ref))};
    if (!component) {
        return false;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gboolean done = atspi_component_grab_focus(component.value, &scope.error);
    if (scope.failed()) {
        noteError("grab_focus", scope.message());
        return false;
    }
    return done != FALSE;
}

bool AtspiBackend::scrollTo(Ref ref) {
    if (ref == 0) {
        return false;
    }
    const ObjectPtrScope<AtspiComponent> component{
        atspi_accessible_get_component_iface(toAccessible(ref))};
    if (!component) {
        return false;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gboolean done =
        atspi_component_scroll_to(component.value, ATSPI_SCROLL_ANYWHERE, &scope.error);
    if (scope.failed()) {
        noteError("scroll_to", scope.message());
        return false;
    }
    return done != FALSE;
}

Ref AtspiBackend::accessibleAtPoint(Ref ref, int x, int y) {
    if (ref == 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiComponent> component{
        atspi_accessible_get_component_iface(toAccessible(ref))};
    if (!component) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiAccessible* found = atspi_component_get_accessible_at_point(
        component.value, x, y, ATSPI_COORD_TYPE_SCREEN, &scope.error);
    if (scope.failed()) {
        noteError("get_accessible_at_point", scope.message());
        return 0;
    }
    return toRef(found);
}

int AtspiBackend::selectedChildCount(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiSelection> iface{
        atspi_accessible_get_selection_iface(toAccessible(ref))};
    if (!iface) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const gint count = atspi_selection_get_n_selected_children(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_n_selected_children", scope.message());
        return 0;
    }
    return count;
}

Ref AtspiBackend::selectedChildAt(Ref ref, int index) {
    if (ref == 0 || index < 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiSelection> iface{
        atspi_accessible_get_selection_iface(toAccessible(ref))};
    if (!iface) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiAccessible* child =
        atspi_selection_get_selected_child(iface.value, index, &scope.error);
    if (scope.failed()) {
        noteError("get_selected_child", scope.message());
        return 0;
    }
    return toRef(child);
}

TableInfo AtspiBackend::tableInfo(Ref ref) {
    TableInfo info;
    if (ref == 0) {
        return info;
    }
    const ObjectPtrScope<AtspiTable> iface{atspi_accessible_get_table_iface(toAccessible(ref))};
    if (!iface) {
        return info;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    info.rowCount = atspi_table_get_n_rows(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_n_rows", scope.message());
        return TableInfo{};
    }
    noteCall();
    info.columnCount = atspi_table_get_n_columns(iface.value, nullptr);
    noteCall();
    info.selectedRowCount = atspi_table_get_n_selected_rows(iface.value, nullptr);
    noteCall();
    info.selectedColumnCount = atspi_table_get_n_selected_columns(iface.value, nullptr);
    return info;
}

CellInfo AtspiBackend::cellInfo(Ref ref) {
    CellInfo info;
    if (ref == 0) {
        return info;
    }
    const ObjectPtrScope<AtspiTableCell> iface{
        atspi_accessible_get_table_cell(toAccessible(ref))};
    if (!iface) {
        return info;
    }
    // One call for all four numbers: AT-SPI's row_column_span gives the
    // position and both spans together, where asking separately is four round
    // trips for one cell.
    const CallTimer callTimer{*this, __func__};
    gint row = -1;
    gint column = -1;
    gint rowSpan = 1;
    gint columnSpan = 1;
    atspi_table_cell_get_row_column_span(iface.value, &row, &column, &rowSpan, &columnSpan,
                                         nullptr);
    info.row = row;
    info.column = column;
    info.rowSpan = std::max(1, rowSpan);
    info.columnSpan = std::max(1, columnSpan);
    return info;
}

Ref AtspiBackend::tableOf(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const ObjectPtrScope<AtspiTableCell> iface{
        atspi_accessible_get_table_cell(toAccessible(ref))};
    if (!iface) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiAccessible* table = atspi_table_cell_get_table(iface.value, &scope.error);
    if (scope.failed()) {
        noteError("get_table", scope.message());
        return 0;
    }
    return toRef(table);
}

namespace {

// The header cells of a row or a column, flattened to the words in them.
std::string headerText(GPtrArray* cells) {
    if (cells == nullptr) {
        return {};
    }
    std::string text;
    for (guint index = 0; index < cells->len; ++index) {
        auto* cell = static_cast<AtspiAccessible*>(g_ptr_array_index(cells, index));
        if (cell == nullptr) {
            continue;
        }
        const CharPtr name{atspi_accessible_get_name(cell, nullptr)};
        const std::string part = name.str();
        if (!part.empty()) {
            if (!text.empty()) {
                text += " ";
            }
            text += part;
        }
    }
    g_ptr_array_unref(cells);
    return text;
}

}  // namespace

std::string AtspiBackend::rowHeaderText(Ref ref) {
    if (ref == 0) {
        return {};
    }
    const ObjectPtrScope<AtspiTableCell> iface{
        atspi_accessible_get_table_cell(toAccessible(ref))};
    if (!iface) {
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    return headerText(atspi_table_cell_get_row_header_cells(iface.value, nullptr));
}

std::string AtspiBackend::columnHeaderText(Ref ref) {
    if (ref == 0) {
        return {};
    }
    const ObjectPtrScope<AtspiTableCell> iface{
        atspi_accessible_get_table_cell(toAccessible(ref))};
    if (!iface) {
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    return headerText(atspi_table_cell_get_column_header_cells(iface.value, nullptr));
}

// ---------------------------------------------------------------------------
// Collection
// ---------------------------------------------------------------------------

namespace {

AtspiCollectionMatchType toMatchType(MatchType type) {
    switch (type) {
        case MatchType::Any:
            return ATSPI_Collection_MATCH_ANY;
        case MatchType::All:
            return ATSPI_Collection_MATCH_ALL;
        case MatchType::None:
            return ATSPI_Collection_MATCH_NONE;
        case MatchType::Empty:
            return ATSPI_Collection_MATCH_EMPTY;
        case MatchType::Invalid:
            break;
    }
    return ATSPI_Collection_MATCH_INVALID;
}

AtspiCollectionSortOrder toSortOrder(SortOrder order) {
    switch (order) {
        case SortOrder::Canonical:
            return ATSPI_Collection_SORT_ORDER_CANONICAL;
        case SortOrder::Flow:
            return ATSPI_Collection_SORT_ORDER_FLOW;
        case SortOrder::Tab:
            return ATSPI_Collection_SORT_ORDER_TAB;
        case SortOrder::ReverseCanonical:
            return ATSPI_Collection_SORT_ORDER_REVERSE_CANONICAL;
        case SortOrder::ReverseFlow:
            return ATSPI_Collection_SORT_ORDER_REVERSE_FLOW;
        case SortOrder::ReverseTab:
            return ATSPI_Collection_SORT_ORDER_REVERSE_TAB;
        case SortOrder::Invalid:
            break;
    }
    return ATSPI_Collection_SORT_ORDER_INVALID;
}

AtspiCollectionTreeTraversalType toTraversal(TreeTraversal traversal) {
    switch (traversal) {
        case TreeTraversal::RestrictChildren:
            return ATSPI_Collection_TREE_RESTRICT_CHILDREN;
        case TreeTraversal::RestrictSibling:
            return ATSPI_Collection_TREE_RESTRICT_SIBLING;
        case TreeTraversal::InOrder:
            break;
    }
    return ATSPI_Collection_TREE_INORDER;
}

// `atspi_match_rule_new` takes ownership of nothing and copies everything, so
// every piece built here is freed on the way out -- which is the reason this
// is one function and not four call sites.
struct MatchRuleScope {
    AtspiMatchRule* rule = nullptr;
    AtspiStateSet* states = nullptr;
    GHashTable* attributes = nullptr;
    GArray* roles = nullptr;
    GArray* interfaces = nullptr;

    explicit MatchRuleScope(const MatchRule& source) {
        states = atspi_state_set_new(nullptr);
        if (source.stateMatch != MatchType::Invalid) {
            for (std::size_t bit = 0; bit < kPlatformStateCount; ++bit) {
                if ((source.states & (1ULL << bit)) != 0) {
                    atspi_state_set_add(states, static_cast<AtspiStateType>(bit));
                }
            }
        }
        attributes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        for (const auto& [key, value] : source.attributes) {
            g_hash_table_insert(attributes, g_strdup(key.c_str()), g_strdup(value.c_str()));
        }
        roles = g_array_new(FALSE, FALSE, sizeof(AtspiRole));
        for (const PlatformRole role : source.roles) {
            AtspiRole value = static_cast<AtspiRole>(role);
            g_array_append_val(roles, value);
        }
        interfaces = g_array_new(FALSE, FALSE, sizeof(gchar*));
        for (const std::string& name : source.interfaces) {
            gchar* copy = g_strdup(name.c_str());
            g_array_append_val(interfaces, copy);
        }
        // MATCH_INVALID means "criterion unused" to US, but the provider side
        // does not implement it: measured against Chrome 157 on a Wikipedia
        // article, a rule whose stateMatch was MATCH_INVALID matched NOTHING
        // (0 of 333 links, in 1.8 ms -- the search ran and answered empty),
        // while every other match type found all 333. Orca never sends
        // MATCH_INVALID, which is why no browser is tested against it.
        //
        // An EMPTY set with MATCH_ALL means "has all of no states", which is
        // vacuously true and therefore exactly the same criterion -- spelled
        // in a way providers actually implement. A non-empty set keeps what
        // the caller asked for.
        const auto unusedIsAll = [](MatchType type, bool empty) {
            return (type == MatchType::Invalid && empty) ? ATSPI_Collection_MATCH_ALL
                                                         : toMatchType(type);
        };
        rule = atspi_match_rule_new(
            states, unusedIsAll(source.stateMatch, source.states == 0), attributes,
            unusedIsAll(source.attributeMatch, source.attributes.empty()), roles,
            unusedIsAll(source.roleMatch, source.roles.empty()), interfaces,
            unusedIsAll(source.interfaceMatch, source.interfaces.empty()),
            source.invert ? TRUE : FALSE);
    }

    ~MatchRuleScope() {
        if (rule != nullptr) {
            g_object_unref(rule);
        }
        if (states != nullptr) {
            g_object_unref(states);
        }
        if (attributes != nullptr) {
            g_hash_table_unref(attributes);
        }
        if (roles != nullptr) {
            g_array_free(roles, TRUE);
        }
        if (interfaces != nullptr) {
            for (guint index = 0; index < interfaces->len; ++index) {
                g_free(g_array_index(interfaces, gchar*, index));
            }
            g_array_free(interfaces, TRUE);
        }
    }

    MatchRuleScope(const MatchRuleScope&) = delete;
    MatchRuleScope& operator=(const MatchRuleScope&) = delete;
};

// The GArray libatspi hands back holds references the caller owns; they become
// the returned Refs one for one, so nothing is unreffed here -- only the array
// itself is freed. Getting this wrong in either direction is N44 again.
std::vector<Ref> takeMatches(GArray* array) {
    std::vector<Ref> result;
    if (array == nullptr) {
        return result;
    }
    result.reserve(array->len);
    for (guint index = 0; index < array->len; ++index) {
        if (AtspiAccessible* object = g_array_index(array, AtspiAccessible*, index)) {
            result.push_back(toRef(object));
        }
    }
    g_array_free(array, TRUE);
    return result;
}

}  // namespace

std::vector<Ref> AtspiBackend::matchesFrom(Ref root, Ref from, const MatchRule& rule,
                                           SortOrder order, TreeTraversal traversal, int count,
                                           bool forwards) {
    if (root == 0 || from == 0) {
        return {};
    }
    const ObjectPtrScope<AtspiCollection> collection{
        atspi_accessible_get_collection_iface(toAccessible(root))};
    if (!collection) {
        // No Collection interface. NOT an error and NOT a tree walk: §9.1
        // point 2 is the whole reason browse mode is affordable on Linux, so a
        // document without it is reported to the user as "no next heading"
        // rather than silently costing a thousand D-Bus calls.
        ALUS_DEBUG("collection: root has no Collection interface");
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const MatchRuleScope matchRule{rule};
    GArray* array = nullptr;
    if (forwards) {
        // The last argument is `traverse`, NOT `limit_scope`. FALSE here makes
        // libatspi consider only the object's immediate children, so a
        // document of any depth answers nothing: measured on a Wikipedia
        // article, traverse=FALSE found 0 links in 0.09 ms where TRUE found
        // 333 in 2.70 ms (phase-6.md N-traverse). Quick navigation needs the
        // whole subtree.
        array = atspi_collection_get_matches_from(
            collection.value, toAccessible(from), matchRule.rule, toSortOrder(order),
            toTraversal(traversal), count, TRUE, &scope.error);
    } else {
        // `get_matches_to` searches BACKWARDS from the object, which is what
        // shift+h is. `limit_scope` false means "the whole subtree of the
        // collection", not "only this object's siblings".
        // Two booleans here, and they are not the same thing: `limit_scope`
        // FALSE means "the whole subtree of the collection", and `traverse`
        // TRUE means "descend into it". Both are needed.
        array = atspi_collection_get_matches_to(
            collection.value, toAccessible(from), matchRule.rule, toSortOrder(order),
            toTraversal(traversal), FALSE, count, TRUE, &scope.error);
    }
    if (array == nullptr && scope.failed()) {
        noteError(forwards ? "get_matches_from" : "get_matches_to", scope.message());
        return {};
    }
    return takeMatches(array);
}

std::vector<Ref> AtspiBackend::matches(Ref root, const MatchRule& rule, SortOrder order,
                                       int count) {
    if (root == 0) {
        return {};
    }
    const ObjectPtrScope<AtspiCollection> collection{
        atspi_accessible_get_collection_iface(toAccessible(root))};
    if (!collection) {
        ALUS_DEBUG("collection: root has no Collection interface");
        return {};
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    const MatchRuleScope matchRule{rule};
    // `traverse` TRUE: the elements list is the whole document, not the
    // root's immediate children. See matchesFrom() above.
    GArray* array = atspi_collection_get_matches(collection.value, matchRule.rule,
                                                 toSortOrder(order), count, TRUE, &scope.error);
    if (array == nullptr && scope.failed()) {
        noteError("get_matches", scope.message());
        return {};
    }
    std::vector<Ref> result = takeMatches(array);

    // FLOW is the order of READING and the order this layer wants (§9.1, T6.6),
    // but `GetMatches` does not implement it everywhere: measured against
    // Chrome 157, sortBy=FLOW and sortBy=TAB return an EMPTY array in 0.07 ms
    // -- not an error, just nothing -- where CANONICAL returns all 333 links
    // in 2.0 ms. `GetMatchesFrom` honours FLOW in the same browser, so this
    // is a gap in one method and not in the provider.
    //
    // So: ask for reading order, and if the provider answered nothing, ask
    // again in tree order rather than tell the user the document is empty.
    // The retry costs one call and only happens on an empty answer.
    if (result.empty() && (order == SortOrder::Flow || order == SortOrder::Tab ||
                           order == SortOrder::ReverseFlow || order == SortOrder::ReverseTab)) {
        const SortOrder fallback =
            (order == SortOrder::Flow || order == SortOrder::Tab) ? SortOrder::Canonical
                                                                  : SortOrder::ReverseCanonical;
        ALUS_DEBUG("collection: get_matches answered nothing in reading order, "
                   "retrying in tree order");
        noteCall();
        GErrorScope retryScope;
        GArray* retry =
            atspi_collection_get_matches(collection.value, matchRule.rule,
                                         toSortOrder(fallback), count, TRUE, &retryScope.error);
        if (retry == nullptr && retryScope.failed()) {
            noteError("get_matches", retryScope.message());
            return result;
        }
        return takeMatches(retry);
    }
    return result;
}

Ref AtspiBackend::application(Ref ref) {
    if (ref == 0) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    GErrorScope scope;
    AtspiAccessible* app = atspi_accessible_get_application(toAccessible(ref), &scope.error);
    if (scope.failed()) {
        noteError("get_application", scope.message());
        return 0;
    }
    return toRef(app);
}

AppInfo AtspiBackend::appInfo(Ref ref) {
    AppInfo info;
    if (ref == 0) {
        return info;
    }
    const Ref appRef = application(ref);
    if (appRef == 0) {
        return info;
    }
    AtspiAccessible* app = toAccessible(appRef);
    const CallTimer callTimer{*this, __func__};
    const CharPtr name{atspi_accessible_get_name(app, nullptr)};
    info.name = name.str();
    noteCall();
    const CharPtr toolkit{atspi_accessible_get_toolkit_name(app, nullptr)};
    // THE PRIMARY KEY for choosing overlay behaviours and app modules on Linux
    // (§6.2): "gtk", "qt", "libreoffice", "chromium". It replaces NVDA's
    // windowClassName, which is a Windows concept.
    info.toolkitName = toolkit.str();
    noteCall();
    const CharPtr version{atspi_accessible_get_toolkit_version(app, nullptr)};
    info.toolkitVersion = version.str();
    noteCall();
    const CharPtr atspiVersion{atspi_accessible_get_atspi_version(app, nullptr)};
    info.atspiVersion = atspiVersion.str();
    noteCall();
    info.processId = static_cast<int>(atspi_accessible_get_process_id(app, nullptr));
    release(appRef);
    return info;
}

Ref AtspiBackend::desktop() {
    if (!initialized_) {
        return 0;
    }
    const CallTimer callTimer{*this, __func__};
    // Index 0: AT-SPI has exactly one desktop, and has had since 2.0.
    return toRef(atspi_get_desktop(0));
}

bool AtspiBackend::subscribe(const std::vector<std::string>& eventTypes, EventSink sink,
                             std::string* error) {
    if (!initialized_ && !initialize(error)) {
        return false;
    }
    sink_ = std::move(sink);
    if (listener_ == nullptr) {
        listener_ = std::make_unique<Listener>(*this);
    }
    std::size_t failures = 0;
    std::string firstError;
    for (const std::string& type : eventTypes) {
        std::string typeError;
        if (listener_->registerType(type, &typeError)) {
            subscribed_.push_back(type);
            continue;
        }
        ++failures;
        if (firstError.empty()) {
            firstError = typeError;
        }
        ALUS_WARNING("{}", typeError);
    }
    if (subscribed_.empty()) {
        if (error != nullptr) {
            *error = firstError.empty() ? "no AT-SPI event subscription succeeded" : firstError;
        }
        return false;
    }
    if (failures > 0) {
        // Partial subscription is usable but degraded, and the user is told:
        // some events simply will not arrive.
        ALUS_WARNING("{} of {} AT-SPI event subscriptions failed", failures, eventTypes.size());
    }
    ALUS_INFO("subscribed to {} AT-SPI event types", subscribed_.size());
    return true;
}

void AtspiBackend::unsubscribe() {
    if (listener_ == nullptr) {
        return;
    }
    for (const std::string& type : subscribed_) {
        listener_->deregisterType(type);
    }
    subscribed_.clear();
    listener_.reset();
    sink_ = nullptr;
}

void AtspiBackend::deliver(void* rawEvent) {
    auto* event = static_cast<AtspiEvent*>(rawEvent);
    if (event == nullptr || !sink_) {
        return;
    }
    PlatformEvent out;
    out.type = event->type != nullptr ? event->type : "";
    out.detail1 = event->detail1;
    out.detail2 = event->detail2;
    if (event->source != nullptr) {
        out.source = toRef(static_cast<AtspiAccessible*>(g_object_ref(event->source)));
    }
    // `sender` is the application the event came from. Using it as the
    // limiter's per-application identity costs nothing, where asking for the
    // application object would be a tree walk per event -- and the limiter
    // sees a thousand events in a flood (§3.4).
    out.application = reinterpret_cast<std::uintptr_t>(event->sender);

    // any_data: a string for a name change or an announcement, an accessible
    // for an active-descendant change.
    if (G_IS_VALUE(&event->any_data)) {
        if (G_VALUE_HOLDS_STRING(&event->any_data)) {
            const char* text = g_value_get_string(&event->any_data);
            out.text = text != nullptr ? text : "";
        } else if (G_VALUE_HOLDS_OBJECT(&event->any_data)) {
            void* object = g_value_get_object(&event->any_data);
            if (object != nullptr && ATSPI_IS_ACCESSIBLE(object)) {
                out.object = toRef(static_cast<AtspiAccessible*>(g_object_ref(object)));
            }
        }
    }
    sink_(std::move(out));
}

}  // namespace alus::a11y
