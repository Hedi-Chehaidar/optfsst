#include <bits/stdc++.h>

#if defined(_WIN32)
  #include <windows.h>
  #define popen _popen
  #define pclose _pclose
#else
  #include <sys/wait.h>
#endif

namespace fs = std::filesystem;

struct Config {
    std::string name;
    std::string args;
};

static std::string trim(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static bool files_equal(const fs::path& a, const fs::path& b) {
    if (!fs::exists(a) || !fs::exists(b)) return false;
    if (fs::file_size(a) != fs::file_size(b)) return false;

    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    std::array<char, 1 << 20> ba, bb;

    while (fa && fb) {
        fa.read(ba.data(), ba.size());
        fb.read(bb.data(), bb.size());
        std::streamsize na = fa.gcount();
        std::streamsize nb = fb.gcount();
        if (na != nb) return false;
        if (!std::equal(ba.begin(), ba.begin() + na, bb.begin())) return false;
    }
    return true;
}

static std::uintmax_t get_checked_file_size(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        throw std::runtime_error("cannot open output file: " + p.string());
    }
    f.seekg(0, std::ios::end);
    auto size = static_cast<std::uintmax_t>(f.tellg());
    if (size == static_cast<std::uintmax_t>(-1)) {
        throw std::runtime_error("failed to determine file size: " + p.string());
    }
    return size;
}

static std::string run_and_capture_stdout(const std::string& cmd, int& exit_code) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "";
    }
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
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
    std::string trimmed = trim(stdout_text);
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
    double value = std::stod(last_token, &parsed);
    if (parsed != last_token.size()) {
        throw std::runtime_error("benchmark command produced non-numeric stdout");
    }
    return value;
}

int main() {
    std::string binary;
    if (fs::exists("../build12/fsst12")) {
        binary = "../build12/fsst12";
    } else if (fs::exists("../build12/binary12")) {
        binary = "../build12/binary12";
    } else {
        throw std::runtime_error("could not find 12-bit executable in ../build12 (expected fsst12 or binary12)");
    }

    std::vector<std::string> files;
    const std::string path = "../fsst-paper/dbtext";
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }

    const std::vector<Config> configs = {
        {"FSST12", ""},
        {"BtrFSST12", "--dp-train --prune --triples --dp-encode"},
    };

    std::unordered_map<std::string, double> avg_cfs;

    std::ofstream out1("./csv/compression_speed_dbtext12.csv");
    out1 << "configuration,Time,file\n";
    std::ofstream out2("./csv/decompression_speed_dbtext12.csv");
    out2 << "configuration,Time,file\n";

    for (const auto& file : files) {
        int pos = 0;
        for (const auto& cfg : configs) {
            double comp_time = 0.0;

            std::ostringstream cmd;
            cmd << binary;
            if (!cfg.args.empty()) cmd << " " << cfg.args;
            cmd << " " << file;
            cmd << " ./tmp12_out_" << pos;

            for (int i = 0; i < 5; i++) {
                int exit_code = 0;
                std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code);
                comp_time += parse_timing_output(cmd.str(), exit_code, stdout_text);
            }
            comp_time /= 5.0;

            double comp_speed = fs::file_size(file) / 1000000.0 / comp_time;
            out1 << cfg.name << "," << comp_speed << "," << file << "\n";

            std::uintmax_t compressed_size = get_checked_file_size("./tmp12_out_" + std::to_string(pos));
            avg_cfs[cfg.name] += 1.0 * get_checked_file_size(file) / compressed_size;

            double decomp_time = 0.0;
            std::ostringstream cmd2;
            cmd2 << binary << " -d ./tmp12_out_" << pos << " ./tmp12_out2_" << pos;

            for (int i = 0; i < 5; i++) {
                int exit_code = 0;
                std::string stdout_text = run_and_capture_stdout(cmd2.str(), exit_code);
                decomp_time += parse_timing_output(cmd2.str(), exit_code, stdout_text);
            }
            decomp_time /= 5.0;

            double decomp_speed = fs::file_size(file) / 1000000.0 / decomp_time;
            out2 << cfg.name << "," << decomp_speed << "," << file << "\n";

            if (!files_equal(file, "./tmp12_out2_" + std::to_string(pos))) {
                std::cerr << "Mismatch after decompression for file " << file
                          << ", config " << cfg.name << "\n";
                throw std::runtime_error("decompression correctness failed");
            }

            pos++;
        }
    }

    std::cout << "runner12 finished" << std::endl;
    std::cout << "Average compression factors:" << std::endl;
    for (auto& p : avg_cfs) {
        std::cout << p.first << " " << p.second / files.size() << std::endl;
    }
    return 0;
}
