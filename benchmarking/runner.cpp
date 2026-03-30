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
    assert(rc == 0);
    return output;
}

int main() {

    // One or more binaries to benchmark (paths to executables).
    const std::vector<std::string> binaries = {
        "../build/fsst"
        //""
    };

    // Input files (one file per run).
    std::vector<std::string> files;
    
    std::string path = "../data/refined";
    
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


    std::ofstream out1("./csv/compression_speed.csv");
    out1 << "configuration,Time,file\n";
    std::ofstream out2("./csv/decompression_speed.csv");
    out2 << "configuration,Time,file\n";
    for (const auto& bin : binaries) { // only one binary for now
        for (const auto& file : files) {
            for (const auto& cfg : configs) {
                // compression
                double comp_time = 0;

                std::ostringstream cmd;
                cmd << bin;
                cmd << " " << cfg.args;
                cmd << " " << file;
                //output file 
                cmd << " ../build/out";
            
                for(int i = 0; i < 5; i++) {
                    int exit_code = 0;
                    std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code);
                    comp_time += std::stod(stdout_text);
                }
                comp_time /= 5;

                double comp_speed = fs::file_size(file) / 1000000.0 / comp_time;

                out1 << cfg.name << ","
                    << comp_speed << ","
                    << file << "\n";

                // decompression
                double decomp_time = 0;
                std::ostringstream cmd2;
                cmd2 << bin << " -d ../build/out ../build/out2";

                for(int i = 0; i < 5; i++) {
                    int exit_code = 0;
                    std::string stdout_text = run_and_capture_stdout(cmd2.str(), exit_code);
                    decomp_time += std::stod(stdout_text);
                }

                decomp_time /= 5;
                double decomp_speed = fs::file_size(file) / 1000000.0 / decomp_time;
                
                out2 << cfg.name << ","
                    << decomp_speed << ","
                    << file << "\n";

                
                
                
            }

        }
        
    }
    
    std::cout << "runner finished" << std::endl;
    return 0;
}
