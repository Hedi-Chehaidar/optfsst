// Stand-alone benchmark driver for FSST+ (vanilla and OptFSST+ variants).
// Reads a UTF-8 text corpus, one string per line, times the full FSST+ pipeline
// (compression and decompression), verifies a byte-exact round-trip, and writes
// a single CSV row per run on stdout.
//
// Usage:
//   bench_runner <input.txt> <dataset_name> <column_name> <variant_tag> <repetitions>
//
// CSV row written to stdout:
//   variant,dataset,column,n_strings,raw_bytes,compressed_bytes,
//   compression_factor,compress_ms_mean,decompress_ms_mean,
//   compress_mb_per_s_mean,decompress_mb_per_s_mean

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "duckdb.hpp"
#include "config.h"
#include "global.h"
#include "cleaving/cleaving_types.h"
#include "cleaving/cleaving.h"
#include "block/block_types.h"
#include "block/block_sizer.h"
#include "block/block_writer.h"
#include "block/block_decompressor.h"
#include "util/basic_fsst.h"
#include "fsst_plus.h"

using duckdb::Store;
using duckdb::Load;

// config.h forward-declares these three; provide a single definition per TU.
namespace config {
    const bool print_sorted_corpus = false;
    const bool print_split_points = false;
    const bool print_decompressed_corpus = false;
}

// ---- Pipeline functions copied verbatim from fsst_plus.cpp -----------------
// The originals live in fsst_plus.cpp which also defines main(); we re-declare
// them locally so this TU can build into its own executable.

static uint8_t *FindBlockStart(uint8_t *block_start_offsets, const int i) {
    uint8_t *offset_ptr = block_start_offsets + (i * sizeof(uint32_t));
    const uint32_t offset = Load<uint32_t>(offset_ptr);
    return offset_ptr + offset;
}

static void DecompressAllSilent(uint8_t *global_header, const fsst_decoder_t &decoder,
                                const std::vector<size_t> &lengths_original,
                                const std::vector<const unsigned char *> &string_ptrs_original) {
    Metadata m{};
    m.global_index = 0;
    uint16_t num_blocks = Load<uint16_t>(global_header);
    uint8_t *block_start_offsets = global_header + sizeof(uint16_t);
    for (int i = 0; i < num_blocks; ++i) {
        const uint8_t *block_start = FindBlockStart(block_start_offsets, i);
        const uint8_t *block_stop = FindBlockStart(block_start_offsets, i + 1);
        DecompressBlock(block_start, decoder, block_stop, lengths_original, string_ptrs_original, m);
    }
}

static std::vector<SimilarityChunk>
FormBlockwiseSimilarityChunksLocal(const size_t &n,
                                   FSSTCompressionResult &input_compressed,
                                   StringCollection &input) {
    std::vector<SimilarityChunk> similarity_chunks;
    similarity_chunks.reserve(n);
    for (size_t i = 0; i < n; i += config::block_granularity) {
        const size_t cleaving_run_n =
            std::min(input_compressed.encoded_string_lengths.size() - i, config::block_granularity);
        TruncatedSort(input_compressed.encoded_string_lengths,
                      input_compressed.encoded_string_ptrs,
                      i, cleaving_run_n, input);
        const std::vector<SimilarityChunk> cleaving_run_similarity_chunks =
            FormSimilarityChunks(input_compressed.encoded_string_lengths,
                                 input_compressed.encoded_string_ptrs, i, cleaving_run_n);
        similarity_chunks.insert(similarity_chunks.end(),
                                 cleaving_run_similarity_chunks.begin(),
                                 cleaving_run_similarity_chunks.end());
    }
    return similarity_chunks;
}

static FSSTPlusCompressionResult
FSSTPlusCompressLocal(const size_t n,
                      const std::vector<SimilarityChunk> &similarity_chunks,
                      const CleavedResult &cleaved_result,
                      fsst_encoder_t *encoder) {
    FSSTPlusCompressionResult compression_result{};
    compression_result.encoder = encoder;
    const size_t max_size = CalcMaxFSSTPlusDataSize(cleaved_result);
    compression_result.data_start = new uint8_t[max_size];

    FSSTPlusSizingResult sizing_result = SizeEverything(n, similarity_chunks, cleaved_result);

    uint8_t *global_header_ptr = compression_result.data_start;
    const size_t n_blocks = sizing_result.block_sizes_pfx_summed.size();
    Store<uint16_t>(static_cast<uint16_t>(n_blocks), global_header_ptr);
    global_header_ptr += sizeof(uint16_t);

    for (size_t i = 0; i < n_blocks; i++) {
        const size_t offsets_to_go = (n_blocks - i);
        const size_t global_header_size_ahead =
            offsets_to_go * sizeof(uint32_t) + sizeof(uint32_t);
        const size_t total_block_size_ahead =
            i == 0 ? 0 : sizing_result.block_sizes_pfx_summed[i - 1];
        Store<uint32_t>(static_cast<uint32_t>(global_header_size_ahead + total_block_size_ahead),
                        global_header_ptr);
        global_header_ptr += sizeof(uint32_t);
    }
    Store<uint32_t>(static_cast<uint32_t>(sizing_result.block_sizes_pfx_summed.back() + sizeof(uint32_t)),
                    global_header_ptr);
    global_header_ptr += sizeof(uint32_t);

    uint8_t *next_block_start_ptr = global_header_ptr;
    for (size_t i = 0; i < sizing_result.wms.size(); i++) {
        next_block_start_ptr = WriteBlock(next_block_start_ptr, cleaved_result, sizing_result.wms[i]);
    }
    compression_result.data_end = next_block_start_ptr;
    return compression_result;
}

// ---- Driver ----------------------------------------------------------------

struct CorpusBuffer {
    std::vector<std::string> strings;
    std::vector<size_t> lengths;
    std::vector<const unsigned char *> ptrs;
    size_t raw_bytes = 0;
};

static CorpusBuffer LoadCorpus(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("could not open input file: " + path);
    }
    CorpusBuffer c;
    std::string line;
    c.strings.reserve(1024);
    while (std::getline(in, line)) {
        c.strings.emplace_back(std::move(line));
        line.clear();
    }
    c.lengths.reserve(c.strings.size());
    c.ptrs.reserve(c.strings.size());
    for (const auto &s : c.strings) {
        c.lengths.push_back(s.size());
        c.ptrs.push_back(reinterpret_cast<const unsigned char *>(s.data()));
        c.raw_bytes += s.size();
    }
    return c;
}

static StringCollection ToStringCollection(const CorpusBuffer &c) {
    StringCollection sc(c.strings.size());
    for (const auto &s : c.strings) {
        sc.data.push_back(s);
        const std::string &held = sc.data.back();
        sc.lengths.push_back(held.size());
        sc.string_ptrs.push_back(reinterpret_cast<const unsigned char *>(held.data()));
    }
    return sc;
}

static double Mean(const std::vector<double> &v) {
    if (v.empty()) return 0.0;
    double sum = 0.0;
    for (const double x : v) sum += x;
    return sum / static_cast<double>(v.size());
}

static bool VerifyRoundTrip(uint8_t *data_start, const fsst_decoder_t &decoder,
                            const std::vector<size_t> &lengths_original,
                            const std::vector<const unsigned char *> &ptrs_original) {
    // Replicate DecompressAll's layout walk but compare each decoded string to the original.
    const uint16_t num_blocks = Load<uint16_t>(data_start);
    uint8_t *block_start_offsets = data_start + sizeof(uint16_t);
    size_t global_index = 0;
    std::vector<uint8_t> buf(1 << 20);
    for (int b = 0; b < num_blocks; ++b) {
        const uint8_t *block_start = FindBlockStart(block_start_offsets, b);
        const uint8_t *block_stop = FindBlockStart(block_start_offsets, b + 1);
        const size_t n_strings = Load<uint8_t>(block_start);
        const uint8_t *suffix_data_area_offsets_ptr = block_start + sizeof(uint8_t);
        for (size_t i = 0; i < n_strings; i++) {
            const uint8_t *suffix_offset_ptr =
                suffix_data_area_offsets_ptr + i * sizeof(uint16_t);
            const uint16_t suffix_offset = Load<uint16_t>(suffix_offset_ptr);
            const uint8_t *suffix_data_area_start =
                suffix_offset_ptr + sizeof(uint16_t) + suffix_offset;
            const uint8_t prefix_length = Load<uint8_t>(suffix_data_area_start);
            uint16_t suffix_data_area_length;
            if (i < n_strings - 1) {
                const uint16_t next_offset = Load<uint16_t>(suffix_offset_ptr + sizeof(uint16_t));
                suffix_data_area_length =
                    next_offset + sizeof(uint16_t) - suffix_offset;
            } else {
                suffix_data_area_length =
                    static_cast<uint16_t>(block_stop - suffix_data_area_start);
            }
            size_t produced;
            std::string original(reinterpret_cast<const char *>(ptrs_original[global_index]),
                                 lengths_original[global_index]);
            if (prefix_length == 0) {
                const uint8_t *encoded_suffix_ptr = suffix_data_area_start + sizeof(uint8_t);
                produced = fsst_decompress(&decoder,
                                           suffix_data_area_length - sizeof(uint8_t),
                                           encoded_suffix_ptr, buf.size(), buf.data());
            } else {
                const uint8_t *prefix_jumpback_offset_ptr =
                    suffix_data_area_start + sizeof(uint8_t);
                const uint16_t prefix_jumpback_offset =
                    Load<uint16_t>(prefix_jumpback_offset_ptr);
                const uint8_t *encoded_prefix_start =
                    prefix_jumpback_offset_ptr - prefix_jumpback_offset;
                size_t prefix_produced =
                    fsst_decompress(&decoder, prefix_length, encoded_prefix_start,
                                    buf.size(), buf.data());
                const uint8_t *encoded_suffix_ptr =
                    prefix_jumpback_offset_ptr + sizeof(uint16_t);
                size_t suffix_produced = fsst_decompress(
                    &decoder,
                    suffix_data_area_length - sizeof(uint8_t) - sizeof(uint16_t),
                    encoded_suffix_ptr, buf.size() - prefix_produced,
                    buf.data() + prefix_produced);
                produced = prefix_produced + suffix_produced;
            }
            if (produced != original.size() ||
                std::memcmp(buf.data(), original.data(), produced) != 0) {
                std::cerr << "round-trip mismatch at index " << global_index << "\n";
                return false;
            }
            ++global_index;
        }
    }
    return global_index == lengths_original.size();
}

int main(int argc, char **argv) {
    if (argc < 5) {
        std::cerr
            << "usage: " << argv[0]
            << " <input.txt> <dataset_name> <column_name> <variant_tag> [repetitions=5]\n";
        return 2;
    }
    const std::string input_path = argv[1];
    const std::string dataset_name = argv[2];
    const std::string column_name = argv[3];
    const std::string variant_tag = argv[4];
    const int repetitions = argc >= 6 ? std::atoi(argv[5]) : 5;
    if (repetitions <= 0) {
        std::cerr << "repetitions must be > 0\n";
        return 2;
    }

    CorpusBuffer corpus = LoadCorpus(input_path);
    if (corpus.strings.empty() || corpus.raw_bytes == 0) {
        std::cerr << "empty corpus, skipping\n";
        return 0;
    }
    if (corpus.strings.size() > UINT8_MAX * 65535ULL) {
        std::cerr << "corpus too large for FSST+ block layout\n";
        return 2;
    }

    std::vector<double> compress_ms;
    std::vector<double> decompress_ms;
    size_t compressed_bytes = 0;
    size_t n_strings = corpus.strings.size();

    for (int rep = 0; rep < repetitions; ++rep) {
        StringCollection input = ToStringCollection(corpus);

        auto t0 = std::chrono::high_resolution_clock::now();
        fsst_encoder_t *encoder = CreateEncoder(input.lengths, input.string_ptrs);
        FSSTCompressionResult input_compressed = FSSTCompress(input, encoder);
        const std::vector<SimilarityChunk> similarity_chunks =
            FormBlockwiseSimilarityChunksLocal(n_strings, input_compressed, input);
        const CleavedResult cleaved_result =
            Cleave(input_compressed.encoded_string_lengths,
                   input_compressed.encoded_string_ptrs, similarity_chunks, n_strings);
        FSSTPlusCompressionResult compression_result =
            FSSTPlusCompressLocal(n_strings, similarity_chunks, cleaved_result, encoder);
        auto t1 = std::chrono::high_resolution_clock::now();
        compress_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

        fsst_decoder_t decoder = fsst_decoder(encoder);

        // Verify only on the first rep to avoid skewing timing.
        if (rep == 0) {
            if (!VerifyRoundTrip(compression_result.data_start, decoder,
                                 input.lengths, input.string_ptrs)) {
                std::cerr << "round-trip verification failed for "
                          << dataset_name << "." << column_name << " ("
                          << variant_tag << ")\n";
                free(input_compressed.output_buffer);
                delete[] compression_result.data_start;
                fsst_destroy(encoder);
                return 1;
            }
        }

        auto t2 = std::chrono::high_resolution_clock::now();
        DecompressAllSilent(compression_result.data_start, decoder,
                            input.lengths, input.string_ptrs);
        auto t3 = std::chrono::high_resolution_clock::now();
        decompress_ms.push_back(std::chrono::duration<double, std::milli>(t3 - t2).count());

        if (rep == 0) {
            // Compute compressed size with the same accounting as RunFSSTPlus:
            // data payload + serialized symbol table - bitpacking savings on block offsets.
            size_t payload =
                static_cast<size_t>(compression_result.data_end - compression_result.data_start);
            payload += CalcSymbolTableSize(encoder);
            const uint16_t n_blocks = Load<uint16_t>(compression_result.data_start);
            const size_t size_of_one_offset =
                n_blocks > 1 ? static_cast<size_t>(std::ceil(std::log2(n_blocks) / 8.0))
                             : 1;
            const size_t bitpacked_offsets = n_blocks * size_of_one_offset;
            const size_t non_bitpacked_offsets = n_blocks * sizeof(uint32_t);
            const size_t savings =
                non_bitpacked_offsets > bitpacked_offsets
                    ? non_bitpacked_offsets - bitpacked_offsets
                    : 0;
            payload -= savings;
            compressed_bytes = payload;
        }

        free(input_compressed.output_buffer);
        delete[] compression_result.data_start;
        fsst_destroy(encoder);
    }

    const double comp_ms = Mean(compress_ms);
    const double decomp_ms = Mean(decompress_ms);
    const double cf = static_cast<double>(corpus.raw_bytes) / static_cast<double>(compressed_bytes);
    const double mb = static_cast<double>(corpus.raw_bytes) / (1024.0 * 1024.0);
    const double comp_mbps = comp_ms > 0 ? mb / (comp_ms / 1000.0) : 0.0;
    const double decomp_mbps = decomp_ms > 0 ? mb / (decomp_ms / 1000.0) : 0.0;

    std::printf("%s,%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                variant_tag.c_str(), dataset_name.c_str(), column_name.c_str(),
                n_strings, corpus.raw_bytes, compressed_bytes, cf,
                comp_ms, decomp_ms, comp_mbps, decomp_mbps);
    return 0;
}
