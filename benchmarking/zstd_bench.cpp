#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <zstd.h>

static std::vector<char> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::cerr << "cannot open input: " << path << "\n";
        std::exit(1);
    }
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<char> buf(size);
    if (size > 0) {
        in.read(buf.data(), static_cast<std::streamsize>(size));
        if (!in) {
            std::cerr << "failed to read input: " << path << "\n";
            std::exit(1);
        }
    }
    return buf;
}

static void write_file(const std::string& path, const char* data, std::size_t size) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "cannot open output: " << path << "\n";
        std::exit(1);
    }
    if (size > 0) out.write(data, static_cast<std::streamsize>(size));
    if (!out) {
        std::cerr << "failed to write output: " << path << "\n";
        std::exit(1);
    }
}

static int compress_file(const std::string& in_path, const std::string& out_path, bool print_time) {
    const auto input = read_file(in_path);
    const std::size_t bound = ZSTD_compressBound(input.size());
    std::vector<char> buf(bound);

    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t compressed_size = ZSTD_compress(
        buf.data(), buf.size(),
        input.data(), input.size(),
        ZSTD_CLEVEL_DEFAULT);
    const auto t1 = std::chrono::steady_clock::now();
    if (ZSTD_isError(compressed_size)) {
        std::cerr << "ZSTD_compress failed: " << ZSTD_getErrorName(compressed_size) << "\n";
        return 1;
    }

    write_file(out_path, buf.data(), compressed_size);

    if (print_time) {
        const double seconds = std::chrono::duration<double>(t1 - t0).count();
        std::cout << seconds << "\n";
    }
    return 0;
}

static int decompress_file(const std::string& in_path, const std::string& out_path) {
    const auto input = read_file(in_path);
    const unsigned long long orig = ZSTD_getFrameContentSize(input.data(), input.size());
    if (orig == ZSTD_CONTENTSIZE_ERROR || orig == ZSTD_CONTENTSIZE_UNKNOWN) {
        std::cerr << "zstd: cannot determine decompressed size\n";
        return 1;
    }
    std::vector<char> output(static_cast<std::size_t>(orig));

    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t decompressed = ZSTD_decompress(
        output.data(), output.size(),
        input.data(), input.size());
    const auto t1 = std::chrono::steady_clock::now();
    if (ZSTD_isError(decompressed) || decompressed != orig) {
        std::cerr << "ZSTD_decompress failed: "
                  << (ZSTD_isError(decompressed) ? ZSTD_getErrorName(decompressed) : "size mismatch")
                  << "\n";
        return 1;
    }

    write_file(out_path, output.data(), decompressed);
    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    std::cout << seconds << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: zstd_bench [--time-compression|-d] <input> <output>\n";
        return 1;
    }
    const std::string first = argv[1];
    if (first == "--time-compression") {
        if (argc != 4) {
            std::cerr << "usage: zstd_bench --time-compression <input> <output>\n";
            return 1;
        }
        return compress_file(argv[2], argv[3], true);
    }
    if (first == "-d") {
        if (argc != 4) {
            std::cerr << "usage: zstd_bench -d <input> <output>\n";
            return 1;
        }
        return decompress_file(argv[2], argv[3]);
    }
    if (argc != 3) {
        std::cerr << "usage: zstd_bench <input> <output>\n";
        return 1;
    }
    return compress_file(argv[1], argv[2], false);
}
