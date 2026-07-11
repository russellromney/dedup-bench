/**
 * @file mincdc_chunking.hpp
 * @brief MinCDC / mincatcdc chunking via the mincatcdc Rust library
 *        (https://github.com/russellromney/mincatcdc, feature `capi`).
 *
 * Two modes, selected by the `mincdc_caterpillar` config key:
 *  - false: plain MinCDC (SIMD argmin boundary search, MinCDCHash4).
 *  - true:  the packed caterpillar fast path — on repetitive data one packed
 *           equality scan proves several boundaries at once; find_cutpoint
 *           answers those from `pending_repeats` without re-entering the
 *           library. The produced boundaries are bit-identical to plain mode.
 */
#ifndef _MINCDC_CHUNKING_
#define _MINCDC_CHUNKING_

#include <cstddef>
#include <cstdint>

#include "chunking_common.hpp"
#include "config.hpp"

extern "C" {
/// From libmincatcdc.a (cargo build --release --features capi).
size_t mincatcdc_next_chunk(const uint8_t *data, size_t len, size_t min_size,
                            size_t max_size, int eof, size_t *repeats_out);
}

class MinCDC_Chunking : public virtual Chunking_Technique {
   private:
    uint64_t min_block_size;
    uint64_t max_block_size;
    bool caterpillar;
    // Boundaries already proven by the last packed scan (caterpillar mode).
    uint64_t pending_repeats = 0;
    uint64_t pending_size = 0;

   public:
    MinCDC_Chunking(const Config &config);

    uint64_t find_cutpoint(char *buff, uint64_t size) override;
};

#endif
