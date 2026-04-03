#include <bits/stdc++.h>

#if defined(_WIN32)
  #include <windows.h>
  #define popen _popen
  #define pclose _pclose
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

    // Output CSV path


    std::ofstream out1("./csv/compression_speed_dbtext.csv");
    out1 << "configuration,Time,file\n";
    std::ofstream out2("./csv/decompression_speed_dbtext.csv");
    out2 << "configuration,Time,file\n";
    double improvement = 0, mx = 0;
    for (const auto& binary : binaries) {
        for (const auto& file : files) {
            int pos = 0;
            double cfs[2] = {0,0};
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
                //cfs[pos++] = comp_time;
                comp_time /= 5;

                double comp_speed = fs::file_size(file) / 1000000.0 / comp_time;

                out1 << config_name << ","
                    << comp_speed << ","
                    << file << "\n";

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

                pos++;
            }

            //improvement += cfs[1] / cfs[0];
            //mx = std::max(mx, cfs[1] / cfs[0]);
        }
        
    }
    
    std::cout << "runner finished" << std::endl;
    //std::cout << improvement / files.size() << " " << mx << std::endl;
    return 0;
}
