// Linker-time shim: when bench_runner is linked against the OptFSST sources
// with -Wl,--wrap=fsst_create -Wl,--wrap=fsst_compress, this file provides
// the wrappers that route the legacy API through OptFSST with all three
// table-construction contributions plus DP encoding enabled, so FSST+ picks
// up OptFSST behavior without any source change to FSST+ or OptFSST.

#include "fsst.h"

#include <cstddef>

extern "C" {

fsst_encoder_t* Optfsst_create(size_t n, const size_t lenIn[],
                               const unsigned char* strIn[],
                               int zeroTerminated,
                               const fsst_options_t* opt);

size_t Optfsst_compress(fsst_encoder_t* encoder,
                        size_t nstrings, const size_t lenIn[],
                        const unsigned char* strIn[],
                        size_t outsize, unsigned char* output,
                        size_t lenOut[], unsigned char* strOut[],
                        const fsst_options_t* opt);

fsst_encoder_t* __wrap_fsst_create(size_t n,
                                   const size_t lenIn[],
                                   const unsigned char* strIn[],
                                   int zeroTerminated) {
    fsst_options_t opt{};
    opt.flags = FSST_OPT_DP_TRAIN | FSST_OPT_TRIPLES | FSST_OPT_PRUNE;
    return Optfsst_create(n, lenIn, strIn, zeroTerminated, &opt);
}

size_t __wrap_fsst_compress(fsst_encoder_t* encoder,
                            size_t nlines,
                            const size_t lenIn[],
                            const unsigned char* strIn[],
                            size_t size,
                            unsigned char* output,
                            size_t lenOut[],
                            unsigned char* strOut[]) {
    fsst_options_t opt{};
    opt.flags = FSST_OPT_DP_ENCODE;
    return Optfsst_compress(encoder, nlines, lenIn, strIn, size, output,
                            lenOut, strOut, &opt);
}

} // extern "C"
