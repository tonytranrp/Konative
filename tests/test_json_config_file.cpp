#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <entt/entt.hpp>

#include "konative/app/app_config.hpp"
#include "konative/app/config/json_config_file.hpp"
#include "konative/reflect/pfr_auto_registration.hpp"

// Desktop coverage for app/config/json_config_file.hpp against the REAL konative::app::AppConfig
// (the same type/registration jni_onload.cpp uses), on real files in the real filesystem - not
// mocks. Every case here is a behavior the on-device path genuinely hits: first-run provisioning,
// a later run loading the provisioned file, a live edit hot-reloading, a broken edit failing
// exactly once, and the untouched-on-failure guarantee.

namespace {

// One shared registration for the whole file - reflect_component_auto is process-global and
// idempotent for the same type/id pair, so TEST_CASEs can all resolve the same id safely.
entt::meta_type app_config_meta_type() {
    constexpr entt::id_type kId = entt::hashed_string{"test::JsonConfigFile::AppConfig"};
    konative::reflect::reflect_component_auto<konative::app::AppConfig>(kId);
    return entt::resolve(kId);
}

// A fresh, empty scratch directory per test case, under the OS temp dir - remove_all() first makes
// every case idempotent across runs (a crashed previous run can't leave state that changes this
// run's behavior).
std::filesystem::path fresh_scratch_dir(const char* case_name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "konative_json_config_file_tests" / case_name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::string read_whole_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(static_cast<bool>(stream));
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Rewrites the file's content AND explicitly bumps its mtime forward - the explicit bump makes the
// "file changed" signal deterministic regardless of the host filesystem's timestamp granularity
// (FAT's 2s, or a very fast write landing inside one NTFS/ext4 timer quantum), instead of
// sleep()ing and hoping.
void rewrite_file_with_newer_mtime(const std::filesystem::path& path, const std::string& content) {
    const auto previous_mtime = std::filesystem::last_write_time(path);
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        REQUIRE(static_cast<bool>(stream));
        stream << content;
    }
    std::filesystem::last_write_time(path, previous_mtime + std::chrono::seconds(2));
}

} // namespace

TEST_CASE("JsonConfigFile: first run provisions a real file from the instance's current values") {
    const auto dir = fresh_scratch_dir("provision");
    const auto path = dir / "konative_config.json";
    konative::app::config::JsonConfigFile<konative::app::AppConfig> file(path.string(),
                                                                          app_config_meta_type());

    konative::app::AppConfig config{};
    REQUIRE(file.load_or_provision(config) ==
            konative::app::config::LoadOutcome::kProvisionedNewFile);

    // The instance itself is untouched (its values WERE the source), and the file on disk is real,
    // parseable JSON carrying exactly those values - proven by round-tripping it back through the
    // same reflection machinery rather than string-matching a specific field order.
    CHECK(config.tick_log_interval == 120);
    CHECK(config.snapshot_interval_ticks == 300);
    REQUIRE(std::filesystem::exists(path));
    konative::app::AppConfig reparsed{};
    reparsed.tick_log_interval = -1;
    reparsed.snapshot_interval_ticks = -1;
    REQUIRE(konative::reflect::meta_component_from_json(app_config_meta_type(), reparsed,
                                                          read_whole_file(path)));
    CHECK(reparsed.tick_log_interval == 120);
    CHECK(reparsed.snapshot_interval_ticks == 300);
}

TEST_CASE("JsonConfigFile: a later run loads the existing file, partial JSON keeps struct defaults") {
    const auto dir = fresh_scratch_dir("load-existing");
    const auto path = dir / "konative_config.json";
    {
        std::ofstream stream(path, std::ios::binary);
        stream << R"({"tick_log_interval":45})"; // deliberately partial - no snapshot_interval_ticks
    }

    konative::app::config::JsonConfigFile<konative::app::AppConfig> file(path.string(),
                                                                          app_config_meta_type());
    konative::app::AppConfig config{};
    REQUIRE(file.load_or_provision(config) ==
            konative::app::config::LoadOutcome::kLoadedExistingFile);
    CHECK(config.tick_log_interval == 45);        // from the file
    CHECK(config.snapshot_interval_ticks == 300); // absent from the file - struct default kept
}

TEST_CASE("JsonConfigFile: poll_reload is kUnchanged until the file genuinely changes, then reloads") {
    const auto dir = fresh_scratch_dir("hot-reload");
    const auto path = dir / "konative_config.json";
    konative::app::config::JsonConfigFile<konative::app::AppConfig> file(path.string(),
                                                                          app_config_meta_type());
    konative::app::AppConfig config{};
    REQUIRE(file.load_or_provision(config) ==
            konative::app::config::LoadOutcome::kProvisionedNewFile);

    // No edit yet - polling any number of times does nothing.
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kUnchanged);
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kUnchanged);

    // A real edit - the exact on-device hot-reload scenario (a human editing the provisioned file
    // while the app keeps running).
    rewrite_file_with_newer_mtime(path, R"({"tick_log_interval":60,"snapshot_interval_ticks":900})");
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kReloaded);
    CHECK(config.tick_log_interval == 60);
    CHECK(config.snapshot_interval_ticks == 900);

    // And back to quiet afterward.
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kUnchanged);
}

TEST_CASE("JsonConfigFile: a broken edit fails exactly once, leaves the instance untouched, then goes quiet") {
    const auto dir = fresh_scratch_dir("broken-edit");
    const auto path = dir / "konative_config.json";
    konative::app::config::JsonConfigFile<konative::app::AppConfig> file(path.string(),
                                                                          app_config_meta_type());
    konative::app::AppConfig config{};
    REQUIRE(file.load_or_provision(config) ==
            konative::app::config::LoadOutcome::kProvisionedNewFile);

    rewrite_file_with_newer_mtime(path, "{ this is not json");
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kFailed);
    // Untouched-on-failure: the live values survive a bad edit.
    CHECK(config.tick_log_interval == 120);
    CHECK(config.snapshot_interval_ticks == 300);
    // The failure was recorded against that mtime - the same unchanged broken content does NOT
    // re-fail on every subsequent poll (the caller's error logging stays proportional to real
    // edits, the header's own documented contract).
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kUnchanged);

    // Fixing the file recovers, no restart needed.
    rewrite_file_with_newer_mtime(path, R"({"tick_log_interval":75})");
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kReloaded);
    CHECK(config.tick_log_interval == 75);
}

TEST_CASE("JsonConfigFile: loading an existing broken file fails, keeps defaults, and a fixing edit hot-reloads") {
    const auto dir = fresh_scratch_dir("broken-existing");
    const auto path = dir / "konative_config.json";
    {
        std::ofstream stream(path, std::ios::binary);
        stream << "not json at all";
    }

    konative::app::config::JsonConfigFile<konative::app::AppConfig> file(path.string(),
                                                                          app_config_meta_type());
    konative::app::AppConfig config{};
    REQUIRE(file.load_or_provision(config) == konative::app::config::LoadOutcome::kFailed);
    CHECK(config.tick_log_interval == 120); // untouched - struct defaults still in force
    CHECK(config.snapshot_interval_ticks == 300);

    // The broken content's mtime was recorded by the failed load - unchanged content stays quiet...
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kUnchanged);
    // ...and a real fix is picked up live, without any restart.
    rewrite_file_with_newer_mtime(path, R"({"snapshot_interval_ticks":150})");
    CHECK(file.poll_reload(config) == konative::app::config::PollOutcome::kReloaded);
    CHECK(config.tick_log_interval == 120);
    CHECK(config.snapshot_interval_ticks == 150);
}

TEST_CASE("JsonConfigFile: provisioning into a nonexistent directory fails cleanly, no crash") {
    const auto dir = fresh_scratch_dir("missing-dir");
    const auto path = dir / "no" / "such" / "subdir" / "konative_config.json";
    konative::app::config::JsonConfigFile<konative::app::AppConfig> file(path.string(),
                                                                          app_config_meta_type());
    konative::app::AppConfig config{};
    CHECK(file.load_or_provision(config) == konative::app::config::LoadOutcome::kFailed);
    CHECK(config.tick_log_interval == 120); // untouched either way
}
