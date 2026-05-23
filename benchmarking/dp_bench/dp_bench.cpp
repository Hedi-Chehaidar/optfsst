// Compression-speed benchmark for OptFSST's buildDP trie traversal:
// compares the production variant (manual 8-deep unrolling + __builtin_expect
// hints) against a "naive" variant compiled with -DBUILDDP_NAIVE that uses a
// plain for-loop and no hints in SymbolTable::buildDP. Both variants share the
// rest of the OptFSST pipeline; only buildDP differs.
//
// One row per (variant, file) is written to stdout in CSV form:
//   variant,file,n_lines,raw_bytes,compressed_bytes,
//   create_ms_mean,compress_ms_mean,total_ms_mean,
//   create_mb_per_s_mean,compress_mb_per_s_mean,total_mb_per_s_mean
//
// Usage: dp_bench <input_file> <variant_tag> [repetitions=5]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fsst.h"

namespace {

constexpr unsigned OPTFSST_FLAGS = FSST_OPT_DP_TRAIN | FSST_OPT_TRIPLES |
                                   FSST_OPT_PRUNE | FSST_OPT_DP_ENCODE;

struct Corpus {
    std::vector<std::string> lines;
    std::vector<size_t> lengths;
    std::vector<const unsigned char *> ptrs;
    size_t raw_bytes = 0;
};

Corpus LoadCorpus(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("could not open input file: " + path);
    }
    Corpus c;
    std::string line;
    while (std::getline(in, line)) {
        c.lines.emplace_back(std::move(line));
        line.clear();
    }
    c.lengths.reserve(c.lines.size());
    c.ptrs.reserve(c.lines.size());
    for (const auto &s : c.lines) {
        c.lengths.push_back(s.size());
        c.ptrs.push_back(reinterpret_cast<const unsigned char *>(s.data()));
        c.raw_bytes += s.size();
    }
    return c;
}

double Mean(const std::vector<double> &v) {
    if (v.empty()) return 0.0;
    double sum = 0.0;
    for (double x : v) sum += x;
    return sum / static_cast<double>(v.size());
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0]
                  << " <input_file> <variant_tag> [repetitions=5]\n";
        return 2;
    }
    const std::string input_path = argv[1];
    const std::string variant_tag = argv[2];
    const int repetitions = argc >= 4 ? std::atoi(argv[3]) : 5;
    if (repetitions <= 0) {
        std::cerr << "repetitions must be > 0\n";
        return 2;
    }

    Corpus corpus = LoadCorpus(input_path);
    if (corpus.lines.empty() || corpus.raw_bytes == 0) {
        std::cerr << "empty corpus, skipping\n";
        return 0;
    }

    std::vector<unsigned char> compress_buf(16 + 2 * corpus.raw_bytes);
    std::vector<size_t> out_lens(corpus.lines.size());
    std::vector<unsigned char *> out_ptrs(corpus.lines.size() + 1);

    std::vector<double> create_ms;
    std::vector<double> compress_ms;
    size_t compressed_bytes = 0;

    fsst_options_t opt{};
    opt.flags = OPTFSST_FLAGS;

    for (int rep = 0; rep < repetitions; ++rep) {
        auto t0 = std::chrono::high_resolution_clock::now();
        fsst_encoder_t *encoder = Optfsst_create(
            corpus.lines.size(), corpus.lengths.data(), corpus.ptrs.data(),
            /*zeroTerminated=*/0, &opt);
        auto t1 = std::chrono::high_resolution_clock::now();

        const size_t produced = Optfsst_compress(
            encoder, corpus.lines.size(), corpus.lengths.data(),
            corpus.ptrs.data(), compress_buf.size(), compress_buf.data(),
            out_lens.data(), out_ptrs.data(), &opt);
        auto t2 = std::chrono::high_resolution_clock::now();

        if (produced != corpus.lines.size()) {
            std::cerr << "Optfsst_compress did not consume all lines ("
                      << produced << " / " << corpus.lines.size() << ")\n";
            fsst_destroy(encoder);
            return 1;
        }

        if (rep == 0) {
            size_t total = 0;
            for (size_t i = 0; i < corpus.lines.size(); ++i) total += out_lens[i];
            // Add serialised symbol table to mirror line_bench's accounting.
            unsigned char dict_buf[sizeof(fsst_decoder_t)];
            total += fsst_export(encoder, dict_buf);
            compressed_bytes = total;
        }

        fsst_destroy(encoder);

        create_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        compress_ms.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
    }

    const double create_mean = Mean(create_ms);
    const double compress_mean = Mean(compress_ms);
    const double total_mean = create_mean + compress_mean;
    const double mb = static_cast<double>(corpus.raw_bytes) / (1024.0 * 1024.0);
    const double create_mbps = create_mean > 0 ? mb / (create_mean / 1000.0) : 0.0;
    const double compress_mbps = compress_mean > 0 ? mb / (compress_mean / 1000.0) : 0.0;
    const double total_mbps = total_mean > 0 ? mb / (total_mean / 1000.0) : 0.0;

    std::printf("%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                variant_tag.c_str(), input_path.c_str(),
                corpus.lines.size(), corpus.raw_bytes, compressed_bytes,
                create_mean, compress_mean, total_mean,
                create_mbps, compress_mbps, total_mbps);
    return 0;
}
