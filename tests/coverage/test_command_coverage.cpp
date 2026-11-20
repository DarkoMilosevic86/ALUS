// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Does every key ALUS ships actually run something?
//
// WHY THIS FILE EXISTS. `quit` is in data/scripts.csv with the gesture
// `kb:ALUS+q`, docs/commands.md promises it, and pressing it does nothing at
// all: nothing in src/ ever binds it (docs/history/phase-9.md N99). T9.5 cannot see
// that, because it compares the reference with the catalogue and the reference
// is GENERATED FROM the catalogue -- two copies of one truth, agreeing
// perfectly about a command that does not exist. The same shape as N91 (the
// browse-mode defect no hermetic test caught, because the check shared its
// source with what it checked) and N97.
//
// So the third source is the one nothing else here reads: THE RUNNING
// PROGRAM. This file assembles the daemon's own stack -- the same services, in
// the same order, with the same wiring (src/daemon/main.cpp) -- and then asks
// the ten-step lookup, for every key assignment ALUS ships, whether a script
// comes back. A key that answers nothing is a key that does nothing when a
// blind user presses it, and that is the bug.
//
// It is hermetic: the mock accessibility platform (tests/a11y/mock_backend.hpp)
// stands in for at-spi2, nothing takes the keyboard (`noInput`), and no braille
// display is opened. No script is ever EXECUTED -- this file asks only whether
// one would be found.
#include "alus/a11y/service.hpp"
#include "alus/app/service.hpp"
#include "alus/braille/service.hpp"
#include "alus/commands/system.hpp"
#include "alus/config/commands.hpp"
#include "alus/config/config.hpp"
#include "alus/gui/service.hpp"
#include "alus/input/gesture.hpp"
#include "alus/input/key_names.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/browse_mode.hpp"
#include "alus/nav/service.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/command_status.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/global_commands.hpp"
#include "alus/ui/message.hpp"

#include "a11y/mock_backend.hpp"
#include "nav/browse_support.hpp"
#include "support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef ALUS_TEST_DATA_DIR
#define ALUS_TEST_DATA_DIR "data"
#endif
#ifndef ALUS_SOURCE_ROOT
#define ALUS_SOURCE_ROOT "."
#endif

using namespace alus;

namespace {

const std::filesystem::path kDataDir{ALUS_TEST_DATA_DIR};
const std::filesystem::path kSourceRoot{ALUS_SOURCE_ROOT};

// ---------------------------------------------------------------------------
// The daemon's stack, hermetically
//
// The order and the wiring are src/daemon/main.cpp's, deliberately: what is
// being measured is which commands the DAEMON can reach, so a fixture that
// assembled a different set would answer a different question. The two
// differences are written down here rather than left to be noticed:
//
//   * the accessibility platform is the mock, not libatspi (a11y::Options has
//     the seam for exactly this), and
//   * there is no speech pipeline, because no script is executed. Every
//     command object is created whether or not a presenter exists; the
//     services say so themselves (nav/service.cpp, braille/service.cpp).
// ---------------------------------------------------------------------------
class DaemonStack : public testing::LoopFixture {
public:
    DaemonStack() {
        a11y::Service::registerDefaults(config_);
        nav::Service::registerDefaults(config_);

        input::InputServiceOptions inputOptions;
        // Never take the keyboard: this test must not compete with the screen
        // reader the developer is running (N8).
        inputOptions.noInput = true;
        inputOptions.dataDir = kDataDir;
        // Not the developer's own gestures.ini: the question is what ALUS
        // SHIPS, not what this machine has been customised to.
        inputOptions.userGestureMap = kDataDir / "there-is-no-user-map.ini";
        input_ = std::make_unique<input::Service>(loop(), config_,
                                                 input::GestureManager::Hooks{},
                                                 inputOptions);

        a11y::A11yServiceOptions a11yOptions;
        a11yOptions.backend = &backend;
        a11y_ = std::make_unique<a11y::Service>(loop(), config_, input_.get(),
                                                [] { return nullptr; }, a11yOptions);
        nav_ = std::make_unique<nav::Service>(
            loop(), config_, [this] { return a11y_->registry(); }, input_.get(),
            [] { return nullptr; });
        braille_ = std::make_unique<braille::Service>(
            loop(), config_, [this] { return a11y_->registry(); },
            [this] { return nav_.get(); }, input_.get(), [] { return nullptr; });
        app_ = std::make_unique<app::Service>(
            config_, [this] { return a11y_->registry(); }, [this] { return nav_.get(); },
            input_.get(), [](const std::string&) {});

        input_->initialize();
        a11y_->initialize();
        nav_->initialize();
        braille_->initialize();
        app_->initialize();

        gui_ = std::make_unique<gui::Service>(config_, input_->manager(),
                                              &input::KeyNames::shared(),
                                              &input_->catalog(),
                                              gui::Service::Hooks{});
        gui_->initialize();
        // The daemon's own line, src/daemon/main.cpp: the settings scripts
        // reach the dispatcher through the context like every other layer's.
        input_->addContextContributor([this](script::DispatchContext& context) {
            context.globalCommands.push_back(gui_->scriptable());
        });
        // The speech and configuration commands, as the daemon builds them.
        // THE GETTERS ANSWER NULL HERE, deliberately: this fixture runs no
        // speech pipeline (see the comment above), so what is being asserted
        // is that the lookup REACHES these commands. Each one then says that
        // speech is unavailable instead of doing nothing, which is the
        // behaviour `alus-daemon --no-speech` has too.
        speechCommands_ = std::make_unique<speech::GlobalCommands>(
            speech::GlobalCommandsContext{}, &input_->catalog());
        config::CommandsContext configCommandContext;
        configCommandContext.manager = &config_;
        configCommandContext.dispatcher = [this] { return input_->dispatcher(); };
        configCommands_ = std::make_unique<config::Commands>(std::move(configCommandContext),
                                                             &input_->catalog());
        commands::SystemCommands::Context systemCommandContext;
        systemCommandContext.dispatcher = [this] { return input_->dispatcher(); };
        systemCommands_ = std::make_unique<commands::SystemCommands>(
            std::move(systemCommandContext), &input_->catalog());
        input_->addContextContributor([this](script::DispatchContext& context) {
            context.globalCommands.push_back(speechCommands_.get());
            context.globalCommands.push_back(configCommands_.get());
            context.globalCommands.push_back(systemCommands_.get());
        });
        // Steps 6 and 7 of the lookup, stood in for by `focusOn` below.
        input_->addContextContributor([this](script::DispatchContext& context) {
            if (focus_ != nullptr) {
                context.focus = focus_.get();
            }
            if (useBrowse_ && browse_ != nullptr) {
                context.treeInterceptor = browse_.get();
                context.treeInterceptorReady = true;
            }
        });
        // LAST, so that it sees what every other contributor put in: the
        // assembled context, kept so that a silent key can be diagnosed.
        // Whether anything OWNS the script is a different fact from whether
        // the lookup can REACH it, and the two have different fixes.
        input_->addContextContributor([this](script::DispatchContext& context) {
            owners_.clear();
            owners_ = context.globalCommands;
            for (script::ScriptableObject* object :
                 {context.gestureScriptable, context.appModule, context.brailleDisplay,
                  context.treeInterceptor, context.focus, context.profileActivation}) {
                if (object != nullptr) {
                    owners_.push_back(object);
                }
            }
            for (script::ScriptableObject* object : context.globalPlugins) {
                owners_.push_back(object);
            }
            for (script::ScriptableObject* object : context.focusAncestors) {
                owners_.push_back(object);
            }
        });
    }

    // Every scriptable the last lookup had in its context. Call after a
    // findScript, which is what assembles it.
    const std::vector<script::ScriptableObject*>& owners() const { return owners_; }

    // Does anything in the last context implement this command at all?
    const script::ScriptableObject* ownerOf(const std::string& name) const {
        for (script::ScriptableObject* object : owners_) {
            if (object->scriptByName(name) != nullptr) {
                return object;
            }
        }
        return nullptr;
    }

    ~DaemonStack() {
        // Before anything is torn down: terminating a service recomputes the
        // grab set, and that runs the lookup over whatever the context says.
        focusOn(nullptr, false);
        owners_.clear();
        browse_.reset();
        gui_->terminate();
        app_->terminate();
        braille_->terminate();
        nav_->terminate();
        a11y_->terminate();
        input_->terminate();
    }

    input::Service& input() { return *input_; }
    nav::Service& nav() { return *nav_; }
    a11y::Registry& registry() { return *a11y_->registry(); }
    const script::Catalog& catalog() { return input_->catalog(); }
    script::Dispatcher& dispatcher() { return *input_->dispatcher(); }

    // Where the focus is, for the next lookup.
    //
    // The object model's own contributor reads this from the registry
    // (`api.focus()`, `focus->treeInterceptor()`), and that path is T4's and
    // T6.9's to assert. What is needed HERE is only that the right objects are
    // in the context at all -- that `kb:h` has a browse mode to be found on
    // and `kb:leftArrow` an editable text -- because the question this file
    // asks is whether anything implements the command, not how the context is
    // assembled. Setting it directly also keeps the mock focus events out of
    // it, which would be a second thing that could fail.
    // Browse mode over a document root, OWNED here. The ownership is the
    // point: the context contributors outlive any local, and
    // `app::Service::terminate()` recomputes the grab set -- which runs the
    // whole lookup once more, after a local interceptor would already be
    // gone. Found by this file crashing in its own teardown.
    nav::TreeInterceptor* installBrowseMode(const a11y::ObjectPtr& root) {
        browse_ = nav::BrowseMode::chooseForDocument(root, *nav_);
        return browse_.get();
    }

    // Where the focus is for the next lookup, and whether it is inside the
    // browse-mode document.
    void focusOn(a11y::ObjectPtr focus, bool withBrowseMode, bool inFocusMode = false) {
        focus_ = std::move(focus);
        useBrowse_ = withBrowseMode;
        if (browse_ != nullptr && browse_->passThrough() != inFocusMode) {
            auto* browse = dynamic_cast<nav::BrowseMode*>(browse_.get());
            if (inFocusMode && browse != nullptr) {
                // Focus mode as ALUS enters it in a field: Escape is ALUS's
                // only then (N148).
                browse->enterFocusModeAutomatically();
            } else {
                browse_->setPassThrough(inFocusMode, /*announce=*/false);
            }
        }
    }

    testing::MockBackend backend;

private:
    testing::QuietLog quiet_;
    config::Manager config_;
    std::unique_ptr<input::Service> input_;
    std::unique_ptr<a11y::Service> a11y_;
    std::unique_ptr<nav::Service> nav_;
    std::unique_ptr<braille::Service> braille_;
    std::unique_ptr<app::Service> app_;
    std::unique_ptr<gui::Service> gui_;
    std::unique_ptr<speech::GlobalCommands> speechCommands_;
    std::unique_ptr<config::Commands> configCommands_;
    std::unique_ptr<commands::SystemCommands> systemCommands_;
    a11y::ObjectPtr focus_;
    nav::TreeInterceptorPtr browse_;
    bool useBrowse_ = false;
    std::vector<script::ScriptableObject*> owners_;
};

}  // namespace

TEST_CASE("the daemon's whole command stack comes up on the mock platform",
          "[commands]") {
    DaemonStack stack;
    REQUIRE(stack.catalog().scriptCount() > 300);
    // If this fails nothing below means anything: an empty context would
    // report every key as unbound, which is a false alarm and not a finding.
    REQUIRE(stack.dispatcher().findScript(*[] {
        input::KeyboardGesture::Init init;
        init.event.keysym = '1';
        init.baseKeysym = '1';
        init.modifiers = static_cast<input::Modifiers>(input::Modifier::Alus);
        return std::make_shared<input::KeyboardGesture>(std::move(init));
    }()).has_value());
}

// ---------------------------------------------------------------------------
// T9.6 -- every key assignment ALUS ships runs something.
//
// The built-in gesture map is what every user gets in every language: 272
// lines of `script = key`, generated from data/scripts.csv. For each of them
// this asks the ten-step lookup for a script, in the two contexts those keys
// are meant for -- the focus in a web document (browse mode: the single
// letters, the cursor, the table keys) and the focus in an editable text field
// (the caret keys, which are bound by the EditableText BEHAVIOUR and not by
// any commands object).
//
// A key that answers nothing in either context is a key that does nothing. It
// is worse than a missing feature, because the user was told it exists: the
// catalogue lists it, docs/commands.md documents it, and the gesture dialog
// offers to rebind it.
// ---------------------------------------------------------------------------

namespace {

// Every keyboard assignment in the built-in map, as (identifier, script).
struct Assignment {
    std::string identifier;  // normalized, e.g. "kb:alus+1"
    std::string location;    // the NVDA class the section names
    std::string script;
};

std::vector<Assignment> keyboardAssignments(const input::GestureMap& map,
                                            std::vector<Assignment>* notKeyboard) {
    std::vector<Assignment> out;
    for (const auto& [identifier, entry] : map.allEntries()) {
        if (entry.script.empty()) {
            // A `None =` unbind. Nothing to implement.
            continue;
        }
        Assignment assignment{identifier, entry.location, entry.script};
        if (!input::parseKeyboardGesture(identifier).has_value()) {
            // Braille display keys and touch gestures: a different input
            // device, a different map, and not this file's question.
            if (notKeyboard != nullptr) {
                notKeyboard->push_back(assignment);
            }
            continue;
        }
        out.push_back(assignment);
    }
    return out;
}

std::shared_ptr<input::KeyboardGesture> gestureFor(const std::string& identifier) {
    const std::optional<input::ParsedKeyboardGesture> parsed =
        input::parseKeyboardGesture(identifier);
    if (!parsed.has_value()) {
        return nullptr;
    }
    input::KeyboardGesture::Init init;
    init.event.keysym = parsed->keysym;
    init.baseKeysym = parsed->keysym;
    init.modifiers = parsed->modifiers;
    init.isAlusKey = parsed->mainKeyName == input::KeyNames::kAlusKeyName;
    // A layout-qualified assignment is only ever pressed in that layout.
    init.config.layout = parsed->layout.value_or(input::Layout::Desktop);
    return std::make_shared<input::KeyboardGesture>(std::move(init));
}

}  // namespace

TEST_CASE("T9.6 every key the built-in map assigns runs something",
          "[commands][T9.6]") {
    DaemonStack stack;
    const input::GestureMap& builtin = stack.catalog().builtinGestures();
    REQUIRE_FALSE(builtin.empty());

    std::vector<Assignment> notKeyboard;
    const std::vector<Assignment> assignments = keyboardAssignments(builtin, &notKeyboard);
    // The map is generated; if it ever came out nearly empty, every assertion
    // below would pass while measuring nothing.
    REQUIRE(assignments.size() > 200);

    // ---- context 1: the focus in a web document, browse mode on ----
    testing::WebDocumentTree tree{stack.backend};
    tree.addLink(stack.backend, "a link");
    const a11y::Ref editRef = tree.addEdit(stack.backend, "Search");
    const a11y::ObjectPtr root = stack.registry().objectFor(tree.document);
    REQUIRE(root != nullptr);
    nav::TreeInterceptor* interceptor = stack.installBrowseMode(root);
    REQUIRE(interceptor != nullptr);
    REQUIRE(interceptor->scriptByName("nextHeading") != nullptr);

    // ---- context 2: the focus in an editable text field ----
    const a11y::ObjectPtr edit = stack.registry().objectFor(editRef);
    REQUIRE(edit != nullptr);
    {
        // Scoped, or this message would be repeated under every failure below.
        INFO("the EditableText behaviour did not bind its caret scripts; without it "
             "every kb:leftArrow-style assignment below would look unbound");
        REQUIRE(edit->scriptByName("caret_moveByCharacter") != nullptr);
    }

    // Which of the two contexts found it, so a failure says where to look.
    const auto resolve = [&](const Assignment& assignment) -> std::string {
        const std::shared_ptr<input::KeyboardGesture> gesture = gestureFor(assignment.identifier);
        if (gesture == nullptr) {
            return {};
        }
        stack.focusOn(root, /*withBrowseMode=*/true);
        if (stack.dispatcher().findScript(*gesture).has_value()) {
            return "browse mode";
        }
        stack.focusOn(edit, /*withBrowseMode=*/false);
        if (stack.dispatcher().findScript(*gesture).has_value()) {
            return "editable text";
        }
        // Focus mode inside the document: Escape, which leaves it, is ALUS's
        // only there -- in browse mode it is the application's (N144).
        stack.focusOn(root, /*withBrowseMode=*/true, /*inFocusMode=*/true);
        const bool inFocusMode = stack.dispatcher().findScript(*gesture).has_value();
        stack.focusOn(root, /*withBrowseMode=*/true, /*inFocusMode=*/false);
        if (inFocusMode) {
            return "focus mode";
        }
        return {};
    };

    // Which object owns a command, asked in BOTH contexts -- the diagnosis is
    // worthless otherwise: `resolve` leaves the editable-text context in
    // place, where browse mode is not in the context at all, so a browse-mode
    // command would be reported as owned by nothing. (It was, until a
    // negative test put the `alus+space` defect back and got the wrong
    // diagnosis for it.)
    const auto ownerClassesOf = [&](const std::string& name) {
        std::string classes;
        for (const bool browse : {true, false}) {
            stack.focusOn(browse ? root : edit, browse);
            // The lookup is what assembles the context the owner is found in.
            (void)stack.dispatcher().findScript(*gestureFor("kb:alus+1"));
            if (const script::ScriptableObject* owner = stack.ownerOf(name)) {
                for (const std::string& each : owner->scriptClasses()) {
                    classes += (classes.empty() ? "" : ", ") + each;
                }
                return classes;
            }
        }
        return classes;
    };

    // N154: the lists the user sees (Input gestures, the commands quick
    // reference) show what the status file calls available. So the file must
    // be exact both ways: a command nothing implements is listed (or the
    // dialog offers a dead row), and a listed [missing] command is really
    // missing (or the dialog hides a working one).
    {
        const script::CommandStatus& commandStatus = stack.catalog().status();
        for (const script::ScriptInfo* info : stack.catalog().all()) {
            const bool owned = !ownerClassesOf(info->name).empty();
            const auto state = commandStatus.stateOf(info->name);
            if (state == script::CommandStatus::State::Implemented && !owned) {
                FAIL_CHECK("NOTHING IMPLEMENTS `" + info->name + "` [" + info->location +
                           "], and the Input gestures dialog would offer it. Implement "
                           "it, or list it under [missing] in data/commands-status.ini.");
            }
            if (state == script::CommandStatus::State::Missing && owned) {
                FAIL_CHECK("`" + info->name + "` is implemented but listed under [missing] "
                           "in data/commands-status.ini, which hides it from the user. "
                           "Delete its line.");
            }
        }
    }

    // N102's regression test, and it belongs here rather than in the browse
    // mode suite: what broke was the WIRING between the interceptor and the
    // catalogue, and the browse mode tests build the interceptor themselves.
    // A command with no description is hidden from the gesture dialog (T9.3)
    // and has nothing for input help to say, so losing the catalogue cost
    // every browse-mode command its name without failing anything.
    {
        // ANNOUNCEMENT STUBS ARE NOT COUNTED, and that is the whole reason
        // this block is written the way it is. The browse-mode class chain
        // ends in `globalCommands.GlobalCommands` (browse_mode.cpp says why),
        // so `bindAnnouncements` puts a stub for every [announce] command on
        // this object too. Counting those made the number below fall by one
        // every time a command was IMPLEMENTED -- a floor that dropped as the
        // screen reader improved, which is the opposite of what a regression
        // test should do. It tripped at 98 when five commands were written in
        // one sitting.
        const script::CommandStatus& commandStatus = stack.catalog().status();
        std::size_t described = 0;
        std::size_t stubs = 0;
        for (const script::ScriptInfo* info : interceptor->scripts()) {
            if (commandStatus.stateOf(info->name) != script::CommandStatus::State::Implemented) {
                ++stubs;
                continue;
            }
            const script::ScriptInfo* row =
                stack.catalog().find(info->location, info->name);
            if (row == nullptr || row->description.empty()) {
                continue;  // upstream documents no description for this one
            }
            INFO("browse mode's `" << info->name
                                   << "` lost its catalogue description: the gesture dialog "
                                      "will hide it and input help cannot name it");
            CHECK_FALSE(info->description.empty());
            ++described;
        }
        INFO("browse mode carries " << interceptor->scriptCount() << " commands, " << described
                                    << " of them implemented and described, and " << stubs
                                    << " announcement stubs");
        // 75 as this is written, of 157 scripts on the interceptor. The floor
        // is well under it on purpose: what this guards against is the
        // catalogue not being wired to the interceptor AT ALL (N102), which
        // takes the number to nearly zero -- not a command or two moving
        // between the two columns above.
        CHECK(described > 60);
    }

    // Three outcomes, and each one is asserted rather than counted:
    //
    //   implemented  the key must find a script. Nothing else to say.
    //   [announce]   the key must ALSO find a script -- the announcement that
    //                the command is not in this version. Silence and
    //                "not in this version" are the same to a test that only
    //                counts; they are not the same to a blind user, which is
    //                why the announcement is bound at all (phase-6.md O10).
    //   [platform]   the key must find NOTHING, because grabbing it would
    //                take it from the application and ALUS cannot give it
    //                back (N18). This one is asserted the other way round, so
    //                that nobody "fixes" a silent key by grabbing it.
    const script::CommandStatus& status = stack.catalog().status();
    REQUIRE_FALSE(status.empty());

    std::size_t implemented = 0;
    std::size_t announced = 0;
    std::size_t platformLimited = 0;
    for (const Assignment& assignment : assignments) {
        const std::string found = resolve(assignment);
        const std::string where = std::string("`") + assignment.script + "` is assigned " +
                                  assignment.identifier + " in [" + assignment.location + "]";
        switch (status.stateOf(assignment.script)) {
            case script::CommandStatus::State::Platform:
                ++platformLimited;
                if (!found.empty()) {
                    FAIL_CHECK("GRABBED BUT MUST NOT BE: " + where +
                               " and the lookup answers it (" + found +
                               "), so ALUS takes the key from the application and cannot "
                               "give it back (N18). data/commands-status.ini lists it under "
                               "[platform]: " + std::string(status.reasonFor(assignment.script)));
                }
                break;
            case script::CommandStatus::State::Announce:
                ++announced;
                if (found.empty()) {
                    // The announcement did not reach this key. Either the
                    // object that should carry it does not answer to the
                    // section, or nothing bound it at all.
                    const std::string ownerClasses = ownerClassesOf(assignment.script);
                    FAIL_CHECK("SILENT: " + where +
                               ", is listed under [announce] in data/commands-status.ini, and "
                               "still answers nothing" +
                               (ownerClasses.empty()
                                    ? std::string(" -- nothing carries the announcement for "
                                                  "that section")
                                    : " -- it is owned by an object answering to " +
                                          ownerClasses + ", which the lookup cannot reach"));
                }
                break;
            case script::CommandStatus::State::Missing:
                FAIL_CHECK("HAS A KEY BUT IS LISTED AS MISSING: " + where +
                           ". [missing] is for commands with no key; give it an "
                           "[announce] line instead, so the key says why.");
                break;
            case script::CommandStatus::State::Implemented:
                ++implemented;
                if (found.empty()) {
                    const std::string ownerClasses = ownerClassesOf(assignment.script);
                    if (ownerClasses.empty()) {
                        FAIL_CHECK("UNIMPLEMENTED AND SILENT: " + where +
                                   " and nothing in ALUS implements it. Implement it, or add "
                                   "it to data/commands-status.ini so the key at least says "
                                   "it is not in this version.");
                    } else {
                        FAIL_CHECK("UNREACHABLE: " + where +
                                   " but is implemented on an object answering to " +
                                   ownerClasses +
                                   " -- the lookup can never get there, so the command was "
                                   "written and then lost on the way to the keyboard.");
                    }
                }
                break;
        }
    }

    // The three numbers, attached to an assertion rather than printed: the
    // suite runs under the TAP reporter and a bare WARN is not TAP, which
    // turns a passing test into "Unknown TAP output lines".
    INFO("keyboard assignments: " << assignments.size() << " -- " << implemented
                                  << " implemented, " << announced
                                  << " announced as not in this version, " << platformLimited
                                  << " deliberately unbound (N18)");
    CHECK(implemented > 150);

    // What was NOT checked, stated rather than left implied: the assignments
    // this file skipped must all be another device's, or the number above is
    // quietly smaller than it looks.
    for (const Assignment& assignment : notKeyboard) {
        INFO("skipped as not a keyboard assignment: " << assignment.identifier << " -> `"
                                                      << assignment.script << "`");
        const bool otherDevice = assignment.identifier.rfind("br", 0) == 0 ||
                                 assignment.identifier.rfind("bk", 0) == 0 ||
                                 assignment.identifier.rfind("ts", 0) == 0;
        CHECK(otherDevice);
    }
}

// ---------------------------------------------------------------------------
// The guard: neither the status file nor the inventory above may rot.
//
// The whole reason N99 survived T9.5 is that the check shared its source with
// what it checked. So this case reads the one source nothing else here reads:
// THE CODE. It is a text search, with the weakness that comes with one -- it
// cannot see a binding whose name is computed -- and that weakness is stated
// rather than left to be discovered, because the two things it must catch are
// both written literally:
//
//   * a command listed as "not in this version" that somebody has since
//     implemented, which leaves the announcement shadowing a real command;
//   * a NEW scriptable class, whose commands the fixture above would silently
//     not have in its context -- reporting every one of its keys as
//     implemented when nothing had been asked.
// ---------------------------------------------------------------------------

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream{path};
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

// Every .cpp under src/, with its text.
std::vector<std::pair<std::string, std::string>> sourceFiles() {
    std::vector<std::pair<std::string, std::string>> files;
    const std::filesystem::path src = kSourceRoot / "src";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(src)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
            continue;
        }
        files.emplace_back(std::filesystem::relative(entry.path(), kSourceRoot).string(),
                           readFile(entry.path()));
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

TEST_CASE("T9.6 the status file and the command inventory do not rot",
          "[commands][T9.6]") {
    const std::vector<std::pair<std::string, std::string>> files = sourceFiles();
    REQUIRE(files.size() > 50);

    SECTION("nothing listed as not-in-this-version is implemented after all") {
        script::CommandStatus status;
        REQUIRE(status.load(kDataDir / "commands-status.ini"));
        REQUIRE_FALSE(status.announced().empty());

        for (const auto& [name, reason] : status.announced()) {
            // The two spellings a binding site uses, both with the name
            // written out: `ALUS_SCRIPT(dateTime,` and `bindScript("dateTime"`.
            const std::string asMacro = "ALUS_SCRIPT(" + name;
            const std::string asString = "bindScript(\"" + name + "\"";
            const std::string asBehavior = "bindBehaviorScript(\n        \"" + name + "\"";
            for (const auto& [path, text] : files) {
                const bool bound = text.find(asMacro) != std::string::npos ||
                                   text.find(asString) != std::string::npos ||
                                   text.find(asBehavior) != std::string::npos;
                INFO("`" << name << "` is bound in " << path
                         << " but data/commands-status.ini still lists it under [announce]"
                            " -- so the announcement is shadowing a command that exists."
                            " Delete the line.");
                CHECK_FALSE(bound);
            }
        }
    }

    SECTION("every file that binds commands is one the fixture has in context") {
        // The files the DaemonStack above reaches, each named with the object
        // that carries its commands. Adding a scriptable class means adding
        // it here AND to the fixture -- which is the point: the failure is
        // loud, and the alternative is a context that quietly covers less
        // than the daemon's.
        const std::set<std::string> covered = {
            // step 10, the global commands, in the order the daemon builds them
            "src/input/commands.cpp",         // input::InputCommands
            "src/a11y/commands.cpp",          // a11y::ObjectCommands
            "src/nav/text_commands.cpp",      // nav::TextCommands
            "src/braille/commands.cpp",       // braille::Commands
            "src/app/commands.cpp",           // app::AppCommands
            "src/gui/service.cpp",            // gui::Service::Commands
            "src/speech/global_commands.cpp", // speech::GlobalCommands
            "src/config/commands.cpp",        // config::Commands
            "src/commands/system.cpp",        // commands::SystemCommands
            "src/input/service.cpp",          // the [announce] fallback, last
            // step 6, the tree interceptor
            "src/nav/browse_mode.cpp",        // nav::BrowseMode
            // step 7, the focus object and its behaviours
            "src/a11y/object.cpp",            // a11y::Object::bindBehaviorScript
            "src/nav/editable_text.cpp",      // the caret commands
            // the generic machinery, which owns no commands of its own
            "src/script/scriptable.cpp",
            "src/script/command_status.cpp",
            // NOT covered, and deliberately: step 3's app module and the
            // plugin host need an application in the mock tree that this
            // fixture does not build. Their commands carry their own
            // gestures and are not in the built-in map, so no key in it
            // depends on them.
            "src/app/modules/libreoffice.cpp",
            "src/app/plugin.cpp",
        };
        for (const auto& [path, text] : files) {
            const bool binds = text.find("bindScript(") != std::string::npos ||
                               text.find("ALUS_SCRIPT(") != std::string::npos ||
                               text.find("bindBehaviorScript(") != std::string::npos;
            if (!binds) {
                continue;
            }
            INFO(path << " binds commands and is not in this test's list. Add the object "
                         "that owns them to the DaemonStack fixture, then name the file "
                         "here -- otherwise every key it implements is reported as "
                         "implemented without anything having been asked.");
            CHECK(covered.count(path) == 1);
        }
    }
}

TEST_CASE("N145 a command ALUS does not have says why, not just that it is missing",
          "[commands]") {
    testing::QuietLog quiet;
    script::Catalog catalog;
    REQUIRE(catalog.loadFromDataDirectory(kDataDir));
    script::ScriptableObject commands({"globalCommands.GlobalCommands"}, &catalog);
    std::vector<std::string> said;
    REQUIRE(script::bindAnnouncements(commands, catalog,
                                      [&said](const std::string& text) { said.push_back(text); }) >
            0);
    const auto press = [&](const char* name) {
        const script::Script* found = commands.scriptByName(name);
        REQUIRE(found != nullptr);
        input::KeyboardGesture::Init init;
        input::KeyboardGesture gesture{init};
        found->function(gesture);
        REQUIRE_FALSE(said.empty());
        return said.back();
    };
    CHECK(press("toggleLeftMouseButton") == "ALUS cannot hold a mouse button down");
    CHECK(press("moveMouseToNavigatorObject").find("the click keys") != std::string::npos);
    CHECK(press("activatePythonConsole") == "ALUS has no Python console");
    // And the commands that do exist now are no longer stubs.
    CHECK(commands.scriptByName("copyLastSpokenInformation") == nullptr);
    CHECK(commands.scriptByName("leftMouseClick") == nullptr);
}
