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
    std::string name;     // configuration name that goes into the CSV
    std::string args;     // arguments passed to the binary for this config
};

struct BinaryConfig {
    std::string label;    // build label, e.g. w/ AVX or w/o AVX
    std::string path;     // path to executable
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

// Run command, capture stdout, measure wall time ms.
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
    try {
        std::istringstream iss(trimmed);
        std::string token;
        std::string last_token;
        while (iss >> token) last_token = token;
        if (last_token.empty()) {
            std::cerr << "Command produced non-numeric stdout\n";
            std::cerr << "Command: " << cmd << "\n";
            std::cerr << "Captured stdout: " << trimmed << "\n";
            throw std::runtime_error("benchmark command produced non-numeric stdout");
        }
        size_t parsed = 0;
        double value = std::stod(last_token, &parsed);
        if (parsed != last_token.size()) {
            std::cerr << "Command produced non-numeric stdout\n";
            std::cerr << "Command: " << cmd << "\n";
            std::cerr << "Captured stdout: " << trimmed << "\n";
            throw std::runtime_error("benchmark command produced non-numeric stdout");
        }
        return value;
    } catch (const std::exception&) {
        std::cerr << "Failed to parse stdout as double\n";
        std::cerr << "Command: " << cmd << "\n";
        std::cerr << "Captured stdout: " << trimmed << "\n";
        throw;
    }
}

int main() {

    const std::vector<BinaryConfig> binaries = {
        {"w/o AVX", "../build-noavx512/fsst"},
        {"w/ AVX", "../build-avx512/fsst"},
    };

    // Input files (one file per run).
    std::vector<std::string> files;
    
    std::string path = "../fsst-paper/dbtext";
    
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if(entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    // Configurations: name + args passed to the binary.
    const std::vector<Config> configs = {
        {"FSST", ""},
        {"BtrFSST", "--dp-train --prune --triples --dp-encode"},
    };

    std::unordered_map<std::string, std::uintmax_t> btrfsst_reference_size;
    std::unordered_map<std::string, double>         avg_cfs;

    // Output CSV path


    std::ofstream out1("./csv/compression_speed_dbtext.csv");
    out1 << "configuration,Time,file\n";
    std::ofstream out2("./csv/decompression_speed_dbtext.csv");
    out2 << "configuration,Time,file\n";
    
    for (const auto& binary : binaries) {
        for (const auto& file : files) {
            int pos = 0;
            for (const auto& cfg : configs) {
                const std::string config_name = cfg.name + " (" + binary.label + ")";
                // compression
                double comp_time = 0;

                std::ostringstream cmd;
                cmd << binary.path;
                cmd << " " << cfg.args;
                cmd << " " << file;
                //output file 
                cmd << " ./tmp_out_" << pos;
            
                for(int i = 0; i < 5; i++) {
                    int exit_code = 0;
                    std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code);
                    comp_time += parse_timing_output(cmd.str(), exit_code, stdout_text);
                }
                comp_time /= 5;

                double comp_speed = fs::file_size(file) / 1000000.0 / comp_time;

                out1 << config_name << ","
                    << comp_speed << ","
                    << file << "\n";

                std::uintmax_t compressed_size = get_checked_file_size("./tmp_out_" + std::to_string(pos));
                if (cfg.name == "BtrFSST") {
                    auto it = btrfsst_reference_size.find(file);
                    if (it == btrfsst_reference_size.end()) {
                        btrfsst_reference_size[file] = compressed_size;
                    } else if (it->second != compressed_size) {
                        std::cerr << "BtrFSST compressed size mismatch for file " << file << "\n"
                                << "expected " << it->second << ", got " << compressed_size
                                << " for binary " << binary.label << "\n";
                        throw std::runtime_error("BtrFSST compression factor mismatch");
                    }
                }

                avg_cfs[config_name] += 1.0 * get_checked_file_size(file) / compressed_size;

                // decompression
                double decomp_time = 0;
                std::ostringstream cmd2;
                cmd2 << binary.path << " -d ./tmp_out_" << pos << " ./tmp_out2_" << pos;

                for(int i = 0; i < 5; i++) {
                    int exit_code = 0;
                    std::string stdout_text = run_and_capture_stdout(cmd2.str(), exit_code);
                    decomp_time += parse_timing_output(cmd2.str(), exit_code, stdout_text);
                }

                decomp_time /= 5;
                double decomp_speed = fs::file_size(file) / 1000000.0 / decomp_time;
                
                out2 << config_name << ","
                    << decomp_speed << ","
                    << file << "\n";

                if (!files_equal(file, "./tmp_out2_" + std::to_string(pos))) {
                    std::cerr << "Mismatch after decompression for file " << file
                            << ", config " << cfg.name
                            << ", binary " << binary.label << "\n";
                    throw std::runtime_error("decompression correctness failed");
                }

                pos++;
            }

        }
        
    }
    
    std::cout << "runner finished" << std::endl;

    // output avg CFs 
    std::cout << "Average compression factors:" << std::endl;
    for(auto& p : avg_cfs) {
        std::cout << p.first << " " << p.second / files.size() << std::endl;
    }
    return 0;
}
