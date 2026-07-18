#pragma once

#include <cstddef>
#include <vector>

// The sanctioned pattern for parallelizing EnTT view iteration, per EnTT's own maintainer
// (entt::view::handle() gives an indexable, splittable range - see ARCHITECTURE.md \xc2\xa75). This
// helper only computes index-range slices; it does not know about entt::view or Taskflow, so it
// stays reusable regardless of which scheduler consumes the slices.
namespace konative::scheduling::detail {

struct IndexRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

inline std::vector<IndexRange> split_evenly(std::size_t count, std::size_t num_slices) {
    std::vector<IndexRange> slices;
    if (num_slices == 0 || count == 0) {
        return slices;
    }
    slices.reserve(num_slices);
    const std::size_t chunk = count / num_slices;
    const std::size_t remainder = count % num_slices;
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < num_slices; ++i) {
        const std::size_t this_chunk = chunk + (i < remainder ? 1 : 0);
        slices.push_back(IndexRange{cursor, cursor + this_chunk});
        cursor += this_chunk;
    }
    return slices;
}

} // namespace konative::scheduling::detail
