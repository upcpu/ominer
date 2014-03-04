ominer
======
Introduce:

This project contains kernel part of PTS GPU miner ( OpenCL ProtoShare (PTS) Momentum POW miner).
Network code part of ominer UPCPU release is modified from cpuminer 2.3.2.

I prefer to move to XPT support and won't support getwork protocol, skipped network code from cpuminer.

src/*  OpenCL kernel part source of ominer.


Build and test: 
This project is build by CodeBlocks 12.11  by mingw. Just install CodeBlocks and open project file ominer.cbp.

Build and run with "--help" option for usage information.

ominer_kernel --help  

-s VRam size               Option to specify GPU VRam size by MB, one of 128, 256, 512, 1024.
-d gpu_deviece             Option to specify GPU device, begin from 0.
-p platform                Option to specify GPU platform, begin from 0.
-D			   Option to get benchmark and debug information.
