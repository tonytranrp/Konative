#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <cereal/archives/binary.hpp>
#include <entt/entt.hpp>

#include "konative/core/result.hpp"
#include "konative/ecs/registry.hpp"

// Durable, file-backed EnTT snapshots - the piece that turns cereal's stated reason for being
// chosen ("binary save-state snapshots via EnTT's native snapshot API",
// KonativeDependencies.cmake) from an in-memory-only proof into real persistence. Until this file,
// the only production snapshot consumer (jni_onload.cpp's periodic on_tick() snapshot) serialized
// real ECS state and then threw the bytes away - only their COUNT ever crossed the
// SnapshotSavedEvent boundary - and entt::snapshot_loader had never restored anything outside the
// self-check's scratch registries.
//
// Split into two halves on purpose, mirroring the producer's real threading constraints
// (SnapshotSavedEvent.hpp's own comment): SERIALIZATION must happen on the thread that owns the
// registry (entt::snapshot reads live storage - snapshotting concurrently with mutation is a real
// data race), while the byte-buffer FILE WRITE is thread-agnostic and belongs off the hot path.
// write_snapshot_bytes_file() therefore takes already-serialized bytes (safe from any thread);
// read_snapshot_file() does the full open/validate/restore (a startup-time, owning-thread call).
namespace konative::ecs {

// Which step failed - same "name the specific step" Result<T, E> convention as
// jni::DexLoadError/jni::FilesDirError.
enum class SnapshotFileError {
    kOpenFailed,      // read: file missing or unreadable; write: the temp file couldn't be created
    kBadHeader,       // read: too short for a header, or the magic isn't a Konative snapshot's
    kVersionMismatch, // read: a real Konative snapshot, but a layout version this build doesn't read
    kArchiveError,    // read: header fine, but cereal/EnTT failed on the payload (corrupt/truncated)
    kWriteFailed,     // write: streaming the bytes out failed
    kRenameFailed,    // write: atomically moving the temp file into place failed
};

// 8-byte header preceding the cereal archive: a magic (identifies the file kind - catches "this
// isn't a snapshot at all" before cereal throws somewhere less explicable) and a caller-owned
// layout version (cereal's binary archive is not self-describing: a component gaining/losing a
// field silently mis-parses an old file rather than erroring, so the version is the caller's ONE
// honest lever to invalidate stale files across layout changes - bump it when a serialized
// component's shape changes). Written in native byte order, deliberately: a state file lives and
// dies in one app install's private storage, never transferred between machines - cross-endian
// portability would be speculative machinery for a file that never travels.
inline constexpr std::uint32_t kSnapshotFileMagic = 0x504E534BU; // "KSNP" as little-endian bytes

// Writes `bytes` (an already-serialized cereal archive - see the threading note above) to `path`
// atomically: streamed to a sibling temp file first, then renamed into place, so a crash or a
// concurrent reader can never observe a half-written state file at `path` itself.
// `unique_suffix` disambiguates the temp file name across overlapping writers (the real producer
// is a detached background thread per snapshot - two would collide on one fixed temp name; the
// caller's tick count is unique per snapshot by construction). Returns the payload byte count.
inline core::Result<std::size_t, SnapshotFileError> write_snapshot_bytes_file(
    const std::string& bytes, const std::string& path, std::uint64_t unique_suffix,
    std::uint32_t version) {
    using R = core::Result<std::size_t, SnapshotFileError>;

    const std::string temp_path = path + ".tmp" + std::to_string(unique_suffix);
    {
        std::ofstream stream(temp_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return R::err(SnapshotFileError::kOpenFailed);
        }
        stream.write(reinterpret_cast<const char*>(&kSnapshotFileMagic), sizeof(kSnapshotFileMagic));
        stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream.good()) {
            return R::err(SnapshotFileError::kWriteFailed);
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, path, ec); // replaces an existing file on POSIX and Windows
    if (ec) {
        std::filesystem::remove(temp_path, ec); // best-effort cleanup; the error to report is the rename
        return R::err(SnapshotFileError::kRenameFailed);
    }
    return R::ok(bytes.size());
}

namespace detail {

// Counts every entity currently alive in `registry` - the honest "how much did the restore
// actually restore" number for the caller's logging, independent of which components each entity
// carries. free_list() on the entity storage IS the in-use count - confirmed against EnTT's own
// vendored basic_snapshot::get<entity_type>() source, which archives exactly
// `storage->free_list()` as the snapshot's own in-use-entities figure, not assumed.
inline std::size_t alive_entity_count(Registry& registry) {
    return static_cast<std::size_t>(registry.storage<Entity>().free_list());
}

} // namespace detail

// Restores a snapshot file written by write_snapshot_bytes_file() into `registry`, which MUST be
// empty (entt::snapshot_loader's own documented precondition). Components... must list the exact
// component set, in the exact order, the serialized archive was built with - cereal's binary
// format has no per-component framing to detect a mismatch, which is exactly what the version
// header exists to guard.
//
// Failure never leaves a half-restored registry: any post-header failure clears the registry back
// to the empty state the precondition required anyway (a partial entt::snapshot_loader::get() can
// genuinely have created entities before the archive threw). The catch(...) is deliberately
// broader than cereal::Exception alone - the payload is untrusted file content by the time it's
// being parsed (a truncated write, a flipped bit, a stale layout), and EnTT's own release-mode
// behavior on nonsense counts is not a contract worth betting the whole app's startup on; this is
// exactly the "prefer Result over an exception crossing a boundary" case core/result.hpp exists
// for. Returns the number of entities restored.
template <typename... Components>
core::Result<std::size_t, SnapshotFileError> read_snapshot_file(Registry& registry,
                                                                 const std::string& path,
                                                                 std::uint32_t expected_version) {
    using R = core::Result<std::size_t, SnapshotFileError>;

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return R::err(SnapshotFileError::kOpenFailed);
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!stream.good() || magic != kSnapshotFileMagic) {
        return R::err(SnapshotFileError::kBadHeader);
    }
    if (version != expected_version) {
        return R::err(SnapshotFileError::kVersionMismatch);
    }

    try {
        cereal::BinaryInputArchive archive(stream); // reads from the current offset - past the header
        entt::snapshot_loader loader{registry};
        loader.get<Entity>(archive);
        (loader.get<Components>(archive), ...);
    } catch (...) {
        registry.clear();
        return R::err(SnapshotFileError::kArchiveError);
    }

    return R::ok(detail::alive_entity_count(registry));
}

} // namespace konative::ecs
