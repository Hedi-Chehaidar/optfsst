rm -rf build
mkdir build
cd build
cmake ..
make -j
cd ../benchmarking
g++ runner.cpp -o runner
./runner
python3 plot_results.py compression_speed_dbtext
python3 plot_results.py decompression_speed_dbtext