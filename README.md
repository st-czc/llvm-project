# AMD LLVM sources for TheRock HIP CPU builds

Pruned copy of the AMD llvm-project fork, reduced to the sources consumed by
the TheRock HIP CPU build.

- Base: `d6f6cb6` (tag `therock-10.1`)
- Kept: `amd/`, `cmake/`, `LICENSE.TXT`
- LLVM/Clang sources and runtimes come from `compiler/llvm-project`.
- libcxx headers used by comgr's HIPRTC embedding also come from
  `compiler/llvm-project/libcxx`.
- Local commits on top of the base:
  - cherry-picks of AMD upstream comgr fixes (`68a8885e41b2`, `5778526e3e78`)
  - workaround for non-`LLVM_ABI` implicit-arg helpers
