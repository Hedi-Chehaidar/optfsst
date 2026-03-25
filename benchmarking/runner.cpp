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
        {"BtrFSST", "--dp-train --dp-encode"},
    };

    // Output CSV path


    std::ofstream out("./csv/counters.csv");
    out << "counters,Time,CF,file\n";

    for (const auto& bin : binaries) { // only one binary for now
        for (const auto& file : files) {

            for (const auto& cfg : configs) {
                for(int nbCounters = 2; nbCounters <= 5; nbCounters++) {
                    double time = 0;
                    std::ostringstream cmd;
                    cmd << bin;
                    cmd << " " << cfg.args;
                    if(nbCounters > 2) cmd << " --triples";
                    cmd << " --counters " << nbCounters;
                    cmd << " " << file;
                    //output file 
                    cmd << " ../build/out";

                    for(int i = 0; i < 5; i++) {
                        int exit_code = 0;
                        std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code);
                        time += std::stod(stdout_text);
                    }

                    double cf_value = 1.0 * fs::file_size(file) / fs::file_size("../build/out");
                    out << nbCounters << ","
                        << time / 5 << ","
                        << cf_value << ","
                        << file << "\n";
    
                }
                
            }


        }
        
    }
    
    std::cout << "runner finished" << std::endl;
    return 0;
}
