# BtrFSST
BtrFSST is an extension of [FSST](https://github.com/cwida/fsst), including the contributions of the Bachelor's thesis by Hedi Chehaidar

Contributions:
- DP compression function
- Third frequency counter
- Pruning conflicting symbols

BtrFSST has higher compression factors than FSST by up to 47.7%. Average improvement was at 7.3%.

![CF improvement plot](https://github.com/Hedi-Chehaidar/btrfsst/blob/master/btrfsst-CF-improvement.png)

You can build the project and compile it the same way like in the FSST repository. Use command line options when running the `fsst` or `fsst12` binary to include the contributions (no options = FSST/FSST12, all options = BtrFSST/BtrFSST12).

For the paper benchmarks, run `./run.sh`. It builds the scalar and AVX-512 8-bit binaries plus the 12-bit binary, then generates:

- compression-factor improvement CSVs and plots for BtrFSST and BtrFSST12
- compression-speed and decompression-speed CSVs and plots comparing `FSST`, `FSST (SIMD)`, `BtrFSST`, `FSST12`, and `BtrFSST12`

More details in thesis and presentation.
