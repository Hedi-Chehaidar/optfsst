#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <lz4.h>

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
    if (input.size() > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) {
        std::cerr << "input too large for lz4: " << in_path << "\n";
        return 1;
    }
    const int max_compressed = LZ4_compressBound(static_cast<int>(input.size()));
    std::vector<char> buf(8 + static_cast<std::size_t>(max_compressed));
    const std::uint64_t orig = static_cast<std::uint64_t>(input.size());
    std::memcpy(buf.data(), &orig, sizeof(orig));

    const auto t0 = std::chrono::steady_clock::now();
    const int compressed_size = LZ4_compress_default(
        input.data(), buf.data() + 8,
        static_cast<int>(input.size()), max_compressed);
    const auto t1 = std::chrono::steady_clock::now();
    if (compressed_size <= 0) {
        std::cerr << "LZ4_compress_default failed\n";
        return 1;
    }

    write_file(out_path, buf.data(), 8 + static_cast<std::size_t>(compressed_size));

    if (print_time) {
        const double seconds = std::chrono::duration<double>(t1 - t0).count();
        std::cout << seconds << "\n";
    }
    return 0;
}

static int decompress_file(const std::string& in_path, const std::string& out_path) {
    const auto input = read_file(in_path);
    if (input.size() < 8) {
        std::cerr << "lz4 input too small\n";
        return 1;
    }
    std::uint64_t orig = 0;
    std::memcpy(&orig, input.data(), sizeof(orig));
    std::vector<char> output(static_cast<std::size_t>(orig));

    const auto t0 = std::chrono::steady_clock::now();
    const int decompressed = LZ4_decompress_safe(
        input.data() + 8, output.data(),
        static_cast<int>(input.size() - 8),
        static_cast<int>(orig));
    const auto t1 = std::chrono::steady_clock::now();
    if (decompressed < 0 || static_cast<std::uint64_t>(decompressed) != orig) {
        std::cerr << "LZ4_decompress_safe failed\n";
        return 1;
    }

    write_file(out_path, output.data(), static_cast<std::size_t>(orig));
    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    std::cout << seconds << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: lz4_bench [--time-compression|-d] <input> <output>\n";
        return 1;
    }
    const std::string first = argv[1];
    if (first == "--time-compression") {
        if (argc != 4) {
            std::cerr << "usage: lz4_bench --time-compression <input> <output>\n";
            return 1;
        }
        return compress_file(argv[2], argv[3], true);
    }
    if (first == "-d") {
        if (argc != 4) {
            std::cerr << "usage: lz4_bench -d <input> <output>\n";
            return 1;
        }
        return decompress_file(argv[2], argv[3]);
    }
    if (argc != 3) {
        std::cerr << "usage: lz4_bench <input> <output>\n";
        return 1;
    }
    return compress_file(argv[1], argv[2], false);
}
