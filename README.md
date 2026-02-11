# BtrFSST
BtrFSST is an extension of [FSST](https://github.com/cwida/fsst), including the contributions of the Bachelor's thesis by Hedi Chehaidar

Contributions:
- DP compression function
- Third frequency counter
- Pruning conflicting symbols

BtrFSST has higher compression factors than FSST by up to 47.7%. Average improvement was at 7.3%.

![CF improvement plot](https://github.com/Hedi-Chehaidar/btrfsst/blob/master/btrfsst-CF-improvement.png)

You can build the project and compile it the same way like in the FSST repository. Use command line options when running the fsst binary file to include the contributions (no options = FSST, all options = BtrFSST).

More details in thesis and presentation.