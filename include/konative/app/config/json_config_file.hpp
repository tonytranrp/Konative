#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include <entt/entt.hpp>

#include "konative/reflect/meta_glaze_json.hpp"

// A real, file-backed JSON config source for any entt::meta-reflected component struct - the piece
// that turns KonativeDependencies.cmake's own stated reason for choosing Glaze ("macro-free
// reflection-driven JSON, for config/hot-reload") from a claim into working machinery. Until this
// file, the read direction (meta_component_from_json) only ever parsed a compiled-in string
// literal in jni_onload.cpp, and the write direction (meta_component_to_json) had ZERO production
// call sites at all (only tests/self-checks) - provisioning a missing config file below is its
// first real one.
//
// Deliberately polling-based (an mtime check per poll_reload() call), not inotify/FileObserver-
// based: the one real consumer (KonativeAndroidApp) already has a per-frame tick as a natural
// polling point, a stat() every couple of seconds is unmeasurable next to a 60-120Hz frame loop,
// and an OS-notification path would be a per-platform mechanism (inotify on Android/Linux,
// ReadDirectoryChangesW on Windows) for zero observable behavior difference at this scale. Same
// "smallest real mechanism that genuinely works" reasoning as CrossThreadEventQueue's
// drain-per-tick design.
namespace konative::app::config {

// What load_or_provision() actually did - three genuinely different situations the caller (which
// owns logging policy - this header never logs) reports differently.
enum class LoadOutcome {
    kProvisionedNewFile, // no file existed; the instance's current values were written as the new file
    kLoadedExistingFile, // a file existed and parsed cleanly; instance now holds its values
    kFailed,             // a file existed but couldn't be read/parsed, or provisioning couldn't write
};

// What poll_reload() actually did. kUnchanged is the overwhelmingly common case (nothing to do);
// kFailed means the file CHANGED but couldn't be parsed - reported once per real file change, not
// once per poll (see poll_reload()'s own comment for the mtime-recording reasoning).
enum class PollOutcome {
    kUnchanged, // file missing, or mtime identical to the last seen one - nothing happened
    kReloaded,  // file changed and parsed cleanly; instance now holds the new values
    kFailed,    // file changed but couldn't be read/parsed; instance left untouched
};

// Owns one JSON file path and the entt::meta_type of the component struct stored in it. T is the
// same struct the meta_type reflects (the two must agree - the same pairing
// meta_component_from_json's own signature already requires, for the documented meta_any
// copy-construction reason).
//
// Failure never tears: both load_or_provision() and poll_reload() parse into a local copy first
// and only assign to the caller's instance on full success, so a half-parsed file can never leave
// the live config in a mixed old/new state (meta_component_from_json sets fields one at a time and
// returns false mid-way on the first bad one - without the copy, an early field could already have
// been written by the time a later one fails).
template <typename T>
class JsonConfigFile {
    static_assert(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>,
                  "JsonConfigFile<T>: T must be copyable - the untouched-on-failure guarantee is "
                  "implemented by parsing into a local copy and assigning back on success");

public:
    JsonConfigFile(std::string path, entt::meta_type type)
        : path_(std::move(path)), type_(type) {}

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // If the file doesn't exist yet: serialize `instance`'s CURRENT values through the real
    // reflection write path (meta_component_to_json) and create the file from them - so a fresh
    // install self-provisions an editable, discoverable config file carrying the real effective
    // defaults, instead of requiring a user to guess the schema. If the file exists: parse it into
    // `instance` (partial JSON is fine - absent fields keep whatever `instance` already holds,
    // meta_component_from_json's own documented partial-update semantics).
    //
    // On kFailed the file's mtime is still recorded, so a subsequent poll_reload() doesn't
    // immediately re-fail on the exact same unchanged content - only a real, new edit re-triggers
    // a parse attempt. `instance` is left untouched on any failure.
    LoadOutcome load_or_provision(T& instance) {
        std::error_code ec;
        const bool file_exists = std::filesystem::exists(path_, ec);
        if (ec) {
            return LoadOutcome::kFailed;
        }

        if (!file_exists) {
            const std::string json = konative::reflect::meta_component_to_json(type_, instance);
            if (json.empty()) {
                return LoadOutcome::kFailed;
            }
            if (!write_file(json)) {
                return LoadOutcome::kFailed;
            }
            record_mtime();
            return LoadOutcome::kProvisionedNewFile;
        }

        record_mtime();
        if (!parse_file_into(instance)) {
            return LoadOutcome::kFailed;
        }
        return LoadOutcome::kLoadedExistingFile;
    }

    // Cheap enough to call from a per-frame path at a modest interval: one std::filesystem
    // last_write_time() stat when nothing changed (the overwhelmingly common case), a full
    // read+parse only when the file's mtime genuinely moved.
    //
    // The new mtime is recorded BEFORE parsing, even if the parse then fails - deliberate, two
    // real reasons: (1) a complete-but-invalid edit (someone saved broken JSON) fails ONCE, not
    // once per poll forever, keeping the caller's error logging proportional to real events;
    // (2) a torn read (polling mid-write) self-heals anyway, because the writer's remaining
    // write()s bump mtime past what was recorded here, so the next poll re-reads the completed
    // file. A file that disappears (or whose mtime can't be read) counts as kUnchanged, not an
    // error - mid-rewrite editors genuinely do this transiently.
    PollOutcome poll_reload(T& instance) {
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(path_, ec);
        if (ec || mtime == last_seen_mtime_) {
            return PollOutcome::kUnchanged;
        }
        last_seen_mtime_ = mtime;

        if (!parse_file_into(instance)) {
            return PollOutcome::kFailed;
        }
        return PollOutcome::kReloaded;
    }

private:
    // Binary mode both directions - JSON treats \r\n as plain whitespace, so translation buys
    // nothing, and identical bytes on disk across Windows (desktop tests) and Android (production)
    // keeps the provisioned file's SHA/diff behavior deterministic.
    [[nodiscard]] bool write_file(const std::string& json) const {
        std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(json.data(), static_cast<std::streamsize>(json.size()));
        stream.flush();
        return stream.good();
    }

    [[nodiscard]] bool parse_file_into(T& instance) const {
        std::ifstream stream(path_, std::ios::binary);
        if (!stream) {
            return false;
        }
        std::ostringstream contents;
        contents << stream.rdbuf();
        if (stream.bad()) {
            return false;
        }

        T candidate = instance; // untouched-on-failure: parse into a copy, assign on success only
        if (!konative::reflect::meta_component_from_json(type_, candidate, contents.str())) {
            return false;
        }
        instance = candidate;
        return true;
    }

    void record_mtime() {
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(path_, ec);
        if (!ec) {
            last_seen_mtime_ = mtime;
        }
    }

    std::string path_;
    entt::meta_type type_;
    std::filesystem::file_time_type last_seen_mtime_{};
};

} // namespace konative::app::config
