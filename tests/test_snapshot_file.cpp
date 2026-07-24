#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <cereal/archives/binary.hpp>
#include <entt/entt.hpp>

#include "konative/ecs/registry.hpp"
#include "konative/ecs/snapshot_file.hpp"

// Desktop coverage for ecs/snapshot_file.hpp against real files - the same write-bytes-then-
// restore lifecycle jni_onload.cpp's periodic snapshot + startup restore runs on-device, plus
// every failure shape a real state file can arrive in (missing, not-a-snapshot, stale version,
// truncated payload).

namespace {

// Real namespace scope (the GCC external-linkage rule every test-local reflected/serialized type
// in this codebase follows), with an ADL-visible serialize() as cereal requires.
struct SavedCounter {
    std::uint64_t ticks = 0;
};

template <class Archive>
void serialize(Archive& archive, SavedCounter& counter) {
    archive(counter.ticks);
}

constexpr std::uint32_t kTestVersion = 1;

std::filesystem::path fresh_scratch_dir(const char* case_name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "konative_snapshot_file_tests" / case_name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

// The exact main-thread serialization pattern the real producer (jni_onload.cpp's on_tick) uses.
std::string serialize_registry(konative::ecs::Registry& registry) {
    std::ostringstream buffer;
    {
        cereal::BinaryOutputArchive archive(buffer);
        entt::snapshot{registry}
            .get<konative::ecs::Entity>(archive)
            .get<SavedCounter>(archive);
    }
    return buffer.str();
}

} // namespace

TEST_CASE("snapshot_file: a real registry round-trips through a real file, identity and values intact") {
    const auto dir = fresh_scratch_dir("round-trip");
    const std::string path = (dir / "state.bin").string();

    konative::ecs::Registry source;
    for (std::uint64_t i = 0; i < 3; ++i) {
        source.emplace<SavedCounter>(source.create(), SavedCounter{i * 1000});
    }

    const std::string bytes = serialize_registry(source);
    auto written = konative::ecs::write_snapshot_bytes_file(bytes, path, 0, kTestVersion);
    REQUIRE(written);
    CHECK(written.value() == bytes.size());
    REQUIRE(std::filesystem::exists(path));

    konative::ecs::Registry restored;
    auto result = konative::ecs::read_snapshot_file<SavedCounter>(restored, path, kTestVersion);
    REQUIRE(result);
    CHECK(result.value() == 3);
    for (auto [entity, counter] : source.view<SavedCounter>().each()) {
        REQUIRE(restored.valid(entity)); // identity preserved, not just counts
        CHECK(restored.get<SavedCounter>(entity).ticks == counter.ticks);
    }
}

TEST_CASE("snapshot_file: overwriting an existing state file replaces it atomically, later read sees the newer state") {
    const auto dir = fresh_scratch_dir("overwrite");
    const std::string path = (dir / "state.bin").string();

    konative::ecs::Registry first;
    first.emplace<SavedCounter>(first.create(), SavedCounter{1});
    REQUIRE(konative::ecs::write_snapshot_bytes_file(serialize_registry(first), path, 1, kTestVersion));

    konative::ecs::Registry second;
    second.emplace<SavedCounter>(second.create(), SavedCounter{999});
    second.emplace<SavedCounter>(second.create(), SavedCounter{998});
    REQUIRE(konative::ecs::write_snapshot_bytes_file(serialize_registry(second), path, 2, kTestVersion));

    konative::ecs::Registry restored;
    auto result = konative::ecs::read_snapshot_file<SavedCounter>(restored, path, kTestVersion);
    REQUIRE(result);
    CHECK(result.value() == 2); // the second write's state, not the first's
    // No stray temp files left behind either - the unique-suffix temps were both renamed away.
    std::size_t files_in_dir = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(dir)) {
        ++files_in_dir;
    }
    CHECK(files_in_dir == 1);
}

TEST_CASE("snapshot_file: a missing file is kOpenFailed - the normal first-boot case, distinguishable from corruption") {
    const auto dir = fresh_scratch_dir("missing");
    konative::ecs::Registry registry;
    auto result = konative::ecs::read_snapshot_file<SavedCounter>(
        registry, (dir / "never_written.bin").string(), kTestVersion);
    REQUIRE_FALSE(result);
    CHECK(result.error() == konative::ecs::SnapshotFileError::kOpenFailed);
}

TEST_CASE("snapshot_file: a file that isn't a Konative snapshot at all is kBadHeader") {
    const auto dir = fresh_scratch_dir("bad-header");
    const std::string path = (dir / "state.bin").string();
    {
        std::ofstream stream(path, std::ios::binary);
        stream << "definitely not a snapshot";
    }
    konative::ecs::Registry registry;
    auto result = konative::ecs::read_snapshot_file<SavedCounter>(registry, path, kTestVersion);
    REQUIRE_FALSE(result);
    CHECK(result.error() == konative::ecs::SnapshotFileError::kBadHeader);
}

TEST_CASE("snapshot_file: a snapshot written under a different layout version is kVersionMismatch") {
    const auto dir = fresh_scratch_dir("version");
    const std::string path = (dir / "state.bin").string();

    konative::ecs::Registry source;
    source.emplace<SavedCounter>(source.create(), SavedCounter{7});
    REQUIRE(konative::ecs::write_snapshot_bytes_file(serialize_registry(source), path, 0,
                                                       kTestVersion));

    konative::ecs::Registry registry;
    auto result =
        konative::ecs::read_snapshot_file<SavedCounter>(registry, path, kTestVersion + 1);
    REQUIRE_FALSE(result);
    CHECK(result.error() == konative::ecs::SnapshotFileError::kVersionMismatch);
}

TEST_CASE("snapshot_file: a truncated payload is kArchiveError and the registry comes back EMPTY, never half-restored") {
    const auto dir = fresh_scratch_dir("truncated");
    const std::string full_path = (dir / "full.bin").string();
    const std::string cut_path = (dir / "cut.bin").string();

    konative::ecs::Registry source;
    for (std::uint64_t i = 0; i < 5; ++i) {
        source.emplace<SavedCounter>(source.create(), SavedCounter{i});
    }
    REQUIRE(konative::ecs::write_snapshot_bytes_file(serialize_registry(source), full_path, 0,
                                                       kTestVersion));

    // Rewrite the valid file cut to 60% of its length - header intact, payload torn mid-archive
    // (the exact shape a crash mid-write would produce WITHOUT the temp-file+rename design; the
    // read side must still survive it, belt and braces).
    {
        std::ifstream in(full_path, std::ios::binary);
        std::ostringstream all;
        all << in.rdbuf();
        const std::string full = all.str();
        std::ofstream out(cut_path, std::ios::binary);
        out.write(full.data(), static_cast<std::streamsize>((full.size() * 6) / 10));
    }

    konative::ecs::Registry registry;
    auto result = konative::ecs::read_snapshot_file<SavedCounter>(registry, cut_path, kTestVersion);
    REQUIRE_FALSE(result);
    CHECK(result.error() == konative::ecs::SnapshotFileError::kArchiveError);
    // The clear-on-failure guarantee: a partial entt::snapshot_loader pass may genuinely have
    // created entities before the archive underflowed - none may leak out.
    CHECK(registry.storage<konative::ecs::Entity>().free_list() == 0);
}

TEST_CASE("snapshot_file: writing into a nonexistent directory is kOpenFailed, no crash") {
    const auto dir = fresh_scratch_dir("write-missing-dir");
    konative::ecs::Registry source;
    source.emplace<SavedCounter>(source.create(), SavedCounter{1});
    auto result = konative::ecs::write_snapshot_bytes_file(
        serialize_registry(source), (dir / "no" / "such" / "dir" / "state.bin").string(), 0,
        kTestVersion);
    REQUIRE_FALSE(result);
    CHECK(result.error() == konative::ecs::SnapshotFileError::kOpenFailed);
}
