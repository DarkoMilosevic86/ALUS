// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/object.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <format>
#include <utility>

namespace alus::a11y {
namespace {

// The text of the first `list item` below `object`, searched breadth first and
// not deeper than a toolkit's few wrapper panels: its own name, or its labels
// joined when it has none (GTK 4 leaves the item unnamed).
std::string shownChoiceText(const Object& object) {
    constexpr int kMaxDepth = 5;
    constexpr std::size_t kMaxVisited = 64;
    std::vector<std::pair<ObjectPtr, int>> queue;
    for (const ObjectPtr& child : object.children()) {
        if (child != nullptr) {
            queue.emplace_back(child, 1);
        }
    }
    for (std::size_t at = 0; at < queue.size() && at < kMaxVisited; ++at) {
        const auto [node, depth] = queue[at];
        if (node->role() == Role::ListItem) {
            if (!node->name().empty()) {
                return node->name();
            }
            std::string text;
            std::vector<ObjectPtr> stack{node};
            while (!stack.empty() && stack.size() < kMaxVisited) {
                const ObjectPtr current = stack.back();
                stack.pop_back();
                const auto& kids = current->children();
                for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                    if (*it != nullptr) {
                        stack.push_back(*it);
                    }
                }
                if (current->role() == Role::Label && !current->name().empty()) {
                    if (!text.empty()) {
                        text += " ";
                    }
                    text += current->name();
                }
            }
            return text;
        }
        if (depth < kMaxDepth) {
            for (const ObjectPtr& child : node->children()) {
                if (child != nullptr) {
                    queue.emplace_back(child, depth + 1);
                }
            }
        }
    }
    return {};
}

// The focusable-descendant search is bounded. An unbounded walk of a web
// document is thousands of D-Bus round trips, which is a freeze, not a search.
constexpr int kFocusableSearchBudget = 256;

// How deep the ancestor walk may go before ALUS decides the tree is broken.
// NVDA uses 100 in api.setFocusObject for the same reason.
constexpr int kMaxAncestorDepth = 100;

// How many children ALUS will ever fetch for one object.
//
// MEASURED, and the measurement is the reason this constant exists. A
// LibreOffice Calc sheet reports its child count as 2147483647 -- INT_MAX, the
// whole addressable grid, not the cells that have anything in them:
//
//     /application/frame/panel/panel/root pane/panel/panel/
//         document spreadsheet/table   name="Sheet Sheet1"  children=2147483647
//
// Without a cap, one `children()` on that object is a reserve() of 32 GB
// followed by two billion D-Bus round trips: the screen reader does not slow
// down, it stops. Anything that walks a window's subtree reaches it -- the flat
// screen review of phase 5 and the focusable-descendant search both do.
//
// The cap is not a cell limit. A spreadsheet is reached through the Table
// interface (`tableInfo`, `cellInfo`, `table()`), which asks for one cell by
// coordinate and is what every table command already uses; the child list is
// for user interfaces, and no real one has eight thousand siblings. An object
// over the cap is reported in the log once per cycle, so a toolkit that is
// merely generous is visible rather than silently truncated.
constexpr int kMaxChildren = 8192;

std::string formatNumber(double value) {
    // No trailing zeros: a slider at 3 says "3", not "3.000000".
    if (std::abs(value - std::round(value)) < 1e-9) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return written > 0 ? std::string{buffer, static_cast<std::size_t>(written)} : std::string{};
}

int parseAttributeInt(const Attributes& attributes, std::string_view key) {
    const auto it = attributes.find(key);
    if (it == attributes.end()) {
        return 0;
    }
    int result = 0;
    const char* first = it->second.data();
    const char* last = first + it->second.size();
    const auto answer = std::from_chars(first, last, result);
    return (answer.ec == std::errc{} && answer.ptr == last) ? result : 0;
}

bool isBlankText(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    });
}

}  // namespace

const char* presentationTypeName(PresentationType type) {
    switch (type) {
        case PresentationType::Unavailable:
            return "unavailable";
        case PresentationType::Layout:
            return "layout";
        case PresentationType::Content:
            return "content";
    }
    return "unknown";
}

Object::Object(Registry& registry, Ref ref)
    : script::ScriptableObject({"NVDAObjects.NVDAObject"}),
      registry_(registry),
      ref_(registry.backend().addRef(ref)),
      // ---- the platform reads ----
      rawName_(scope_, [this] { return backend().name(ref_); }),
      rawDescription_(scope_, [this] { return backend().description(ref_); }),
      rawRole_(scope_, [this] { return backend().role(ref_); }),
      rawStates_(scope_, [this] { return backend().states(ref_); }),
      rawAttributes_(scope_, [this] { return backend().attributes(ref_); }),
      rawInterfaces_(scope_, [this] { return backend().interfaces(ref_); }),
      rawExtents_(scope_, [this] { return backend().extents(ref_); }),
      rawChildCount_(scope_, [this] { return backend().childCount(ref_); }),
      rawIndexInParent_(scope_, [this] { return backend().indexInParent(ref_); }),
      rawValue_(scope_, [this] { return backend().value(ref_); }),
      rawAlive_(scope_, [this] { return backend().alive(ref_); }),
      rawPath_(scope_, [this] { return backend().path(ref_); }),
      rawActionCount_(scope_, [this] { return backend().actionCount(ref_); }),
      rawKeyboardShortcut_(scope_,
                           [this] {
                               // ATK joins the mnemonic, the full path and the
                               // accelerator with semicolons. What the user
                               // wants is the accelerator -- the key that
                               // works from here -- which is the last
                               // non-empty part.
                               if (backend().actionCount(ref_) <= 0) {
                                   return std::string{};
                               }
                               const std::string binding =
                                   backend().actionKeyBinding(ref_, 0);
                               std::string best;
                               std::size_t start = 0;
                               while (start <= binding.size()) {
                                   const std::size_t end = binding.find(';', start);
                                   const std::string part = binding.substr(
                                       start, end == std::string::npos ? std::string::npos
                                                                       : end - start);
                                   if (!part.empty()) {
                                       best = part;
                                   }
                                   if (end == std::string::npos) {
                                       break;
                                   }
                                   start = end + 1;
                               }
                               return best;
                           }),
      rawTableInfo_(scope_, [this] { return backend().tableInfo(ref_); }),
      rawCellInfo_(scope_, [this] { return backend().cellInfo(ref_); }),
      rawRowHeaderText_(scope_, [this] { return backend().rowHeaderText(ref_); }),
      rawColumnHeaderText_(scope_, [this] { return backend().columnHeaderText(ref_); }),
      rawAppInfo_(scope_, [this] { return backend().appInfo(ref_); }),
      rawCharacterCount_(scope_, [this] { return backend().characterCount(ref_); }),
      // ---- the derived values ----
      refined_(scope_,
               [this] {
                   const Role base = roleFromPlatform(*rawRole_);
                   const StateSet states = statesFromPlatform(*rawStates_, *rawRole_);
                   return refine(base, states, *rawAttributes_);
               }),
      name_(scope_,
            [this] {
                if (auto override = askBehaviors<std::string>(
                        [this](const Behavior& behavior) { return behavior.name(*this); })) {
                    return *override;
                }
                std::string platformName = *rawName_;
                if (!platformName.empty()) {
                    return platformName;
                }
                // §6.2: the name comes from the LABELLED_BY relation when the
                // object has none of its own. A GTK entry labelled by a
                // GtkLabel is the common case, and without this the user hears
                // "edit" with no idea what it is for.
                for (const ObjectPtr& label :
                     objectsFrom(backend().relation(ref_, PlatformRelation::LabelledBy))) {
                    if (label && !label->name().empty()) {
                        return label->name();
                    }
                }
                return platformName;
            }),
      description_(scope_,
                   [this] {
                       if (auto override = askBehaviors<std::string>(
                               [this](const Behavior& behavior) {
                                   return behavior.description(*this);
                               })) {
                           return *override;
                       }
                       std::string platform = *rawDescription_;
                       if (!platform.empty()) {
                           return platform;
                       }
                       // The DESCRIBED_BY relation, for the same reason as
                       // LABELLED_BY above.
                       for (const ObjectPtr& source :
                            objectsFrom(backend().relation(ref_, PlatformRelation::DescribedBy))) {
                           if (source && !source->name().empty()) {
                               return source->name();
                           }
                       }
                       return platform;
                   }),
      value_(scope_,
             [this] {
                 if (auto override = askBehaviors<std::string>([this](const Behavior& behavior) {
                         return behavior.value(*this);
                     })) {
                     return *override;
                 }
                 if (const std::optional<ValueInfo>& info = *rawValue_) {
                     if (!info->text.empty()) {
                         // The toolkit's own wording wins: GTK says "50%" or
                         // "Monday" where ALUS could only say a number.
                         return info->text;
                     }
                     if (info->maximum > info->minimum) {
                         const double percentage = (info->current - info->minimum) * 100.0 /
                                                   (info->maximum - info->minimum);
                         return formatNumber(std::round(percentage)) + "%";
                     }
                     return formatNumber(info->current);
                 }
                 // A combo box with no Value interface: the selected item is
                 // the value, which is what the user means by "what is it set
                 // to".
                 const Role currentRole = role();
                 if (currentRole == Role::ComboBox || currentRole == Role::List) {
                     if (backend().selectedChildCount(ref_) > 0) {
                         if (const ObjectPtr selected =
                                 objectFrom(backend().selectedChildAt(ref_, 0))) {
                             return selected->name();
                         }
                     }
                 }
                 if (currentRole == Role::ComboBox) {
                     // GTK 4 (AdwComboRow, GtkDropDown) implements neither
                     // Value nor Selection: the shown choice is the one
                     // `list item` inside it, measured as
                     //     combo box > panel > panel > list > list item > panel > label
                     // Orca reads it from there too (FAZA10.md N124).
                     return shownChoiceText(*this);
                 }
                 return std::string{};
             }),
      role_(scope_,
            [this] {
                if (auto override = askBehaviors<Role>([this](const Behavior& behavior) {
                        return behavior.role(*this);
                    })) {
                    return *override;
                }
                return refined().role;
            }),
      positionInfo_(scope_,
                    [this] {
                        PositionInfo info;
                        info.level = refined().level;
                        // Gecko and Chromium put the ARIA values here, and
                        // they are authoritative: a virtual list of 10000 rows
                        // reports setsize 10000 while having 30 children.
                        const int posInSet = parseAttributeInt(*rawAttributes_, "posinset");
                        const int setSize = parseAttributeInt(*rawAttributes_, "setsize");
                        if (posInSet > 0 && setSize > 0) {
                            info.indexInGroup = posInSet;
                            info.similarItemsInGroup = setSize;
                            return info;
                        }
                        // No posinset/setsize, so the position has to be
                        // worked out from the parent -- but only for the roles
                        // where a group position means anything.
                        //
                        // NVDA does not need this list: on Windows it reads
                        // IAccessible2::groupPosition, which the TOOLKIT
                        // computes and leaves at zero ("not applicable") for
                        // an ordinary button. AT-SPI has no groupPosition at
                        // all, so without a list of roles here every button
                        // in a window would be announced as "20 of 20".
                        switch (role()) {
                            case Role::ListItem:
                            case Role::TreeViewItem:
                            case Role::Tab:
                            case Role::MenuItem:
                            case Role::CheckMenuItem:
                            case Role::RadioMenuItem:
                            case Role::RadioButton:
                            case Role::TableRow:
                            case Role::DesktopIcon:
                                break;
                            default:
                                return info;
                        }
                        const int index = *rawIndexInParent_;
                        if (index < 0) {
                            return info;
                        }
                        if (const ObjectPtr container = parent()) {
                            const int count = container->childCount();
                            if (count > 1) {
                                info.indexInGroup = index + 1;
                                info.similarItemsInGroup = count;
                            }
                        }
                        return info;
                    }),
      presentationType_(scope_,
                        [this] {
                            const StateSet& currentStates = states();
                            if (currentStates.has(State::Invisible)) {
                                return PresentationType::Unavailable;
                            }
                            const Role currentRole = role();
                            const bool isLandmark = currentRole == Role::Landmark ||
                                                    currentRole == Role::Region ||
                                                    !landmark().empty();
                            if (isLandmark && !registry_.settings().reportLandmarks) {
                                return PresentationType::Layout;
                            }
                            if (!roleText().empty()) {
                                // An author who wrote a role description meant
                                // it to be heard.
                                return PresentationType::Content;
                            }
                            if (currentRole == Role::StaticText) {
                                const std::string text =
                                    name().empty() && hasInterface(Interface::Text)
                                        ? textRange(0, -1)
                                        : name();
                                return (!text.empty() && !isBlankText(text))
                                           ? PresentationType::Content
                                           : PresentationType::Layout;
                            }
                            switch (currentRole) {
                                case Role::Unknown:
                                case Role::Pane:
                                // Two that NVDA's list does not need and
                                // AT-SPI does. On Windows the focus ancestry
                                // does not contain the desktop or the
                                // application object; on AT-SPI it always
                                // does, and announcing them would prefix
                                // every focus change with "main desktop pane,
                                // Text Editor". The window below them says
                                // everything the user needs.
                                case Role::DesktopPane:
                                case Role::Application:
                                case Role::TextFrame:
                                case Role::RootPane:
                                case Role::LayeredPane:
                                case Role::ScrollPane:
                                case Role::SplitPane:
                                case Role::Section:
                                case Role::Paragraph:
                                case Role::TitleBar:
                                case Role::Label:
                                case Role::Whitespace:
                                case Role::Border:
                                    return PresentationType::Layout;
                                default:
                                    break;
                            }
                            std::string objectName = name();
                            std::string objectDescription = description();
                            if (isBlankText(objectName)) {
                                objectName.clear();
                            }
                            if (isBlankText(objectDescription)) {
                                objectDescription.clear();
                            }
                            if (objectName.empty() && objectDescription.empty()) {
                                switch (currentRole) {
                                    // FILLER is AT-SPI's, not NVDA's (MSAA
                                    // has none, so NVDA's list does not
                                    // need it). GTK and the desktop-icons
                                    // extension wrap every column of icons
                                    // in an unnamed filler, and moving to an
                                    // icon in another column announced
                                    // "filler" as a new focus ancestor --
                                    // the "popunjavanje" a user reported as
                                    // noise. Orca never speaks a filler.
                                    case Role::Filler:
                                    case Role::Window:
                                    case Role::Panel:
                                    case Role::PropertyPage:
                                    case Role::Grouping:
                                    case Role::OptionPane:
                                    case Role::InternalFrame:
                                    case Role::Form:
                                    case Role::TableBody:
                                    case Role::Region:
                                        return PresentationType::Layout;
                                    case Role::Table:
                                        if (!registry_.settings().reportTables) {
                                            return PresentationType::Layout;
                                        }
                                        break;
                                    case Role::TableRow:
                                    case Role::TableColumn:
                                    case Role::TableCell:
                                        if (!registry_.settings().reportTables ||
                                            !registry_.settings().reportTableCellCoords) {
                                            return PresentationType::Layout;
                                        }
                                        break;
                                    default:
                                        break;
                                }
                            }
                            return PresentationType::Content;
                        }),
      errorMessage_(scope_,
                    [this] {
                        for (const ObjectPtr& source :
                             objectsFrom(backend().relation(ref_, PlatformRelation::ErrorMessage))) {
                            if (source == nullptr) {
                                continue;
                            }
                            if (!source->name().empty()) {
                                return source->name();
                            }
                            if (source->hasInterface(Interface::Text)) {
                                std::string text = source->textRange(0, -1);
                                if (!text.empty()) {
                                    return text;
                                }
                            }
                        }
                        return std::string{};
                    }),
      cellCoordsText_(scope_,
                      [this] {
                          // AT-SPI has no "cell coordinates text": the toolkit
                          // exposes only numbers. Spreadsheets that know their
                          // own wording ("B7") put it in the name, and
                          // LibreOffice does exactly that -- so when the name
                          // of a cell is short and the object is a cell, that
                          // name IS the coordinate text. Anything else is left
                          // to the presentation layer, which can say "row 2
                          // column 3".
                          if (role() != Role::TableCell) {
                              return std::string{};
                          }
                          const auto it = rawAttributes_.get().find("coords-text");
                          return it != rawAttributes_.get().end() ? it->second : std::string{};
                      }),
      parent_(scope_, [this] { return objectFrom(backend().parent(ref_)); }),
      children_(scope_,
                [this] {
                    std::vector<ObjectPtr> result;
                    int count = *rawChildCount_;
                    if (count <= 0) {
                        return result;
                    }
                    if (count > kMaxChildren) {
                        // See kMaxChildren: a Calc sheet says 2147483647.
                        ALUS_DEBUG_WARNING(
                            "{} reports {} children; fetching the first {}", path(),
                            count, kMaxChildren);
                        count = kMaxChildren;
                    }
                    result.reserve(static_cast<std::size_t>(count));
                    for (int index = 0; index < count; ++index) {
                        if (ObjectPtr child = objectFrom(backend().childAt(ref_, index))) {
                            result.push_back(std::move(child));
                        }
                    }
                    return result;
                }),
      application_(scope_, [this] { return objectFrom(backend().application(ref_)); }),
      table_(scope_, [this] { return objectFrom(backend().tableOf(ref_)); }),
      details_(scope_, [this] {
          return objectsFrom(backend().relation(ref_, PlatformRelation::Details));
      }) {}

Object::~Object() { registry_.backend().release(ref_); }

Backend& Object::backend() const noexcept { return registry_.backend(); }

template <class Answer, class Ask>
std::optional<Answer> Object::askBehaviors(const Ask& ask) const {
    for (const std::unique_ptr<Behavior>& behavior : behaviors_) {
        if (std::optional<Answer> answer = ask(*behavior)) {
            return answer;
        }
    }
    return std::nullopt;
}

ObjectPtr Object::objectFrom(Ref ref) const {
    if (ref == 0) {
        return nullptr;
    }
    return registry_.adoptObject(ref);
}

std::vector<ObjectPtr> Object::objectsFrom(const std::vector<Ref>& refs) const {
    std::vector<ObjectPtr> result;
    result.reserve(refs.size());
    for (const Ref ref : refs) {
        if (ObjectPtr object = objectFrom(ref)) {
            result.push_back(std::move(object));
        }
    }
    return result;
}

const Refined& Object::refined() const { return refined_.get(); }

bool Object::isAlive() const { return rawAlive_.get(); }

const std::string& Object::path() const { return rawPath_.get(); }

const std::string& Object::name() const { return name_.get(); }

const std::string& Object::description() const { return description_.get(); }

const std::string& Object::platformName() const { return rawName_.get(); }

const std::string& Object::platformDescription() const { return rawDescription_.get(); }

Role Object::role() const { return role_.get(); }

Role Object::platformRole() const { return refined().role; }

const std::string& Object::roleText() const {
    static const std::string empty;
    const auto it = rawAttributes_.get().find("roledescription");
    return it != rawAttributes_.get().end() ? it->second : empty;
}

const StateSet& Object::states() const { return refined().states; }

const std::string& Object::value() const { return value_.get(); }

const std::string& Object::keyboardShortcut() const { return rawKeyboardShortcut_.get(); }

const Rect& Object::location() const { return rawExtents_.get(); }

const Attributes& Object::attributes() const { return rawAttributes_.get(); }

InterfaceSet Object::interfaces() const { return rawInterfaces_.get(); }

int Object::level() const { return refined().level; }

const std::string& Object::landmark() const { return refined().landmark; }

IsCurrent Object::isCurrent() const { return refined().current; }

const std::string& Object::placeholder() const { return refined().placeholder; }

const PositionInfo& Object::positionInfo() const { return positionInfo_.get(); }

ObjectPtr Object::flowsTo() const {
    for (const ObjectPtr& target : objectsFrom(backend().relation(ref_, PlatformRelation::FlowsTo))) {
        if (target != nullptr) {
            return target;
        }
    }
    return nullptr;
}

bool Object::isFocusable() const {
    if (auto override = askBehaviors<bool>(
            [this](const Behavior& behavior) { return behavior.isFocusable(*this); })) {
        return *override;
    }
    return states().has(State::Focusable);
}

bool Object::hasFocus() const { return states().has(State::Focused); }

bool Object::isProtected() const { return states().has(State::Protected); }

bool Object::hasNavigableText() const {
    // NVDA: `_hasNavigableText`. A label has a Text interface too, but there is
    // nothing to navigate in it; what matters is whether the object is a text
    // container the caret can move in.
    if (!hasInterface(Interface::Text)) {
        return false;
    }
    switch (role()) {
        case Role::StaticText:
        case Role::Label:
        case Role::Link:
        case Role::ListItem:
        case Role::MenuItem:
        case Role::CheckMenuItem:
        case Role::RadioMenuItem:
        case Role::TreeViewItem:
        case Role::Button:
        case Role::ToggleButton:
        case Role::CheckBox:
        case Role::RadioButton:
        case Role::Tab:
            return false;
        default:
            return true;
    }
}

bool Object::isTextEmpty() const {
    if (!hasInterface(Interface::Text)) {
        return value().empty();
    }
    return rawCharacterCount_.get() <= 0;
}

PresentationType Object::presentationType() const { return presentationType_.get(); }

bool Object::isPresentableFocusAncestor() const {
    if (auto override = askBehaviors<bool>([this](const Behavior& behavior) {
            return behavior.isPresentableFocusAncestor(*this);
        })) {
        return *override;
    }
    const PresentationType type = presentationType();
    if (type == PresentationType::Layout || type == PresentationType::Unavailable) {
        return false;
    }
    switch (role()) {
        // NVDA's list exactly: an ancestor of these kinds says nothing the
        // focus itself will not say.
        case Role::TreeViewItem:
        case Role::ListItem:
        case Role::ProgressBar:
        case Role::EditableText:
            return false;
        default:
            return true;
    }
}

const std::string& Object::errorMessage() const { return errorMessage_.get(); }

const std::vector<ObjectPtr>& Object::details() const { return details_.get(); }

ObjectPtr Object::parent() const { return parent_.get(); }

ObjectPtr Object::container() const {
    // Generalised hook 3 of §12.2: the app module may reshape the ancestry
    // without the platform's tree changing. Nothing installed, or a null
    // answer, leaves NVDA's own default in place.
    if (ObjectPtr override = registry_.containerFor(*this)) {
        return override;
    }
    return parent();
}

const std::vector<ObjectPtr>& Object::children() const { return children_.get(); }

int Object::childCount() const { return rawChildCount_.get(); }

ObjectPtr Object::firstChild() const {
    if (childCount() <= 0) {
        return nullptr;
    }
    return objectFrom(backend().childAt(ref_, 0));
}

ObjectPtr Object::lastChild() const {
    const int count = childCount();
    if (count <= 0) {
        return nullptr;
    }
    return objectFrom(backend().childAt(ref_, count - 1));
}

ObjectPtr Object::next() const {
    const ObjectPtr container = parent();
    if (container == nullptr) {
        return nullptr;
    }
    const int index = indexInParent();
    if (index < 0 || index + 1 >= container->childCount()) {
        return nullptr;
    }
    return objectFrom(backend().childAt(container->ref(), index + 1));
}

ObjectPtr Object::previous() const {
    const ObjectPtr container = parent();
    if (container == nullptr) {
        return nullptr;
    }
    const int index = indexInParent();
    if (index <= 0) {
        return nullptr;
    }
    return objectFrom(backend().childAt(container->ref(), index - 1));
}

int Object::indexInParent() const { return rawIndexInParent_.get(); }

ObjectPtr Object::firstFocusableDescendant() const {
    int budget = kFocusableSearchBudget;
    // Breadth first, because the control an author forgot to make focusable is
    // usually one level down, and because the depth of a web document is
    // unbounded.
    std::vector<ObjectPtr> level = children();
    while (!level.empty() && budget > 0) {
        std::vector<ObjectPtr> next;
        for (const ObjectPtr& child : level) {
            if (budget-- <= 0) {
                break;
            }
            if (child == nullptr) {
                continue;
            }
            if (child->isFocusable()) {
                return child;
            }
            const std::vector<ObjectPtr>& grandchildren = child->children();
            next.insert(next.end(), grandchildren.begin(), grandchildren.end());
        }
        level = std::move(next);
    }
    if (budget <= 0) {
        ALUS_DEBUG_WARNING("focusable descendant search gave up after {} objects",
                          kFocusableSearchBudget);
    }
    return nullptr;
}

ObjectPtr Object::application() const { return application_.get(); }

const AppInfo& Object::appInfo() const { return rawAppInfo_.get(); }

int Object::processId() const { return appInfo().processId; }

script::ScriptableObject* Object::appModule() const {
    return registry_.appModuleFor(appInfo());
}

int Object::selectedItemCount(int max) const {
    const int count = backend().selectedChildCount(ref_);
    return max > 0 ? std::min(count, max) : count;
}

ObjectPtr Object::selectionContainer() const {
    // The nearest ancestor that can hold a selection. Bounded, like every
    // other walk in this file.
    ObjectPtr current = parent();
    for (int depth = 0; current != nullptr && depth < kMaxAncestorDepth; ++depth) {
        if (current->hasInterface(Interface::Selection)) {
            return current;
        }
        current = current->parent();
    }
    return nullptr;
}

const TableInfo& Object::tableInfo() const { return rawTableInfo_.get(); }

const CellInfo& Object::cellInfo() const { return rawCellInfo_.get(); }

const std::string& Object::cellCoordsText() const { return cellCoordsText_.get(); }

const std::string& Object::rowHeaderText() const { return rawRowHeaderText_.get(); }

const std::string& Object::columnHeaderText() const { return rawColumnHeaderText_.get(); }

ObjectPtr Object::table() const { return table_.get(); }

Ref Object::tableId() const {
    const ObjectPtr owner = table();
    return owner != nullptr ? owner->ref() : 0;
}

int Object::characterCount() const { return rawCharacterCount_.get(); }

std::string Object::textRange(int start, int end) const {
    return backend().text(ref_, start, end);
}

int Object::caretOffset() const { return backend().caretOffset(ref_); }

int Object::actionCount() const { return rawActionCount_.get(); }

std::string Object::actionName(int index) const { return backend().actionName(ref_, index); }

bool Object::doAction(int index) {
    if (!isAlive()) {
        return false;
    }
    if (index < 0 || index >= actionCount()) {
        return false;
    }
    return backend().doAction(ref_, index);
}

bool Object::setFocus() {
    if (!isAlive()) {
        return false;
    }
    // Not askBehaviors(): setFocus is the one override that mutates, so the
    // behaviours are asked through a non-const reference.
    for (const std::unique_ptr<Behavior>& behavior : behaviors_) {
        if (const std::optional<bool> answer = behavior->setFocus(*this)) {
            return *answer;
        }
    }
    if (!hasInterface(Interface::Component)) {
        return false;
    }
    return backend().grabFocus(ref_);
}

bool Object::scrollIntoView() {
    if (!isAlive() || !hasInterface(Interface::Component)) {
        return false;
    }
    return backend().scrollTo(ref_);
}

bool Object::hasBehavior(std::string_view className) const {
    return std::any_of(behaviors_.begin(), behaviors_.end(),
                       [className](const std::unique_ptr<Behavior>& behavior) {
                           return behavior->className() == className;
                       });
}

void Object::setBehaviors(std::vector<std::unique_ptr<Behavior>> behaviors) {
    behaviors_ = std::move(behaviors);
    // Anything a chooser asked about while deciding was answered WITHOUT
    // these behaviours, so those answers are now wrong. Only the derived
    // values are dropped: the raw platform reads are still true, so this
    // costs no round trip.
    name_.invalidate();
    description_.invalidate();
    value_.invalidate();
    role_.invalidate();
    presentationType_.invalidate();
    // The script class chain, most derived first: every behaviour's NVDA class
    // name, then the plain object. This is what makes a gesture map section
    // written for an NVDA behaviour class reach this object (§5.1).
    std::vector<std::string> chain;
    chain.reserve(behaviors_.size() + 1);
    for (const std::unique_ptr<Behavior>& behavior : behaviors_) {
        chain.emplace_back(behavior->className());
    }
    chain.emplace_back("NVDAObjects.NVDAObject");
    setScriptClasses(std::move(chain));
    // And now the behaviours' own scripts. AFTER the class chain, so that a
    // script bound here is addressable under the behaviour's class name.
    for (const std::unique_ptr<Behavior>& behavior : behaviors_) {
        behavior->bindScripts(*this);
    }
}

void Object::bindBehaviorScript(std::string name, script::ScriptFunction function,
                                script::ScriptSpec spec) {
    bindScript(std::move(name), std::move(function), std::move(spec));
}

void Object::setTreeInterceptor(script::ScriptableObject* interceptor, bool ready) {
    treeInterceptor_ = interceptor;
    treeInterceptorReady_ = ready;
}

void Object::handleEvent(const core::Event& event) {
    // The behaviours first, in order: one that handles the event stops the
    // chain, which is NVDA's "do not call super()".
    for (const std::unique_ptr<Behavior>& behavior : behaviors_) {
        if (behavior->handleEvent(*this, event)) {
            return;
        }
    }

    const PresentationHooks& hooks = registry_.presentation();
    Api& api = registry_.api();
    const ObjectPtr focus = api.focus();
    const bool isFocus = focus != nullptr && focus->sameAs(*this);

    if (event.name == events::kGainFocus) {
        // NVDA: NVDAObject.reportFocus().
        if (hooks.isFocusAnnouncementSuppressed && hooks.isFocusAnnouncementSuppressed(*this)) {
            return;
        }
        if (hooks.speakObject) {
            hooks.speakObject(*this, OutputReason::Focus);
        }
        return;
    }
    if (event.name == events::kFocusEntered) {
        const Role currentRole = role();
        if (currentRole == Role::MenuBar || currentRole == Role::PopupMenu ||
            currentRole == Role::MenuItem) {
            // Opening a menu cancels what was being said; the menu item that
            // gains focus is what matters.
            if (hooks.cancelSpeech) {
                hooks.cancelSpeech();
            }
            return;
        }
        if (isPresentableFocusAncestor() && hooks.speakObject) {
            hooks.speakObject(*this, OutputReason::FocusEntered);
        }
        return;
    }
    if (event.name == events::kForeground) {
        // NVDA says nothing here on purpose: a focusEntered or gainFocus for
        // this same object follows, and saying it twice is worse than late.
        if (hooks.cancelSpeech) {
            hooks.cancelSpeech();
        }
        return;
    }
    if (event.name == events::kNameChange || event.name == events::kValueChange ||
        event.name == events::kDescriptionChange) {
        if (!hooks.speakPropertyChange) {
            return;
        }
        if (!isFocus) {
            return;
        }
        PropertyChange change;
        change.name = event.name == events::kNameChange;
        change.value = event.name == events::kValueChange;
        change.description = event.name == events::kDescriptionChange;
        hooks.speakPropertyChange(*this, change, OutputReason::Change);
        return;
    }
    if (event.name == events::kStateChange || event.name == events::kSelection) {
        // NVDA announces a state change on the focus AND on its ancestors: a
        // focused button may change the state of the column header it is in.
        bool inFocusChain = isFocus;
        if (!inFocusChain) {
            for (const ObjectPtr& ancestor : api.focusAncestors()) {
                if (ancestor != nullptr && ancestor->sameAs(*this)) {
                    inFocusChain = true;
                    break;
                }
            }
        }
        if (inFocusChain && hooks.speakPropertyChange) {
            PropertyChange change;
            change.states = true;
            hooks.speakPropertyChange(*this, change, OutputReason::Change);
        }
        return;
    }
    if (event.name == events::kLiveRegionChange) {
        // The announcement text travels with the event when the toolkit sends
        // `object:announcement`; otherwise the name is the message.
        std::string text;
        if (event.payload != nullptr) {
            text = static_cast<const EventPayload*>(event.payload.get())->text;
        }
        if (text.empty()) {
            text = name();
        }
        if (!text.empty() && hooks.message) {
            hooks.message(text);
        }
        return;
    }
    // Everything else (caret, textChange, childrenAdded, documentLoadComplete)
    // belongs to phases 5 and 6; the event is dispatched and recorded, and no
    // object speaks about it yet.
}


std::vector<std::string> Object::devInfo() const {
    std::vector<std::string> lines;
    lines.push_back(std::format("path: {}", path()));
    lines.push_back(std::format("name: {}", name()));
    if (platformName() != name()) {
        lines.push_back(std::format("platform name: {}", platformName()));
    }
    lines.push_back(std::format("role: {} ({})", roleName(role()), static_cast<int>(role())));
    if (!roleText().empty()) {
        lines.push_back(std::format("role text: {}", roleText()));
    }
    std::string stateText;
    for (const State state : states().toVector()) {
        stateText += (stateText.empty() ? "" : ", ");
        stateText += stateName(state);
    }
    lines.push_back(std::format("states: {}", stateText.empty() ? "(none)" : stateText));
    if (!description().empty()) {
        lines.push_back(std::format("description: {}", description()));
    }
    if (!value().empty()) {
        lines.push_back(std::format("value: {}", value()));
    }
    if (!keyboardShortcut().empty()) {
        lines.push_back(std::format("keyboard shortcut: {}", keyboardShortcut()));
    }
    const Rect& where = location();
    lines.push_back(std::format("location: {},{} {}x{}", where.x, where.y, where.width,
                                where.height));
    lines.push_back(std::format("presentation: {}", presentationTypeName(presentationType())));
    lines.push_back(std::format("children: {}, index in parent: {}", childCount(),
                                indexInParent()));
    lines.push_back(std::format("process: {}, application: {}", processId(),
                                appInfo().name));
    // The behaviours and the script classes, which is what decides what a key
    // does on this object -- the first thing to look at when a key does the
    // wrong thing here.
    std::string classes;
    for (const std::string& each : scriptClasses()) {
        classes += (classes.empty() ? "" : ", ") + each;
    }
    lines.push_back(std::format("script classes: {}", classes));
    lines.push_back(std::format("scripts: {}, behaviours: {}", scriptCount(),
                                behaviors().size()));
    const Attributes& attributes = this->attributes();
    if (!attributes.empty()) {
        std::string text;
        for (const auto& [key, value] : attributes) {
            text += (text.empty() ? "" : ", ") + key + "=" + value;
        }
        lines.push_back(std::format("attributes: {}", text));
    }
    return lines;
}


std::string Object::linkDestination() const {
    // First, the attributes: WebKitGTK and Gecko both put the href there, and
    // reading an attribute set that is cached anyway costs nothing.
    const Attributes& attributes = this->attributes();
    for (const std::string_view key : {"uri", "href", "link-uri"}) {
        const auto it = attributes.find(key);
        if (it != attributes.end() && !it->second.empty()) {
            return it->second;
        }
    }
    // Then the Hypertext interface of the CONTAINING object, which is where
    // AT-SPI keeps a link's URI: the link itself is an embedded object inside
    // its parent's text, and `hyperlink(parent, i)` is what carries the uri.
    const ObjectPtr container = parent();
    if (container == nullptr) {
        return {};
    }
    const int count = backend().hyperlinkCount(container->ref());
    for (int index = 0; index < count; ++index) {
        const std::optional<LinkInfo> link = backend().hyperlink(container->ref(), index);
        if (!link.has_value()) {
            continue;
        }
        // The reference the backend handed over has to be given back, which
        // is the N44 rule; adoptObject is what takes it.
        const ObjectPtr embedded =
            link->object != 0 ? registry_.adoptObject(link->object) : nullptr;
        if (embedded != nullptr && embedded->sameAs(*this)) {
            return link->uri;
        }
    }
    return {};
}

}  // namespace alus::a11y
