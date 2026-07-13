/**
 * @file mincdc_chunking.cpp
 * @brief MinCDC / mothcdc chunking technique (see mincdc_chunking.hpp).
 */
#include "mincdc_chunking.hpp"

#include <stdexcept>

MinCDC_Chunking::MinCDC_Chunking(const Config &config) {
    min_block_size = config.get_mincdc_min_block_size();
    max_block_size = config.get_mincdc_max_block_size();
    caterpillar = config.get_mincdc_caterpillar();
    coalesce_identical_metadata_records(caterpillar);
    technique_name =
        caterpillar ? "MinCDC (mothcdc, packed caterpillar)" : "MinCDC (mothcdc)";
    // chunk_stream keeps its buffer full except at end of stream, so a
    // buffer shorter than one full decision window implies eof — but only
    // if the buffer can hold a full window in the first place.
    if (config.get_buffer_size() < max_block_size + 1) {
        throw std::runtime_error(
            "mincdc requires buffer_size >= mincdc_max_block_size + 1");
    }
}

uint64_t MinCDC_Chunking::find_cutpoint(char *buff, uint64_t size) {
    if (pending_repeats > 0) {
        // Boundary already proven identical by the packed scan; the stream
        // advanced exactly one unit, so the proof carries over verbatim.
        pending_repeats--;
        return pending_size;
    }
    size_t repeats = 0;
    // Buffer shorter than a full decision window => end of stream (the
    // driver never under-fills mid-stream); the library then uses its
    // truncated-window (eof) branches, matching SliceChunker semantics.
    int eof = size < max_block_size + 1;
    size_t len = mothcdc_next_chunk(
        reinterpret_cast<const uint8_t *>(buff), size, min_block_size,
        max_block_size, eof, caterpillar ? &repeats : nullptr);
    if (len == 0) {
        // Unreachable under the eof rule for size > 0; never emit a
        // zero-length chunk (it would loop the driver forever).
        return size;
    }
    if (repeats > 0) {
        pending_repeats = repeats;
        pending_size = len;
    }
    return len;
}
