# Code Citations

## License: GPL-2.0
https://github.com/86Box/86Box/blob/6f8971d1b9877bde4cf30e04c012991858388bc9/README.md

```
inspired by mainstream hypervisor software
* Low level emulation of 8086-based processors up to the Mendocino-era Celeron with focus on accuracy
* Great range of customizability of virtual machines
* Many available systems, such as the very first IBM PC 5150 from 1981, or the more obscure IBM PS/2 line of systems based on the Micro Channel Architecture
* Lots of supported peripherals including video adapters, sound cards, network adapters, hard disk controllers, and SCSI adapters
* MIDI output to Windows built-in MIDI support, FluidSynth, or emulated Roland synthesizers
* Supports running MS-DOS, older Windows versions, OS/2, many Linux distributions, or vintage systems such as BeOS or NEXTSTEP, and
```

## MMX on ARM64 Optimizations

The following references were used for implementing NEON-accelerated MMX emulation in the new dynarec backend for Apple Silicon:

- ARM Architecture Reference Manual ARMv8-A and NEON Programmer’s Guide – instruction semantics and intrinsics mapping (https://developer.arm.com/documentation/).
- Intel 64 and IA-32 Architectures Software Developer’s Manual, Vol. 2 – MMX instruction semantics (https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
- Agner Fog, "Instruction tables / Optimizing subroutines in assembly" – microarchitecture characteristics and instruction latencies (https://www.agner.org/optimize/).
- Apple Performance Optimization Guidelines for Apple Silicon – compiler flags and vectorization notes (https://developer.apple.com/documentation/metal/optimizing-performance-for-apple-silicon/).
- Clang/LLVM docs on vectorization and ARM64 backends – `-Rpass` diagnostics and NEON lowering (https://llvm.org/docs/Vectorizers.html).

