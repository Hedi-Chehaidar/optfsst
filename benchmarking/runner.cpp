#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
  #define popen _popen
  #define pclose _pclose
#else
  #include <sys/wait.h>
#endif

namespace fs = std::filesystem;

struct Config {
    std::string label;
    std::string args;
};

struct SpeedVariant {
    std::string label;
    std::string binary;
    std::string args;
};

static std::string trim(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

static std::string run_and_capture_stdout(const std::string& cmd, int& exit_code) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "";
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    int rc = pclose(pipe);
#if defined(_WIN32)
    exit_code = rc;
#else
    if (WIFEXITED(rc)) exit_code = WEXITSTATUS(rc);
    else exit_code = rc;
#endif
    return output;
}

static double parse_timing_output(const std::string& cmd, int exit_code, const std::string& stdout_text) {
    const std::string trimmed = trim(stdout_text);
    if (exit_code != 0) {
        std::cerr << "Command failed with exit code " << exit_code << "\n";
        std::cerr << "Command: " << cmd << "\n";
        std::cerr << "Captured stdout: " << (trimmed.empty() ? "<empty>" : trimmed) << "\n";
        throw std::runtime_error("benchmark command failed");
    }
    if (trimmed.empty()) {
        std::cerr << "Command produced empty stdout\n";
        std::cerr << "Command: " << cmd << "\n";
        throw std::runtime_error("benchmark command produced empty stdout");
    }

    std::istringstream iss(trimmed);
    std::string token;
    std::string last_token;
    while (iss >> token) last_token = token;
    if (last_token.empty()) {
        throw std::runtime_error("benchmark command produced non-numeric stdout");
    }

    size_t parsed = 0;
    const double value = std::stod(last_token, &parsed);
    if (parsed != last_token.size()) {
        std::cerr << "Command: " << cmd << "\n";
        std::cerr << "Captured stdout: " << trimmed << "\n";
        throw std::runtime_error("benchmark command produced non-numeric stdout");
    }
    return value;
}

static bool files_equal(const fs::path& a, const fs::path& b) {
    if (!fs::exists(a) || !fs::exists(b)) return false;
    if (fs::file_size(a) != fs::file_size(b)) return false;

    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    std::array<char, 1 << 20> ba{};
    std::array<char, 1 << 20> bb{};

    while (fa && fb) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        const std::streamsize na = fa.gcount();
        const std::streamsize nb = fb.gcount();
        if (na != nb) return false;
        if (!std::equal(ba.begin(), ba.begin() + na, bb.begin())) return false;
    }
    return true;
}

static std::uintmax_t get_checked_file_size(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::uintmax_t>(in.tellg());
    if (size == static_cast<std::uintmax_t>(-1)) {
        throw std::runtime_error("failed to determine file size: " + path.string());
    }
    return size;
}

static std::vector<fs::path> collect_files(const fs::path& root) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

static void assert_exists(const fs::path& path) {
    if (!fs::exists(path)) {
        throw std::runtime_error("missing required file: " + path.string());
    }
}

static double run_compress_cf(const fs::path& binary,
                              const std::string& args,
                              const fs::path& input,
                              const fs::path& output) {
    std::ostringstream cmd;
    cmd << shell_quote(binary.string());
    if (!args.empty()) cmd << " " << args;
    cmd << " " << shell_quote(input.string()) << " " << shell_quote(output.string());

    int exit_code = 0;
    const std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code);
    parse_timing_output(cmd.str(), exit_code, stdout_text);
    return static_cast<double>(get_checked_file_size(input)) / static_cast<double>(get_checked_file_size(output));
}

static void run_improvement_benchmark(const std::string& title,
                                      const fs::path& binary,
                                      const std::vector<Config>& configs,
                                      const std::vector<fs::path>& files,
                                      const fs::path& csv_path,
                                      const fs::path& tmp_root) {
    std::ofstream out(csv_path);
    out << "configuration,CF,file\n";

    std::map<std::string, int> best_config_counts;

    for (const auto& file : files) {
        double baseline_cf = 0.0;
        double best_cf = 0.0;
        std::string best_label;

        for (size_t idx = 0; idx < configs.size(); ++idx) {
            const auto& cfg = configs[idx];
            const fs::path tmp_out = tmp_root / (title + "_cf_" + std::to_string(idx) + ".bin");
            const double cf_value = run_compress_cf(binary, cfg.args, file, tmp_out);

            if (idx == 0) {
                baseline_cf = cf_value;
            } else {
                out << cfg.label << "," << (cf_value / baseline_cf) << "," << file.string() << "\n";
            }

            if (cf_value >= best_cf) {
                best_cf = cf_value;
                best_label = cfg.label;
            }
        }

        best_config_counts[best_label]++;
    }

    std::cout << title << " best configurations:\n";
    for (const auto& [label, count] : best_config_counts) {
        std::cout << "  " << label << ": " << count << "\n";
    }
}

static void run_speed_benchmark(const std::vector<SpeedVariant>& variants,
                                const std::vector<fs::path>& files,
                                const fs::path& table_construction_csv,
                                const fs::path& compression_csv,
                                const fs::path& decompression_csv,
                                const fs::path& tmp_root) {
    std::ofstream table_construction_out(table_construction_csv);
    table_construction_out << "configuration,Time,file\n";

    std::ofstream compression_out(compression_csv);
    compression_out << "configuration,Time,file\n";

    std::ofstream decompression_out(decompression_csv);
    decompression_out << "configuration,Time,file\n";

    for (const auto& variant : variants) {
        for (const auto& file : files) {
            const fs::path compressed = tmp_root / (variant.label + "_compressed.bin");
            const fs::path decompressed = tmp_root / (variant.label + "_decompressed.bin");

            if (variant.label != "FSST (SIMD)") {
                double table_construction_time = 0.0;
                std::ostringstream table_construction_cmd;
                table_construction_cmd << shell_quote(variant.binary);
                if (!variant.args.empty()) table_construction_cmd << " " << variant.args;
                table_construction_cmd << " --time-table-construction "
                                       << shell_quote(file.string()) << " "
                                       << shell_quote(compressed.string());

                for (int rep = 0; rep < 5; ++rep) {
                    int exit_code = 0;
                    const std::string stdout_text = run_and_capture_stdout(table_construction_cmd.str(), exit_code);
                    table_construction_time += parse_timing_output(table_construction_cmd.str(), exit_code, stdout_text);
                }
                table_construction_time /= 5.0;

                table_construction_out << variant.label << ","
                                       << (static_cast<double>(get_checked_file_size(file)) / 1000000.0 / table_construction_time) << ","
                                       << file.string() << "\n";
            }

            double compression_time = 0.0;
            std::ostringstream compress_cmd;
            compress_cmd << shell_quote(variant.binary);
            if (!variant.args.empty()) compress_cmd << " " << variant.args;
            compress_cmd << " --time-compression "
                         << shell_quote(file.string()) << " "
                         << shell_quote(compressed.string());

            for (int rep = 0; rep < 5; ++rep) {
                int exit_code = 0;
                const std::string stdout_text = run_and_capture_stdout(compress_cmd.str(), exit_code);
                compression_time += parse_timing_output(compress_cmd.str(), exit_code, stdout_text);
            }
            compression_time /= 5.0;

            compression_out << variant.label << ","
                            << (static_cast<double>(get_checked_file_size(file)) / 1000000.0 / compression_time) << ","
                            << file.string() << "\n";

            double decompression_time = 0.0;
            std::ostringstream decompress_cmd;
            decompress_cmd << shell_quote(variant.binary)
                           << " -d "
                           << shell_quote(compressed.string()) << " "
                           << shell_quote(decompressed.string());

            for (int rep = 0; rep < 5; ++rep) {
                int exit_code = 0;
                const std::string stdout_text = run_and_capture_stdout(decompress_cmd.str(), exit_code);
                decompression_time += parse_timing_output(decompress_cmd.str(), exit_code, stdout_text);
            }
            decompression_time /= 5.0;

            decompression_out << variant.label << ","
                              << (static_cast<double>(get_checked_file_size(file)) / 1000000.0 / decompression_time) << ","
                              << file.string() << "\n";

            if (!files_equal(file, decompressed)) {
                throw std::runtime_error("decompression correctness failed for " + variant.label + " on " + file.string());
            }
        }
    }
}

int main() {
    const fs::path corpus_root = "../data/refined"; //"../fsst-paper/dbtext"; 
    const fs::path csv_dir = "./csv";
    const fs::path tmp_dir = "./tmp";

    fs::create_directories(csv_dir);
    fs::create_directories(tmp_dir);

    const std::vector<fs::path> files = collect_files(corpus_root);
    if (files.empty()) {
        throw std::runtime_error("no benchmark files found under " + corpus_root.string());
    }

    const fs::path fsst_scalar = "../build-noavx512/fsst";
    const fs::path fsst_simd = "../build-avx512/fsst";
    const fs::path fsst12 = "../build12/fsst12";

    assert_exists(fsst_scalar);
    assert_exists(fsst_simd);
    assert_exists(fsst12);

    const std::vector<Config> improvement_configs_8bit = {
        {"FSST + dp-train", "--dp-train"},
        {"+ triples", "--dp-train --triples"},
        {"+ prune", "--dp-train --triples --prune"},
        {"FSST + dp-encode", "--dp-encode"},
        {"+ dp-train", "--dp-train --dp-encode"},
        {"+ triples", "--dp-train --triples --dp-encode"},
        {"+ prune = OptFSST", "--dp-train --triples --prune --dp-encode"},
    };

    const std::vector<Config> improvement_configs_12bit = {
        {"FSST12 + dp-train", "--dp-train"},
        {"+ triples", "--dp-train --triples"},
        {"+ prune", "--dp-train --triples --prune"},
        {"FSST12 + dp-encode", "--dp-encode"},
        {"+ dp-train", "--dp-train --dp-encode"},
        {"+ triples", "--dp-train --triples --dp-encode"},
        {"+ prune = OptFSST12", "--dp-train --triples --prune --dp-encode"},
    };

    auto prepend_baseline = [](std::vector<Config> configs, const std::string& label) {
        configs.insert(configs.begin(), Config{label, ""});
        return configs;
    };

    run_improvement_benchmark(
        "optfsst",
        fsst_scalar,
        prepend_baseline(improvement_configs_8bit, "FSST"),
        files,
        csv_dir / "improvement.csv",
        tmp_dir);

    run_improvement_benchmark(
        "optfsst12",
        fsst12,
        prepend_baseline(improvement_configs_12bit, "FSST12"),
        files,
        csv_dir / "improvement12.csv",
        tmp_dir);

    const std::vector<SpeedVariant> speed_variants = {
        {"FSST", fsst_scalar.string(), ""},
        {"FSST (SIMD)", fsst_simd.string(), ""},
        {"OptFSST", fsst_scalar.string(), "--dp-train --triples --prune --dp-encode"},
        {"FSST12", fsst12.string(), ""},
        {"OptFSST12", fsst12.string(), "--dp-train --triples --prune --dp-encode"},
    };

    run_speed_benchmark(
        speed_variants,
        files,
        csv_dir / "table_construction_speed_paper.csv",
        csv_dir / "compression_speed_paper.csv",
        csv_dir / "decompression_speed_paper.csv",
        tmp_dir);

    fs::remove_all(tmp_dir);

    std::cout << "benchmark runner finished\n";
    return 0;
}
