// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Configuration: files, profiles, triggers, and restart in place.
//
// Port of NVDA's `config/` (`ConfigManager`, `ProfileTrigger`) and
// `core.resetConfiguration` (`core.py:315-419`). See architecture.md §3.9.
//
// The storage is GKeyFile (ini), the same shape as NVDA's configobj files, so
// `gestures.ini` files are directly compatible. Paths follow the XDG spec:
//
//   ~/.config/alus/alus.ini          the main configuration
//   ~/.config/alus/gestures.ini      user key overrides (NVDA format)
//   ~/.config/alus/profiles/*.ini    configuration profiles
//
// A profile need only carry a subset of the settings. More recently activated
// profiles win; the base configuration is consulted last. Changed settings are
// written to the most recently activated profile — NVDA's rule, and the reason
// a profile can be a thin overlay.
//
// EVERY SUBSYSTEM MUST BE RESTARTABLE IN PROCESS, like NVDA's
// resetConfiguration. That is a design requirement, not an afterthought: a user
// who changes the synthesizer or the braille table must not have to restart the
// screen reader. Hence `SubsystemRegistry` and `resetConfiguration()` below.
#pragma once

#include "alus/core/extension_points.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using GKeyFile = struct _GKeyFile;

namespace alus::config {

struct Paths {
    std::filesystem::path configDir;    // ~/.config/alus
    std::filesystem::path mainFile;     // ~/.config/alus/alus.ini
    std::filesystem::path profilesDir;  // ~/.config/alus/profiles

    // Honours $ALUS_CONFIG_DIR, then $XDG_CONFIG_HOME/alus, then
    // ~/.config/alus. Tests use the first.
    static Paths fromEnvironment();
    static Paths forDirectory(std::filesystem::path configDir);
};

class Manager {
public:
    explicit Manager(Paths paths = Paths::fromEnvironment());
    ~Manager();

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    const Paths& paths() const noexcept { return paths_; }

    // The factory defaults (NVDA: confspec). A value that is set nowhere falls
    // back to these, and a factory reset goes back to them.
    void setDefault(std::string section, std::string key, std::string value);

    // Loads the base configuration. A missing file is not an error — the first
    // run has no file yet.
    bool load(std::string* error = nullptr);
    // Writes every file that changed.
    bool save(std::string* error = nullptr);
    // NVDA: conf.reset(). Rereads from disk, or returns to the factory
    // defaults, and drops every activated profile.
    void reset(bool factoryDefaults = false);

    // ---- values ----
    bool has(std::string_view section, std::string_view key) const;
    // The keys of a section that have a factory default, sorted: every
    // setting ALUS knows of there. The settings window's test uses it to find
    // a setting with no row (N140).
    std::vector<std::string> defaultKeys(std::string_view section) const;
    std::string getString(std::string_view section, std::string_view key,
                          std::string_view fallback = {}) const;
    bool getBool(std::string_view section, std::string_view key, bool fallback = false) const;
    std::int64_t getInt(std::string_view section, std::string_view key,
                        std::int64_t fallback = 0) const;
    double getDouble(std::string_view section, std::string_view key, double fallback = 0.0) const;

    // Written to the most recently activated profile, unless the section is
    // base-only (NVDA: BASE_ONLY_SECTIONS).
    void setString(std::string_view section, std::string_view key, std::string_view value);
    void setBool(std::string_view section, std::string_view key, bool value);
    void setInt(std::string_view section, std::string_view key, std::int64_t value);
    void setDouble(std::string_view section, std::string_view key, double value);

    // Sections that cannot be overridden in a profile.
    void addBaseOnlySection(std::string section);
    bool isBaseOnlySection(std::string_view section) const;

    // ---- profiles ----
    std::vector<std::string> listProfiles() const;
    bool createProfile(const std::string& name, std::string* error = nullptr);
    bool deleteProfile(const std::string& name, std::string* error = nullptr);
    bool renameProfile(const std::string& from, const std::string& to,
                       std::string* error = nullptr);

    // NVDA: manualActivateProfile. Stays active until deactivated.
    bool activateProfile(const std::string& name, std::string* error = nullptr);
    bool deactivateProfile(const std::string& name);
    // In activation order; the last one wins on lookup and takes the writes.
    std::vector<std::string> activeProfiles() const;

    // ---- triggers (NVDA: ProfileTrigger) ----
    // A trigger is a string spec ("app:firefox", "sayAll") the user associates
    // with a profile. enter() activates it, exit() deactivates it.
    void mapTrigger(std::string spec, std::string profileName);
    bool unmapTrigger(const std::string& spec);
    std::optional<std::string> profileForTrigger(const std::string& spec) const;

    bool triggerEnter(const std::string& spec);
    bool triggerExit(const std::string& spec);

    void setTriggersEnabled(bool enabled);
    bool triggersEnabled() const noexcept { return triggersEnabled_; }
    // NVDA: suspendProfileTriggers / resumeProfileTriggers. Used by the speech
    // manager's profile latch, which must switch profiles mid-utterance.
    void suspendTriggers();
    void resumeTriggers();
    bool triggersSuspended() const noexcept { return suspended_.has_value(); }

    // NVDA: ConfigManager.atomicProfileSwitch(). Several trigger enters and
    // exits counted as ONE switch.
    //
    // WHY IT IS NOT A CONVENIENCE. Switching application is three profile
    // operations: the old application's trigger exits, the new one's enters,
    // and a say-all or a braille trigger may ride along. Each one on its own
    // notifies `postProfileSwitch`, and every subsystem listening rebuilds
    // itself from the configuration -- the synthesizer rate, the braille
    // table, the object-presentation settings. Done one at a time, alt-tab
    // costs N rebuilds and the user hears the synthesizer stutter. Held in one
    // of these, it costs exactly one (T8.4).
    //
    // The guard is RAII rather than a callback because the choreography of
    // §12.1 has to run INSIDE it: loseFocus, the trigger changes, gainFocus.
    class AtomicSwitch {
    public:
        AtomicSwitch() = default;
        explicit AtomicSwitch(Manager* manager);
        ~AtomicSwitch();

        AtomicSwitch(const AtomicSwitch&) = delete;
        AtomicSwitch& operator=(const AtomicSwitch&) = delete;
        AtomicSwitch(AtomicSwitch&& other) noexcept;
        AtomicSwitch& operator=(AtomicSwitch&& other) noexcept;

        // Ends the batch early; the destructor then does nothing.
        void finish();

    private:
        Manager* manager_ = nullptr;
    };

    [[nodiscard]] AtomicSwitch atomicProfileSwitch();
    bool inAtomicProfileSwitch() const noexcept { return atomicDepth_ > 0; }
    // How many times postProfileSwitch has actually fired. The measurement
    // T8.4 asks for, and one line in the log when a switch looks expensive.
    std::uint64_t profileSwitchCount() const noexcept { return profileSwitches_; }

    // NVDA: post_configProfileSwitch. Notified after the active profile set
    // changes, so subsystems can re-read what concerns them.
    core::Action<>& postProfileSwitch() noexcept { return postProfileSwitch_; }
    // Notified at the end of resetConfiguration().
    core::Action<>& postConfigReset() noexcept { return postConfigReset_; }

private:
    struct Layer;

    Layer* topWritableLayer(std::string_view section);
    const Layer* findLayerWith(std::string_view section, std::string_view key) const;
    Layer* loadProfileLayer(const std::string& name, std::string* error);
    std::filesystem::path profilePath(const std::string& name) const;
    void notifyProfileSwitch();

    Paths paths_;
    std::unique_ptr<Layer> base_;
    // Activated profiles, in activation order.
    std::vector<std::unique_ptr<Layer>> profiles_;
    std::map<std::string, std::string> defaults_;  // "section/key" → value
    std::set<std::string, std::less<>> baseOnlySections_;
    std::map<std::string, std::string> triggersToProfiles_;
    std::vector<std::string> activeTriggers_;
    std::optional<std::vector<std::string>> suspended_;
    bool triggersEnabled_ = true;
    int atomicDepth_ = 0;
    bool atomicPending_ = false;
    std::uint64_t profileSwitches_ = 0;
    core::Action<> postProfileSwitch_;
    core::Action<> postConfigReset_;
};

// Something that can be taken down and brought back up in process.
class Subsystem {
public:
    Subsystem() = default;
    virtual ~Subsystem() = default;

    Subsystem(const Subsystem&) = delete;
    Subsystem& operator=(const Subsystem&) = delete;

    virtual std::string_view name() const = 0;
    virtual void initialize() = 0;
    virtual void terminate() = 0;
};

// A subsystem built from two callables, for small ones and for tests.
class FunctionSubsystem final : public Subsystem {
public:
    FunctionSubsystem(std::string name, std::function<void()> initialize,
                      std::function<void()> terminate)
        : name_(std::move(name)),
          initialize_(std::move(initialize)),
          terminate_(std::move(terminate)) {}

    std::string_view name() const override { return name_; }
    void initialize() override {
        if (initialize_) {
            initialize_();
        }
    }
    void terminate() override {
        if (terminate_) {
            terminate_();
        }
    }

private:
    std::string name_;
    std::function<void()> initialize_;
    std::function<void()> terminate_;
};

// Registration order is initialisation order; teardown goes in reverse, as in
// NVDA's resetConfiguration.
class SubsystemRegistry {
public:
    void add(Subsystem* subsystem);
    bool remove(Subsystem* subsystem);
    void clear();

    void initializeAll();
    void terminateAll();

    std::size_t size() const noexcept { return subsystems_.size(); }
    bool initialized() const noexcept { return initialized_; }

private:
    std::vector<Subsystem*> subsystems_;
    bool initialized_ = false;
};

// NVDA: core.resetConfiguration (core.py:315-419). Terminate everything in
// reverse order, reload the configuration, bring everything back up. One
// subsystem that throws on the way down or up must not abort the rest, or the
// screen reader would be left half-dead.
void resetConfiguration(Manager& manager, SubsystemRegistry& subsystems,
                        bool factoryDefaults = false);

}  // namespace alus::config
