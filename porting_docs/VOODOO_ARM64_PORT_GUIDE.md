# Voodoo ARM64 Dynarec Port - Complete Implementation Guide

> **One-stop reference for porting the 86Box Voodoo dynarec from x86-64/SSE2 to ARM64/NEON.**

---

## Executive Summary

This guide consolidates all documentation needed to port the 86Box Voodoo graphics card dynamic recompiler from x86-64 to ARM64. The port targets Apple Silicon (M1+) running macOS, generating NEON instructions equivalent to the existing SSE2 codegen.

**Key deliverables:**
- `vid_voodoo_codegen_arm64.h` - New ARM64/NEON codegen (~3500 lines)
- Modifications to `vid_voodoo_render.h` and `vid_voodoo_render.c`
- macOS JIT memory handling with W^X compliance

---

## Part 0.0: Quick Reference Cheat Sheet

**For experienced developers who want to start immediately.**

### Build and Run (30 seconds)

```bash
# Install dependencies
brew install cmake ninja qt@6 sdl2 rtmidi fluid-synth libpng freetype libslirp openal-soft libserialport webp

# Configure environment
BREW_PREFIX=$(brew --prefix)
export PATH="$BREW_PREFIX/opt/qt@6/bin:$PATH"

# Build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEW_DYNAREC=ON
cmake --build build && cmake --install build

# Run with debug
lldb ./dist/86Box.app/Contents/MacOS/86Box -P "/path/to/vm/"
```

### Key File Locations

| File | Purpose | Location |
|:-----|:--------|:---------|
| ARM64 codegen | **[CREATE THIS]** | `src/include/86box/vid_voodoo_codegen_arm64.h` |
| x86-64 reference | Existing implementation | `src/include/86box/vid_voodoo_codegen_x86-64.h` |
| Voodoo types | Main structures | `src/include/86box/vid_voodoo_common.h:259-680` |
| Render dispatch | Integration point | `src/video/vid_voodoo_render.c` |
| Platform detection | Conditional includes | `src/include/86box/vid_voodoo_render.h` |

### Essential LLDB Commands

```bash
# Launch with VM config
lldb ./dist/86Box.app/Contents/MacOS/86Box -P "/path/to/vm/"

# Critical breakpoints
(lldb) b voodoo_generate_arm64      # JIT compilation
(lldb) b voodoo_render_scanline     # Execution entry
(lldb) run

# Examine JIT code
(lldb) p/x data->code_block          # Get address
(lldb) x/20xw data->code_block       # Hex dump instructions
(lldb) dis -s data->code_block -c 50 # Disassemble

# Check registers
(lldb) reg read x19 x20 x21          # State, params, real_y
(lldb) reg read v0 v8 v9             # SIMD registers
(lldb) reg read v0 --format uint16_t[] # View as 16-bit values
```

### Register Allocation Quick Map

| x86-64 | ARM64 | Usage |
|:-------|:------|:------|
| RDI | X19 | `voodoo_state_t*` |
| RSI | X20 | `voodoo_params_t*` |
| R14 | X21 | `real_y` scanline |
| XMM0-7 | V0-7 | Pixel data (scratch) |
| XMM8 | V8 | Constant: `0x0001` (01_w) |
| XMM9 | V9 | Constant: `0x00ff` (ff_w) |

### Common SIMD Translations

| SSE2 | ARM64 NEON |
|:-----|:-----------|
| `PUNPCKLBW xmm, zero` | `UXTL Vd.8H, Vn.8B` |
| `PACKUSWB` | `SQXTUN Vd.8B, Vn.8H` |
| `PADDW` | `ADD Vd.8H, Vn.8H, Vm.8H` |
| `PMULLW` | `MUL Vd.8H, Vn.8H, Vm.8H` |
| `PSRLW xmm, imm` | `USHR Vd.8H, Vn.8H, #imm` |
| `PAND` | `AND Vd.16B, Vn.16B, Vm.16B` |

### Emergency Troubleshooting

**Crash on startup?**
```bash
# Check dynarec is disabled for testing
VOODOO_DYNAREC=0 ./dist/86Box.app/Contents/MacOS/86Box -P "/path/to/vm/"
```

**SIGBUS in JIT code?**
```bash
# Check instruction encoding
(lldb) x/10xw data->code_block
# Verify stack alignment (must be 16-byte aligned at function entry)
(lldb) reg read sp
```

**Wrong rendering output?**
```bash
# Enable frame capture
VOODOO_FRAME_CAPTURE=1 VOODOO_DYNAREC=0 ./86Box  # Interpreter
VOODOO_FRAME_CAPTURE=1 VOODOO_DYNAREC=1 ./86Box  # Dynarec
# Compare frames
python3 scripts/compare_frames.py /tmp/voodoo_frames/interpreter_*.ppm /tmp/voodoo_frames/dynarec_*.ppm
```

### Next Steps

1. Read Part 0.1-0.5 for complete prerequisites
2. Implement runtime toggle (Part 10.6) first
3. Follow incremental development strategy (Part 12)
4. Validate correctness at each phase (Part 13.2)
5. Check definition of done (Part 13.5)

---

## Part 0: Prerequisites and Groundwork

### 0.1 Required Knowledge

Before implementing the ARM64 port, ensure familiarity with:

| Domain | Specific Topics |
|:-------|:----------------|
| **Dynamic Recompilation** | JIT compilation, code caching, block invalidation, runtime specialization |
| **ARM64 Architecture** | AArch64 ISA, register conventions (X0-X30, V0-V31), AAPCS64 calling convention |
| **NEON SIMD** | Vector register layout, element arrangements (.8B, .4H, .2S), lane operations |
| **macOS Security** | W^X enforcement, `MAP_JIT`, `pthread_jit_write_protect_np()` |
| **3dfx Voodoo** | Framebuffer layout, texture mapping units (TMUs), depth buffering, alpha blending |

**Recommended Reading**:
- [ARM64 Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest/)
- [ARM NEON Programmer's Guide](https://developer.arm.com/documentation/den0018/latest/)
- [Apple JIT Memory Guide](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon)

### 0.2 Development Environment Setup

**Required Tools and Dependencies**:
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew dependencies
brew install cmake ninja pkg-config qt@6 sdl2 rtmidi fluid-synth \
  libpng freetype libslirp openal-soft libserialport webp
```

**Why these dependencies**:
- **Qt6**: Required for GUI, must be from Homebrew (CMake scripts rely on Qt6 components and plugins)
- **webp**, **libserialport**: Runtime libraries that get bundled into `86Box.app`
- **ninja**: Fast incremental builds
- **Audio**: `openal-soft`, `rtmidi`, `fluid-synth` for sound emulation
- **Networking**: `libslirp` for network emulation

**Configure Build Environment**:
```bash
cd /Users/anthony/projects/code/86Box-voodoo-dynarec-v2

# Clean previous builds
rm -rf build dist

# Set up environment variables
BREW_PREFIX=$(brew --prefix)
export PATH="$BREW_PREFIX/opt/qt@6/bin:$PATH"

# Configure pkg-config paths
export PKG_CONFIG_PATH="$BREW_PREFIX/opt/freetype/lib/pkgconfig:$BREW_PREFIX/opt/libpng/lib/pkgconfig:$BREW_PREFIX/opt/libslirp/lib/pkgconfig:$BREW_PREFIX/opt/openal-soft/lib/pkgconfig:$BREW_PREFIX/opt/rtmidi/lib/pkgconfig:$BREW_PREFIX/opt/fluidsynth/lib/pkgconfig:$BREW_PREFIX/opt/sdl2/lib/pkgconfig:$BREW_PREFIX/opt/qt@6/lib/pkgconfig:$BREW_PREFIX/opt/libserialport/lib/pkgconfig:$BREW_PREFIX/opt/webp/lib/pkgconfig"

# Configure CMake prefix path (for BundleUtilities to find libraries)
export CMAKE_PREFIX_PATH="$BREW_PREFIX:$BREW_PREFIX/opt/qt@6/lib/cmake:$BREW_PREFIX/opt/qt@6:$BREW_PREFIX/opt/sdl2:$BREW_PREFIX/opt/freetype:$BREW_PREFIX/opt/libpng:$BREW_PREFIX/opt/libslirp:$BREW_PREFIX/opt/openal-soft:$BREW_PREFIX/opt/rtmidi:$BREW_PREFIX/opt/fluidsynth:$BREW_PREFIX/opt/libserialport:$BREW_PREFIX/opt/webp"
```

**Build 86Box (Release)**:
```bash
# Configure with CMake (Ninja generator)
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$PWD/dist \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_MACOSX_BUNDLE=ON \
  -DQT=ON \
  -DUSE_QT6=ON \
  -DOPENAL=ON \
  -DRTMIDI=ON \
  -DFLUIDSYNTH=ON \
  -DMUNT=OFF \
  -DDISCORD=OFF \
  -DNEW_DYNAREC=ON \
  -DLIBSERIALPORT_ROOT="$BREW_PREFIX/opt/libserialport" \
  -DQT_QMAKE_EXECUTABLE="$BREW_PREFIX/opt/qt@6/bin/qmake"

# Build
cmake --build build --config Release

# Install (creates dist/86Box.app with bundled dependencies)
cmake --install build --config Release
```

**Build 86Box with Debug Symbols** (for development):
```bash
cmake -S . -B build-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX=$PWD/dist-debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_MACOSX_BUNDLE=ON \
  -DCMAKE_C_FLAGS="-g3 -O0 -fsanitize=address" \
  -DCMAKE_CXX_FLAGS="-g3 -O0 -fsanitize=address" \
  -DQT=ON \
  -DUSE_QT6=ON \
  -DOPENAL=ON \
  -DRTMIDI=ON \
  -DFLUIDSYNTH=ON \
  -DNEW_DYNAREC=ON \
  -DLIBSERIALPORT_ROOT="$BREW_PREFIX/opt/libserialport" \
  -DQT_QMAKE_EXECUTABLE="$BREW_PREFIX/opt/qt@6/bin/qmake"

cmake --build build-debug
cmake --install build-debug
```

**Verify the Bundle**:
```bash
# Run the app
open dist/86Box.app

# Or run from command line to see console output
./dist/86Box.app/Contents/MacOS/86Box
```

**Notes**:
- The install step runs CMake's `BundleUtilities`, which copies Qt frameworks/plugins and all Homebrew dependencies into `86Box.app`
- `fixup_bundle` rewrites `@rpath` entries (you'll see `install_name_tool` warnings about invalidating signatures - this is normal)
- Use Ninja generator for fast incremental rebuilds

**Enable Debugging Environment Variables**:
```bash
export VOODOO_JIT_PARITY=1      # Compare dynarec vs interpreter output
export VOODOO_DEBUG=1            # Enable verbose logging
export ASAN_OPTIONS=detect_leaks=0  # Disable leak checking for initial dev
```

### 0.3 Codebase Architecture

**File Organization**:
```
86Box-voodoo-dynarec-v2/
├── src/
│   ├── video/
│   │   ├── vid_voodoo.c              # Main emulation loop, device init
│   │   ├── vid_voodoo_render.c       # Pixel rendering, dynarec dispatch
│   │   └── vid_voodoo_fb.c           # Framebuffer access
│   └── include/86box/
│       ├── vid_voodoo_common.h       # voodoo_t structure (line 259-680)
│       ├── vid_voodoo_render.h       # Codegen includes, platform detection
│       ├── vid_voodoo_codegen_x86-64.h  # Reference x86-64 dynarec
│       └── vid_voodoo_codegen_arm64.h   # [NEW] ARM64 dynarec
```

**Voodoo Emulation Flow**:
```
User Program (Windows 98)
    ↓
PCI MMIO Write → vid_voodoo.c::voodoo_write()
    ↓
Command FIFO → voodoo_queue_command()
    ↓
FIFO Thread → voodoo_fifo_thread()
    ↓
Render Params → voodoo_params_t buffer
    ↓
Render Threads (0-3) → voodoo_render_thread()
    ↓
Dynarec Dispatch → vid_voodoo_render.c::voodoo_render_scanline()
    ↓
[IF dynarec_enabled]
    Generated Code → voodoo_arm64_data[].code_block
[ELSE]
    Interpreter → voodoo_render_scanline_interpreter()
    ↓
Framebuffer Write → fb_mem[]
    ↓
Display Update → svga_render()
```

**Dynarec Integration Points**:

1. **Initialization** - `vid_voodoo.c:1058, 1182`
   ```c
   #ifndef NO_CODEGEN
       voodoo_codegen_init(voodoo);  // Allocates code_block memory
   #endif
   ```

2. **Code Generation** - `vid_voodoo_render.c` (called per scanline)
   ```c
   if (cache_miss || state_changed) {
       voodoo_generate_arm64(voodoo, params, state, real_y);
   }
   ```

3. **Execution** - `vid_voodoo_render.c` (pixel processing)
   ```c
   typedef void (*scanline_func_t)(voodoo_state_t *, voodoo_params_t *, int);
   scanline_func_t func = (scanline_func_t)arm64_data->code_block;
   func(state, params, real_y);
   ```

### 0.4 Key Data Structures

**Primary State Structure** - `voodoo_t` ([vid_voodoo_common.h:259-680](file:///Users/anthony/projects/code/86Box-voodoo-dynarec-v2/src/include/86box/vid_voodoo_common.h#L259-L680))

```c
typedef struct voodoo_t {
    // Framebuffer memory
    uint8_t  *fb_mem;              // Main framebuffer
    uint8_t  *tex_mem[2];          // TMU texture memory
    int       fb_size;             // 2MB (Voodoo 1) or 4MB (Voodoo 2)
    
    // Rendering state
    voodoo_params_t params;        // Current render parameters
    int      row_width;            // Scanline stride
    int      col_tiled;            // Voodoo 3 tiled addressing
    int      dual_tmus;            // Voodoo 2: 2 TMUs
    
    // Dynarec control
    int      use_recompiler;       // 1 = codegen available
    void    *codegen_data;         // Platform-specific (x86_data_t or arm64_data_t)
    
    // Threading
    thread_t *render_thread[4];    // Parallel scanline rendering
    int       odd_even_mask;       // Thread dispatch mask
} voodoo_t;
```

**Render Parameters** - `voodoo_params_t` ([vid_voodoo_common.h:103-221](file:///Users/anthony/projects/code/86Box-voodoo-dynarec-v2/src/include/86box/vid_voodoo_common.h#L103-L221))

```c
typedef struct voodoo_params_t {
    // Triangle vertices (fixed-point)
    int32_t vertexAx, vertexAy;
    int32_t vertexBx, vertexBy;
    int32_t vertexCx, vertexCy;
    
    // Color gradients (16.16 fixed-point)
    uint32_t startR, startG, startB, startA;
    int32_t  dRdX, dGdX, dBdX, dAdX;
    int32_t  dRdY, dGdY, dBdY, dAdY;
    
    // Texture coordinates per TMU
    struct {
        int64_t startS, startT, startW;
        int64_t dSdX, dTdX, dWdX;
        int64_t dSdY, dTdY, dWdY;
    } tmu[2];
    
    // Pipeline modes (these drive code specialization)
    uint32_t fbzMode;          // Depth test, dithering, stipple
    uint32_t fbzColorPath;     // Color combine equations
    uint32_t alphaMode;        // Alpha test, blending
    uint32_t fogMode;          // Fog enable, mode
    uint32_t textureMode[2];   // Texture format, filtering
    
    // Framebuffer layout
    int      col_tiled;        // 0 = linear, 1 = tiled (Voodoo 3)
    int      aux_tiled;        // Auxiliary buffer tiling
    int      row_width;        // Scanline stride in pixels
} voodoo_params_t;
```

**Codegen Data** - Platform-specific cache structure

```c
// x86-64 version (reference)
typedef struct voodoo_x86_data_t {
    uint8_t  code_block[8192];     // Generated machine code
    int      xdir;                 // Scanline direction
    uint32_t alphaMode;            // Cached state for invalidation
    uint32_t fbzMode;
    uint32_t fogMode;
    uint32_t fbzColorPath;
    uint32_t textureMode[2];
    uint32_t tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_x86_data_t;

// ARM64 version (to be created)
typedef struct voodoo_arm64_data_t {
    uint8_t  code_block[8192];     // Generated ARM64 code
    int      xdir;
    uint32_t alphaMode;            // Same state fields
    uint32_t fbzMode;
    uint32_t fogMode;
    uint32_t fbzColorPath;
    uint32_t textureMode[2];
    uint32_t tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_arm64_data_t;
```

**Thread Model**:
- **1 FIFO thread**: Processes command queue, fills `params_buffer[]`
- **4 Render threads**: Process scanlines in parallel (odd/even interleaving)
- **Synchronization**: `wake_render_thread[]` events, `render_voodoo_busy[]` flags

### 0.5 Debugging Infrastructure

**Environment Variables**:

```bash
# Compare dynarec output vs interpreter (pixel-exact validation)
export VOODOO_JIT_PARITY=1

# Enable verbose logging (slows execution significantly)
export VOODOO_DEBUG=1

# Force interpreter mode (for baseline testing)
export VOODOO_FORCE_INTERPRETER=1
```

**LLDB Debugging for JIT Code**:

```bash
# Launch 86Box under debugger with a specific VM config
lldb ./dist/86Box.app/Contents/MacOS/86Box -P "/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy/"

# Alternative: Launch debug build with config
lldb ./dist-debug/86Box.app/Contents/MacOS/86Box -P "/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy/"

# Or use your own VM directory path
lldb ./dist/86Box.app/Contents/MacOS/86Box -P "/path/to/your/vm/directory/"

# Set breakpoint before code execution
(lldb) b voodoo_render_scanline
(lldb) run

# When hit, examine generated code
(lldb) x/20i $pc
(lldb) register read x19 x20 x21  # State, params, real_y
(lldb) register read v0 v1 v8 v9  # NEON registers

# Disassemble generated block
(lldb) dis -s <code_block_address> -c 100
```

**Note**: The `-P` parameter specifies the VM directory path (not the config file itself). 86Box will look for `86box.cfg` in that directory. Without this parameter, 86Box will start but won't load a VM automatically.

**Advanced LLDB Workflows**:

**1. Debugging JIT Compilation**:
```bash
# Break when dynarec generates code
(lldb) b voodoo_generate_arm64
(lldb) run

# Step through code generation
(lldb) thread step-over
(lldb) p block_pos    # Show current code buffer position

# Examine generated instructions
(lldb) x/10xw code_block  # Hex dump of instructions
(lldb) dis -s code_block -c 50  # Disassemble first 50 instructions

# Verify instruction encoding
(lldb) x/1xw code_block+<offset>  # Check specific instruction bytes
```

**2. Debugging Generated Code Execution**:
```bash
# Set breakpoint on generated code entry
(lldb) b voodoo_render_scanline
(lldb) run

# When entering JIT code, get code block address
(lldb) p/x data->code_block  # Get address of generated code
(lldb) p block_pos           # Length of generated code

# Set breakpoint inside generated code (if known offset)
(lldb) br set -a <code_block_address + offset>

# Single-step through JIT code
(lldb) si  # Step instruction
(lldb) register read --all  # Check all registers

# Watch for memory corruption
(lldb) watchpoint set expression -w write -- voodoo->fb_mem
```

**3. Register State Inspection**:
```bash
# Check ARM64 general-purpose registers
(lldb) register read x0 x1 x2    # Function arguments
(lldb) register read x19 x20 x21 # Callee-saved (state, params, real_y)
(lldb) register read x29 x30     # Frame pointer, link register
(lldb) register read sp          # Stack pointer

# Check NEON vector registers
(lldb) register read v0 v1 v2 v3    # Caller-saved scratch
(lldb) register read v8 v9 v10 v11  # Constants (01_w, ff_w, ff_b, minus_254)

# Format vector registers as 16-bit values
(lldb) register read v0 --format uint16_t[]
(lldb) register read v8 --format hex

# Check condition flags
(lldb) register read cpsr
```

**4. Memory Examination**:
```bash
# Examine voodoo_t structure
(lldb) p *voodoo
(lldb) p voodoo->fb_mem
(lldb) p voodoo->use_recompiler
(lldb) p voodoo->dynarec_enabled

# Examine framebuffer
(lldb) x/16xw voodoo->fb_mem  # First 64 bytes as 32-bit words
(lldb) memory read -s4 -fx -c16 voodoo->fb_mem

# Examine params structure
(lldb) p *params
(lldb) p params->fbzMode
(lldb) p params->alphaMode
(lldb) p params->textureMode[0]

# Check code block state
(lldb) p *data
(lldb) x/512xb data->code_block  # Dump entire 8KB block
```

**5. Stack Backtrace Analysis**:
```bash
# Show call stack
(lldb) bt

# Show stack with local variables
(lldb) bt all

# Select specific frame
(lldb) frame select 0
(lldb) frame variable  # Show all local variables

# Navigate frames
(lldb) up    # Move up stack
(lldb) down  # Move down stack
```

**6. Conditional Breakpoints**:
```bash
# Break only when specific conditions met
(lldb) b voodoo_render_scanline
(lldb) br modify 1 -c 'voodoo->dynarec_enabled == 1'

# Break on specific render mode
(lldb) b voodoo_generate_arm64
(lldb) br modify 2 -c 'params->fbzMode & 0x10'  # Depth buffering enabled

# Break after N iterations
(lldb) b voodoo_render_scanline
(lldb) br modify 3 -i 1000  # Ignore first 1000 hits
```

**7. Scripted Debugging**:
```bash
# Define helper command to dump JIT code
(lldb) command script add -f my_lldb_helpers.dump_jit_code dump_jit
(lldb) dump_jit data->code_block block_pos

# Python script example (save as my_lldb_helpers.py):
"""
def dump_jit_code(debugger, command, result, internal_dict):
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    frame = process.GetSelectedThread().GetSelectedFrame()
    
    # Parse arguments: address and size
    args = command.split()
    addr = frame.EvaluateExpression(args[0]).GetValueAsUnsigned()
    size = frame.EvaluateExpression(args[1]).GetValueAsUnsigned()
    
    # Disassemble
    insts = target.ReadInstructions(lldb.SBAddress(addr, target), size // 4)
    for inst in insts:
        print(inst)
"""

# Load script
(lldb) command script import ~/my_lldb_helpers.py
```

**8. Crash Analysis**:
```bash
# When app crashes, get crash info
(lldb) thread info
(lldb) bt  # Backtrace

# Check crash address
(lldb) register read pc  # Instruction pointer at crash
(lldb) image lookup -a $pc  # What function/symbol?

# For SIGBUS/SIGSEGV in JIT code
(lldb) x/5i $pc-8  # Show surrounding instructions
(lldb) register read --all  # Check all registers for bad pointers

# Check if address is valid
(lldb) memory read $x19  # Check state pointer
(lldb) memory read $x20  # Check params pointer

# Examine last few instructions executed
(lldb) dis -c 10 -a $pc-32
```

**9. Performance Profiling in LLDB**:
```bash
# Set up time measurement
(lldb) b voodoo_render_scanline
(lldb) commands
Enter commands, one per line. End with CTRL+D.
> p (uint64_t)clock_gettime_nsec_np(1)
> continue
> CTRL+D

# Check execution time
(lldb) b voodoo_render_scanline
(lldb) dis  # Will show subsequent breakpoint hit
# Calculate delta between timestamps
```

**10. Debugging W^X Issues**:
```bash
# Check memory protection
(lldb) memory region <code_block_address>

# Expected output for JIT memory:
# [0x... - 0x...] rwx <-- Read, Write, Execute

# If protection is wrong, check:
(lldb) b pthread_jit_write_protect_np
(lldb) commands
> bt
> register read x0  # Should be 0 for write, 1 for execute
> continue
```

**Common LLDB Commands Reference**:

| Command | Purpose |
|:--------|:--------|
| `b <function>` | Set breakpoint |
| `br list` | List all breakpoints |
| `br delete <num>` | Delete breakpoint |
| `r` or `run` | Start program |
| `c` or `continue` | Continue execution |
| `s` or `step` | Step into (source line) |
| `n` or `next` | Step over (source line) |
| `si` | Step instruction |
| `ni` | Next instruction |
| `finish` | Run until current function returns |
| `p <expr>` | Print expression |
| `x/<fmt> <addr>` | Examine memory |
| `dis` | Disassemble |
| `reg read` | Read registers |
| `bt` | Backtrace |
| `frame variable` | Show local variables |
| `watchpoint set` | Set memory watchpoint |
| `thread list` | List threads |
| `image lookup` | Lookup symbol/address |

**Examining Specific ARM64 Instruction Encodings**:
```bash
# Verify prologue encoding
(lldb) x/10xw data->code_block
# Example expected output for STP X29, X30, [SP, #-224]!:
# 0xa98f7bfd

# Decode instruction manually
(lldb) p/t 0xa98f7bfd
# Binary: 10101001100011110111101111111101
# Verify: opc=10 (64-bit), V=0 (GP reg), L=1 (load), imm7=...

# Check NEON instruction
(lldb) x/1xw data->code_block+<offset>
# For UXTL Vd.8H, Vn.8B: 0x2f00a400 | Rd | Rn
```

**Debugging Tips**:
- Always build with `-g3` for full debug info
- Use `--batch` mode for automated debugging: `lldb --batch -o "b main" -o "run" -o "bt" ./86Box`
- For multithreaded issues, use `thread apply all bt` to see all thread stacks
- Set `DYLD_PRINT_LIBRARIES=1` to debug dylib loading issues
- Use `process save-core` to save crash dumps for later analysis

**Common Failure Modes**:

| Error | Cause | Debug Strategy |
|:------|:------|:---------------|
| **SIGBUS** | Misaligned memory access or bad instruction encoding | Check prologue stack alignment, verify instruction bytes with `objdump` |
| **SIGSEGV** | Invalid pointer (state, params, fb_mem) | Verify X19/X20 contain valid addresses, check `voodoo->fb_mem != NULL` |
| **Rendering corruption** | Incorrect SIMD operation or blend equation | Enable `VOODOO_JIT_PARITY=1`, compare outputs at pixel level |
| **Performance regression** | Missing NEON optimization or excessive branching | Profile with Instruments Time Profiler, check for scalar fallbacks |
| **JIT compilation failure** | `MAP_JIT` not set or W^X violation | Verify `pthread_jit_write_protect_np()` calls, check entitlements |

**Logging Macros** (add to `vid_voodoo_codegen_arm64.h`):

```c
#ifdef VOODOO_DEBUG
#define VOODOO_LOG(...) pclog(__VA_ARGS__)
#else
#define VOODOO_LOG(...) do {} while(0)
#endif

#define VOODOO_ASSERT(cond) \
    do { if (!(cond)) fatal("Voodoo assertion failed: %s at %s:%d\n", \
                            #cond, __FILE__, __LINE__); } while(0)
```

**Disassembly Tools**:

```bash
# Dump generated code to file
xxd code_block.bin code_block.hex

# Disassemble with LLVM
llvm-objdump -d -m -arch arm64 code_block.bin

# Alternative: Use online ARM64 assembler/disassembler
# https://armconverter.com/
```

### 0.6 Migration Strategy from x86-64

**Goal**: Systematically port the existing x86-64 dynarec to ARM64, reusing logic while replacing instruction encoding.

#### 0.6.1 High-Level Approach

The x86-64 dynarec in `vid_voodoo_codegen_x86-64.h` is ~3500 lines of direct byte emission. The ARM64 port follows the same structure but with different opcodes.

**What stays the same**:
- Code generation logic (when to emit what)
- Pipeline state specialization
- Block caching strategy
- Function signatures

**What changes**:
- Register allocation (RDI→X19, RSI→X20, etc.)
- Instruction bytes (x86-64 opcodes → ARM64 opcodes)
- SIMD operations (SSE2 → NEON)
- Calling convention (System V AMD64 → AAPCS64)

#### 0.6.2 Step-by-Step Port Strategy

**Step 1: Create skeleton file**
```bash
cp src/include/86box/vid_voodoo_codegen_x86-64.h \
   src/include/86box/vid_voodoo_codegen_arm64.h
```

**Step 2: Replace header guards and includes**
```c
// Change from:
#ifndef VIDEO_VOODOO_CODEGEN_X86_64_H
// To:
#ifndef VIDEO_VOODOO_CODEGEN_ARM64_H
```

**Step 3: Map data structures**
```c
// x86-64 version
typedef struct voodoo_x86_data_t {
    uint8_t code_block[BLOCK_SIZE];
    // ... state fields ...
} voodoo_x86_data_t;

// ARM64 version (identical structure)
typedef struct voodoo_arm64_data_t {
    uint8_t code_block[BLOCK_SIZE];
    // ... same state fields ...
} voodoo_arm64_data_t;
```

**Step 4: Port emission macros**
```c
// x86-64: Variable-length instructions
#define addbyte(val) do { code_block[block_pos++] = val; } while (0)
#define addword(val) ...
#define addlong(val) ...

// ARM64: Fixed 32-bit instructions
#define emit32(val) do { *(uint32_t*)&code_block[block_pos] = val; block_pos += 4; } while (0)
```

**Step 5: Port prologue/epilogue** (Part 6)
- Replace x86-64 stack setup with ARM64 STP/LDP
- Map register saves (X19-X28, V8-V15)
- Load NEON constants into V8-V11

**Step 6: Port each operation systematically**

Go through the x86-64 file linearly, replacing each operation:

| x86-64 Code Section | ARM64 Equivalent | Reference |
|:-----|:-----------|:----------|
| Load framebuffer pixel | `LDR W`, `LDR D` | Part 4.7 |
| Unpack RGBA to 16-bit | `UXTL` | Part 8.1 |
| Color interpolation | `ADD.4H`, `SMULL` | Part 8 |
| Alpha blending | `MUL.4H`, `USHR` | Part 8.3 |
| Depth test | `CMGT`, `BIC` | Part 7.8 |
| Pack 16-bit to RGBA | `SQXTUN` | Part 8.2 |
| Store result | `STR W`, `STR D` | Part 4.7 |

**Step 7: Port control flow**
```c
// x86-64: JE, JNE, etc.
JE(skip_offset);  // Emits JE rel32

// ARM64: B.cond
int patch = emit_branch_cond(COND_EQ);
// ... code to skip ...
patch_branch(patch);
```

#### 0.6.3 Mapping Template for Each Function

For every function in x86-64 dynarec:

```c
// x86-64 original
static void emit_unpack_rgba(void) {
    op_PUNPCKLBW_xmm_xmm(REG_XMM0, REG_XMM7);  // Zero extend
}

// ARM64 port
static void emit_unpack_rgba_arm64(void) {
    emit32(ARM64_UXTL | Rd(REG_V0) | Rn(REG_V0));  // UXTL V0.8H, V0.8B
}
```

**Translation checklist per function**:
1. Identify the operation's purpose
2. Find ARM64 equivalent in Part 7 (SSE2 to NEON Translation)
3. Look up opcode constant in Part 4
4. Verify register mapping
5. Emit instruction(s)
6. Add comment explaining the operation

#### 0.6.4 Line-by-Line Port Example

**x86-64 code** (simplified):
```c
// Load pixel from framebuffer
op_MOV_r32_m32(REG_EAX, REG_RDI, offset);
// Unpack to 16-bit
op_MOVD_xmm_r32(REG_XMM0, REG_EAX);
op_PUNPCKLBW_xmm_xmm(REG_XMM0, REG_XMM7);  // XMM7 = zero
// Add color gradient
op_PADDW_xmm_xmm(REG_XMM0, REG_XMM1);
// Pack back to 8-bit
op_PACKUSWB_xmm_xmm(REG_XMM0, REG_XMM0);
// Store result
op_MOVD_r32_xmm(REG_EAX, REG_XMM0);
op_MOV_m32_r32(REG_RDI, offset, REG_EAX);
```

**ARM64 port**:
```c
// Load pixel from framebuffer (X19 = voodoo state, contains fb_mem pointer)
emit_ldr_offset(REG_W0, REG_X19, offset);   // LDR W0, [X19, #offset]
// Move to SIMD and unpack to 16-bit
emit32(ARM64_FMOV_S_W | Rd(REG_V0) | Rn(REG_W0));  // FMOV S0, W0
emit32(ARM64_UXTL | Rd(REG_V0) | Rn(REG_V0));      // UXTL V0.8H, V0.8B
// Add color gradient (V1 contains gradient)
emit32(ARM64_ADD_4H | Rd(REG_V0) | Rn(REG_V0) | Rm(REG_V1));  // ADD V0.4H, V0.4H, V1.4H
// Pack back to 8-bit
emit32(ARM64_SQXTUN_8B | Rd(REG_V0) | Rn(REG_V0));  // SQXTUN V0.8B, V0.8H
// Move back to GP and store
emit32(ARM64_FMOV_W_S | Rd(REG_W0) | Rn(REG_V0));   // FMOV W0, S0
emit_str_offset(REG_W0, REG_X19, offset);           // STR W0, [X19, #offset]
```

#### 0.6.5 Validation During Migration

After porting each section:
1. Compile and fix syntax errors
2. Run with interpreter to verify infrastructure
3. Enable dynarec for simple test case (flat triangle)
4. Compare framebuffer against interpreter
5. Only proceed if pixel-perfect match

#### 0.6.6 Migration Progress Tracking

Create `migration_checklist.md` to track progress:

```markdown
# ARM64 Migration Checklist

## Prologue/Epilogue
- [x] Stack frame setup
- [x] Register saves (X19-X28)
- [x] NEON register saves (V8-V15)
- [x] Load NEON constants
- [ ] Argument marshaling

## Data Movement
- [ ] Load pixel from FB
- [ ] Store pixel to FB
- [ ] GP to SIMD transfer
- [ ] SIMD to GP transfer

## SIMD Operations
- [ ] Unpack (UXTL)
- [ ] Pack (SQXTUN)
- [ ] Add (ADD.4H)
- [ ] Subtract (SUB.4H)
- [ ] Multiply (MUL.4H)
- [ ] Shift right (USHR)
- [ ] Logic (AND/ORR/EOR)

## Pipeline Stages
- [ ] Flat shading
- [ ] Gouraud shading
- [ ] Texture fetch
- [ ] Bilinear filter
- [ ] Alpha blend
- [ ] Depth test
- [ ] Fog
```

---

## Part 1: Architecture Analysis

### 1.1 Existing x86-64 Dynarec Strategy

The existing dynarec in `vid_voodoo_codegen_x86-64.h` uses:

| Technique | Description |
|:----------|:------------|
| **Direct byte emission** | `addbyte()`, `addword()`, `addlong()`, `addquad()` macros |
| **State-dependent specialization** | Blocks specialized for `alphaMode`, `fbzMode`, `fogMode`, etc. |
| **Block caching** | 8 blocks × 8KB each per odd/even scanline |
| **Fully inlined** | No external function calls during pixel processing |

### 1.2 Key Data Structures

```c
typedef struct voodoo_x86_data_t {
    uint8_t  code_block[BLOCK_SIZE];  // 8192 bytes
    int      xdir;
    uint32_t alphaMode, fbzMode, fogMode, fbzColorPath;
    uint32_t textureMode[2], tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_x86_data_t;

// ARM64 equivalent:
typedef struct voodoo_arm64_data_t {
    uint8_t  code_block[BLOCK_SIZE];  // 8192 bytes
    int      xdir;
    uint32_t alphaMode, fbzMode, fogMode, fbzColorPath;
    uint32_t textureMode[2], tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_arm64_data_t;
```

### 1.3 x86-64 Register Allocation

| Register | Usage |
|:---------|:------|
| RDI | `voodoo_state_t*` pointer |
| RSI/R15 | `voodoo_params_t*` pointer |
| R14 | `real_y` scanline |
| R9-R13 | Lookup table pointers |
| XMM0-XMM7 | Pixel/texture data (16-bit channels) |
| XMM8-XMM11 | Constants (`01_w`, `ff_w`, `ff_b`, `minus_254`) |

### 1.4 SSE2 Instruction Patterns Used

| Category | Instructions |
|:---------|:-------------|
| **Unpack/Pack** | `PUNPCKLBW`, `PUNPCKHBW`, `PACKUSWB`, `PACKSSDW` |
| **Arithmetic** | `PADDW`, `PSUBW`, `PMULLW`, `PMULHW`, `PSRAD`, `PSRLW`, `PSLLW` |
| **Logic** | `PAND`, `POR`, `PXOR` |
| **Compare** | `PCMPEQ` with masking |
| **Shuffle** | `PSHUFLW`, `PSLLDQ`, `PSRLDQ`, `PINSRW` |
| **Move** | `MOVD`, `MOVQ`, `MOVDQU`, `MOVDQA` |

---

## Part 2: ARM64 Design

### 2.1 File Structure

| File | Purpose |
|:-----|:--------|
| `vid_voodoo_codegen_arm64.h` | **[NEW]** ARM64/NEON codegen |
| `vid_voodoo_render.h` | Modified to conditionally include ARM64 codegen |
| `vid_voodoo_render.c` | Modified to select codegen at runtime |

### 2.2 ARM64 Register Allocation

| Register | Usage |
|:---------|:------|
| **X0-X7** | Scratch/arguments (caller-saved) |
| **X8** | Scratch |
| **X16-X17** | Intra-procedure-call scratch |
| **X19** | `voodoo_state_t*` pointer |
| **X20** | `voodoo_params_t*` pointer |
| **X21** | `real_y` scanline |
| **X22** | `logtable` pointer |
| **X23** | `alookup` table |
| **X24** | `aminuslookup` table |
| **X25** | `bilinear_lookup` table |
| **X26-X28** | Additional lookup pointers |
| **X29** | Frame pointer |
| **X30** | Link register |
| **V0-V7** | Scratch/computation (caller-saved) |
| **V8** | Constant: `0x0001000100010001` (01_w) |
| **V9** | Constant: `0x00ff00ff00ff00ff` (ff_w) |
| **V10** | Constant: `0x00000000ffffffff` (ff_b) |
| **V11** | Constant: `0xff02ff02ff02ff02` (minus_254) |
| **V12-V15** | Additional constants (callee-saved) |
| **V16-V31** | Scratch/pixel data |

### 2.3 Data Type Mapping

| Voodoo Data | ARM64 Representation |
|:------------|:---------------------|
| Packed 8-bit RGBA (32-bit pixel) | Lower 32 bits of D register |
| Unpacked 16-bit channels | V register as 4x16-bit (4H) or 8x16-bit (8H) |
| Dual-pixel processing | Full 128-bit Q register |

---

## Part 3: Emission Infrastructure

### 3.1 Basic Emission Macros

```c
/* ARM64 uses fixed 32-bit instructions */
#define emit32(val)                                 \
    do {                                            \
        *(uint32_t *)&code_block[block_pos] = val;  \
        block_pos += 4;                             \
    } while (0)

/* For 64-bit addresses */
#define emit64_addr(reg, addr) emit_mov_imm64(reg, (uint64_t)(uintptr_t)(addr))
```

### 3.2 Register Field Encoding Macros

```c
/* Register field positions */
#define Rd(x)              (x)           /* Destination: bits 0-4 */
#define Rn(x)              ((x) << 5)    /* First source: bits 5-9 */
#define Rm(x)              ((x) << 16)   /* Second source: bits 16-20 */
#define Rt(x)              (x)           /* Transfer register */
#define Rt2(x)             ((x) << 10)   /* Second transfer register */

/* Immediate fields */
#define IMM12(imm)         ((imm) << 10)         /* 12-bit immediate */
#define IMM16(imm)         ((imm) << 5)          /* 16-bit immediate */
#define SHIFT_12(sh)       ((sh) << 22)          /* 0=none, 1=LSL#12 */
#define HW(hw)             ((hw) << 21)          /* Halfword for MOVK */
#define SHIFT_IMM6(sh)     ((sh) << 10)          /* Shift amount */

/* Branch offsets */
#define OFFSET19(off)      (((off >> 2) << 5) & 0x00ffffe0)  /* Conditional */
#define OFFSET26(off)      ((off >> 2) & 0x03ffffff)         /* Unconditional */

/* Vector shift amounts */
#define V_SHIFT_4H(sh)     (((sh) | 0x10) << 16)  /* 16-bit elements */
#define V_SHIFT_2S(sh)     (((sh) | 0x20) << 16)  /* 32-bit elements */
```

---

## Part 4: Opcode Constants

### 4.1 Data Processing - Immediate

```c
#define ARM64_ADD_IMM_W    0x11000000   /* ADD Wd, Wn, #imm */
#define ARM64_ADD_IMM_X    0x91000000   /* ADD Xd, Xn, #imm */
#define ARM64_SUB_IMM_W    0x51000000   /* SUB Wd, Wn, #imm */
#define ARM64_SUB_IMM_X    0xd1000000   /* SUB Xd, Xn, #imm */
#define ARM64_CMP_IMM_W    0x71000000   /* CMP Wn, #imm */
#define ARM64_CMP_IMM_X    0xf1000000   /* CMP Xn, #imm */
```

### 4.2 Move Wide

```c
#define ARM64_MOVZ_W       0x52800000   /* MOVZ Wd, #imm16, LSL #hw */
#define ARM64_MOVZ_X       0xd2800000   /* MOVZ Xd, #imm16, LSL #hw */
#define ARM64_MOVK_W       0x72800000   /* MOVK Wd, #imm16, LSL #hw */
#define ARM64_MOVK_X       0xf2800000   /* MOVK Xd, #imm16, LSL #hw */
```

### 4.3 Data Processing - Register

```c
#define ARM64_ADD_REG      0x0b000000   /* ADD Wd, Wn, Wm */
#define ARM64_ADD_REG_X    0x8b000000   /* ADD Xd, Xn, Xm */
#define ARM64_SUB_REG      0x4b000000   /* SUB Wd, Wn, Wm */
#define ARM64_MUL          0x1b007c00   /* MUL Wd, Wn, Wm */
#define ARM64_SDIV         0x1ac00c00   /* SDIV Wd, Wn, Wm */
#define ARM64_LSL_REG      0x1ac02000   /* LSLV Wd, Wn, Wm */
#define ARM64_LSR_REG      0x1ac02400   /* LSRV Wd, Wn, Wm */
#define ARM64_ASR_REG      0x1ac02800   /* ASRV Wd, Wn, Wm */
#define ARM64_CLZ          0x5ac01000   /* CLZ Wd, Wn */
```

### 4.4 Logical - Register

```c
#define ARM64_AND_REG      0x0a000000   /* AND Wd, Wn, Wm */
#define ARM64_ORR_REG      0x2a000000   /* ORR Wd, Wn, Wm */
#define ARM64_EOR_REG      0x4a000000   /* EOR Wd, Wn, Wm */
#define ARM64_TST_REG      0x6a000000   /* TST Wn, Wm */
```

### 4.5 Conditional Select

```c
#define ARM64_CSEL         0x1a800000   /* CSEL Wd, Wn, Wm, cond */
#define ARM64_CSEL_X       0x9a800000   /* CSEL Xd, Xn, Xm, cond */

/* Condition codes */
#define COND_EQ  0x0    #define COND_NE  0x1
#define COND_CS  0x2    #define COND_CC  0x3   /* Unsigned >= / < */
#define COND_MI  0x4    #define COND_PL  0x5
#define COND_VS  0x6    #define COND_VC  0x7
#define COND_HI  0x8    #define COND_LS  0x9   /* Unsigned > / <= */
#define COND_GE  0xa    #define COND_LT  0xb
#define COND_GT  0xc    #define COND_LE  0xd

#define CSEL_COND(c)       ((c) << 12)
```

### 4.6 Bitfield Operations

```c
#define ARM64_UBFM         0x53000000   /* UBFM (LSR imm, UBFX, UXTB) */
#define ARM64_SBFM         0x13000000   /* SBFM (ASR imm, SBFX, SXTB) */
```

### 4.7 Load/Store - Unsigned Offset

```c
#define ARM64_LDR_IMM_W    0xb9400000   /* LDR Wt, [Xn, #imm] */
#define ARM64_LDR_IMM_X    0xf9400000   /* LDR Xt, [Xn, #imm] */
#define ARM64_LDRB_IMM     0x39400000   /* LDRB Wt, [Xn, #imm] */
#define ARM64_LDRH_IMM     0x79400000   /* LDRH Wt, [Xn, #imm] */
#define ARM64_LDRSB_IMM    0x39c00000   /* LDRSB Wt, [Xn, #imm] */
#define ARM64_LDRSH_IMM    0x79c00000   /* LDRSH Wt, [Xn, #imm] */
#define ARM64_LDRSW_IMM    0xb9800000   /* LDRSW Xt, [Xn, #imm] */

#define ARM64_STR_IMM_W    0xb9000000   /* STR Wt, [Xn, #imm] */
#define ARM64_STR_IMM_X    0xf9000000   /* STR Xt, [Xn, #imm] */
#define ARM64_STRB_IMM     0x39000000   /* STRB Wt, [Xn, #imm] */
#define ARM64_STRH_IMM     0x79000000   /* STRH Wt, [Xn, #imm] */
```

### 4.8 Load/Store - Register Offset

```c
#define ARM64_LDR_REG_W    0xb8606800   /* LDR Wt, [Xn, Xm, SXTX] */
#define ARM64_LDR_REG_X    0xf8606800   /* LDR Xt, [Xn, Xm, SXTX] */
#define ARM64_LDRB_REG     0x38606800   /* LDRB Wt, [Xn, Xm, SXTX] */
#define ARM64_LDRH_REG     0x78606800   /* LDRH Wt, [Xn, Xm, SXTX] */
#define ARM64_STR_REG_W    0xb8206800   /* STR Wt, [Xn, Xm, SXTX] */
#define ARM64_STRB_REG     0x38206800   /* STRB Wt, [Xn, Xm, SXTX] */
#define ARM64_STRH_REG     0x78206800   /* STRH Wt, [Xn, Xm, SXTX] */
```

### 4.9 Branch Instructions

```c
#define ARM64_B            0x14000000   /* B label */
#define ARM64_BL           0x94000000   /* BL label */
#define ARM64_BR           0xd61f0000   /* BR Xn */
#define ARM64_BLR          0xd63f0000   /* BLR Xn */
#define ARM64_RET          0xd65f0000   /* RET {Xn} */
#define ARM64_B_COND       0x54000000   /* B.cond label */
#define ARM64_CBZ_W        0x34000000   /* CBZ Wt, label */
#define ARM64_CBZ_X        0xb4000000   /* CBZ Xt, label */
#define ARM64_CBNZ_W       0x35000000   /* CBNZ Wt, label */
#define ARM64_CBNZ_X       0xb5000000   /* CBNZ Xt, label */
```

### 4.10 NEON SIMD Instructions

```c
/* Vector Data Movement */
#define ARM64_FMOV_S_W     0x1e270000   /* FMOV Sd, Wn */
#define ARM64_FMOV_W_S     0x1e260000   /* FMOV Wd, Sn */
#define ARM64_INS_S        0x4e040400   /* INS Vd.S[idx], Wn */
#define ARM64_UMOV_S       0x0e043c00   /* UMOV Wd, Vn.S[idx] */
#define ARM64_INS_H        0x6e020400   /* INS Vd.H[idx], Wn */
#define ARM64_UMOV_H       0x0e023c00   /* UMOV Wd, Vn.H[idx] */
#define ARM64_DUP_H        0x0e020400   /* DUP Vd.4H, Vn.H[idx] */

/* Vector Loads/Stores */
#define ARM64_LDR_Q        0x3dc00000   /* LDR Qt, [Xn, #imm] (128-bit) */
#define ARM64_LDR_D        0xfd400000   /* LDR Dt, [Xn, #imm] (64-bit) */
#define ARM64_LDR_S        0xbd400000   /* LDR St, [Xn, #imm] (32-bit) */
#define ARM64_STR_Q        0x3d800000   /* STR Qt, [Xn, #imm] */
#define ARM64_STR_D        0xfd000000   /* STR Dt, [Xn, #imm] */
#define ARM64_STR_S        0xbd000000   /* STR St, [Xn, #imm] */

/* Vector Arithmetic - 8B, 4H, 2S arrangements */
#define ARM64_ADD_8B       0x0e208400   /* ADD Vd.8B, Vn.8B, Vm.8B */
#define ARM64_SUB_8B       0x2e208400   /* SUB Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ADD_4H       0x0e608400   /* ADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SUB_4H       0x2e608400   /* SUB Vd.4H, Vn.4H, Vm.4H */
#define ARM64_MUL_4H       0x0e609c00   /* MUL Vd.4H, Vn.4H, Vm.4H */
#define ARM64_ADD_2S       0x0ea08400   /* ADD Vd.2S, Vn.2S, Vm.2S */
#define ARM64_SUB_2S       0x2ea08400   /* SUB Vd.2S, Vn.2S, Vm.2S */

/* Saturating Add/Subtract */
#define ARM64_UQADD_4H     0x2e600c00   /* UQADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_UQSUB_4H     0x2e602c00   /* UQSUB Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SQADD_4H     0x0e600c00   /* SQADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SQSUB_4H     0x0e602c00   /* SQSUB Vd.4H, Vn.4H, Vm.4H */

/* Vector Widening Multiply */
#define ARM64_SMULL_4S_4H  0x0e60c000   /* SMULL Vd.4S, Vn.4H, Vm.4H */
#define ARM64_SMULL2_4S_8H 0x4e60c000   /* SMULL2 Vd.4S, Vn.8H, Vm.8H */
#define ARM64_UMULL_4S_4H  0x2e60c000   /* UMULL Vd.4S, Vn.4H, Vm.4H */

/* Vector Logical */
#define ARM64_AND_V        0x0e201c00   /* AND Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ORR_V        0x0ea01c00   /* ORR Vd.8B, Vn.8B, Vm.8B */
#define ARM64_EOR_V        0x2e201c00   /* EOR Vd.8B, Vn.8B, Vm.8B */
#define ARM64_BIC_V        0x0e601c00   /* BIC Vd.8B, Vn.8B, Vm.8B */

/* Vector Shifts - Immediate */
#define ARM64_SHL_D        0x0f005400   /* SHL (64-bit arrangement) */
#define ARM64_SHL_Q        0x4f005400   /* SHL (128-bit arrangement) */
#define ARM64_USHR_D       0x2f000400   /* USHR unsigned right */
#define ARM64_USHR_Q       0x6f000400   /* USHR (128-bit) */
#define ARM64_SSHR_D       0x0f000400   /* SSHR signed right */
#define ARM64_SSHR_Q       0x4f000400   /* SSHR (128-bit) */

/* Vector Extend/Narrow */
#define ARM64_UXTL         0x2f00a400   /* UXTL Vd.8H, Vn.8B */
#define ARM64_UXTL_4S      0x2f10a400   /* UXTL Vd.4S, Vn.4H */
#define ARM64_SXTL         0x0f00a400   /* SXTL Vd.8H, Vn.8B */
#define ARM64_XTN          0x0e212800   /* XTN Vd.8B, Vn.8H */
#define ARM64_XTN_4H       0x0e612800   /* XTN Vd.4H, Vn.4S */
#define ARM64_SQXTN_8B     0x0e214800   /* SQXTN saturating */
#define ARM64_SQXTUN_8B    0x2e212800   /* SQXTUN saturating unsigned */
#define ARM64_UQXTN_8B     0x2e214800   /* UQXTN */

/* Vector Interleave */
#define ARM64_ZIP1_8B      0x0e003800   /* ZIP1 Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ZIP1_4H      0x0e403800   /* ZIP1 Vd.4H, Vn.4H, Vm.4H */
#define ARM64_ZIP1_2S      0x0e803800   /* ZIP1 Vd.2S, Vn.2S, Vm.2S */
#define ARM64_ZIP2_8B      0x0e007800   /* ZIP2 Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ZIP2_4H      0x0e407800   /* ZIP2 Vd.4H, Vn.4H, Vm.4H */

/* Vector Compare */
#define ARM64_CMEQ_4H      0x2e608c00   /* CMEQ Vd.4H, Vn.4H, Vm.4H */
#define ARM64_CMGT_4H      0x0e603400   /* CMGT Vd.4H, Vn.4H, Vm.4H */
#define ARM64_CMHI_4H      0x2e603400   /* CMHI unsigned > */

/* Vector EXT */
#define ARM64_EXT_8B       0x2e000000   /* EXT Vd.8B, Vn.8B, Vm.8B, #idx */
```

---

## Part 5: Encoder Functions

### 5.1 Load 64-bit Immediate (MOVZ + MOVK sequence)

```c
static inline void emit_mov_imm64(int reg, uint64_t val)
{
    emit32(ARM64_MOVZ_X | Rd(reg) | IMM16(val & 0xffff) | HW(0));
    if (val & 0xffff0000ull)
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 16) & 0xffff) | HW(1));
    if (val & 0xffff00000000ull)
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 32) & 0xffff) | HW(2));
    if (val & 0xffff000000000000ull)
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 48) & 0xffff) | HW(3));
}
```

### 5.2 ADD/SUB with Immediate

```c
static inline void emit_add_imm(int dst, int src, uint32_t imm)
{
    if (imm == 0) {
        if (dst != src)
            emit32(ARM64_ORR_REG | Rd(dst) | Rn(REG_XZR) | Rm(src));
    } else if (imm <= 0xfff) {
        emit32(ARM64_ADD_IMM_W | Rd(dst) | Rn(src) | IMM12(imm) | SHIFT_12(0));
    } else if ((imm & 0xfff) == 0 && (imm >> 12) <= 0xfff) {
        emit32(ARM64_ADD_IMM_W | Rd(dst) | Rn(src) | IMM12(imm >> 12) | SHIFT_12(1));
    } else {
        emit_mov_imm64(REG_X16, imm);
        emit32(ARM64_ADD_REG | Rd(dst) | Rn(src) | Rm(REG_W16));
    }
}
```

### 5.3 Load/Store with Offset

```c
static inline void emit_ldr_offset(int dst, int base, int offset)
{
    if (offset >= 0 && offset < 16384 && (offset & 3) == 0) {
        emit32(ARM64_LDR_IMM_W | Rt(dst) | Rn(base) | ((offset >> 2) << 10));
    } else {
        emit_mov_imm64(REG_X16, offset);
        emit32(ARM64_LDR_REG_W | Rt(dst) | Rn(base) | Rm(REG_X16));
    }
}
```

### 5.4 Conditional Branches with Forward Patching

```c
static inline int emit_branch_cond(int cond)
{
    int patch_pos = block_pos;
    emit32(ARM64_B_COND | cond);
    return patch_pos;
}

static inline void patch_branch(int patch_pos)
{
    int offset = block_pos - patch_pos;
    uint32_t *insn = (uint32_t *)&code_block[patch_pos];
    *insn |= OFFSET19(offset);
}
```

---

## Part 6: Prologue and Epilogue

### 6.1 Function Prologue

```c
static inline void emit_prologue(voodoo_t *voodoo, voodoo_params_t *params, 
                                  voodoo_state_t *state)
{
    /* Stack frame: 224 bytes
     * X19-X28 (10 regs) + X29/X30 (2 regs) = 96 bytes
     * V8-V15 (8 NEON regs) = 128 bytes
     */
    
    emit32(0xa98f7bfd);  /* STP X29, X30, [SP, #-224]! */
    emit32(0xa9014ff3);  /* STP X19, X20, [SP, #16] */
    emit32(0xa90257f5);  /* STP X21, X22, [SP, #32] */
    emit32(0xa9035ff7);  /* STP X23, X24, [SP, #48] */
    emit32(0xa90467f9);  /* STP X25, X26, [SP, #64] */
    emit32(0xa9056ffb);  /* STP X27, X28, [SP, #80] */
    
    /* Save NEON callee-saved V8-V15 */
    emit32(0x6d0627e8);  /* STP D8, D9, [SP, #96] */
    emit32(0x6d072fea);  /* STP D10, D11, [SP, #112] */
    emit32(0x6d0837ec);  /* STP D12, D13, [SP, #128] */
    emit32(0x6d093fee);  /* STP D14, D15, [SP, #144] */
    
    /* Move args to dedicated registers (AAPCS64):
     * X0 -> X19 (state), X1 -> X20 (params), X2 -> X21 (real_y)
     */
    emit32(ARM64_ORR_REG | (1 << 31) | Rd(REG_X19) | Rn(REG_XZR) | Rm(REG_X0));
    emit32(ARM64_ORR_REG | (1 << 31) | Rd(REG_X20) | Rn(REG_XZR) | Rm(REG_X1));
    emit32(ARM64_ORR_REG | (1 << 31) | Rd(REG_X21) | Rn(REG_XZR) | Rm(REG_X2));
    
    /* Load lookup table pointers */
    emit_mov_imm64(REG_X22, (uintptr_t)&logtable);
    emit_mov_imm64(REG_X23, (uintptr_t)&alookup);
    emit_mov_imm64(REG_X24, (uintptr_t)&aminuslookup);
    emit_mov_imm64(REG_X25, (uintptr_t)&bilinear_lookup);
    
    /* Load NEON constants into V8-V11 */
    emit_mov_imm64(REG_X16, (uintptr_t)&xmm_01_w);
    emit32(ARM64_LDR_Q | Rt(REG_V8) | Rn(REG_X16));
    emit_mov_imm64(REG_X16, (uintptr_t)&xmm_ff_w);
    emit32(ARM64_LDR_Q | Rt(REG_V9) | Rn(REG_X16));
    emit_mov_imm64(REG_X16, (uintptr_t)&xmm_ff_b);
    emit32(ARM64_LDR_Q | Rt(REG_V10) | Rn(REG_X16));
    emit_mov_imm64(REG_X16, (uintptr_t)&minus_254);
    emit32(ARM64_LDR_Q | Rt(REG_V11) | Rn(REG_X16));
}
```

### 6.2 Function Epilogue

```c
static inline void emit_epilogue(void)
{
    /* Restore NEON V8-V15 */
    emit32(0x6d4627e8);  /* LDP D8, D9, [SP, #96] */
    emit32(0x6d472fea);  /* LDP D10, D11, [SP, #112] */
    emit32(0x6d4837ec);  /* LDP D12, D13, [SP, #128] */
    emit32(0x6d493fee);  /* LDP D14, D15, [SP, #144] */
    
    /* Restore GP X19-X28 */
    emit32(0xa9414ff3);  /* LDP X19, X20, [SP, #16] */
    emit32(0xa94257f5);  /* LDP X21, X22, [SP, #32] */
    emit32(0xa9435ff7);  /* LDP X23, X24, [SP, #48] */
    emit32(0xa94467f9);  /* LDP X25, X26, [SP, #64] */
    emit32(0xa9456ffb);  /* LDP X27, X28, [SP, #80] */
    
    emit32(0xa8cf7bfd);  /* LDP X29, X30, [SP], #224 */
    emit32(ARM64_RET | Rn(REG_X30));
}
```

---

## Part 7: SSE2 to NEON Translation

### 7.1 General Purpose Instructions

| x86-64 | ARM64 | Notes |
|:-------|:------|:------|
| `MOV Rd, imm32` | `MOVZ` + `MOVK` | Sequence for large immediates |
| `MOV Rd, [base+off]` | `LDR Wd, [Xn, #off]` | Offset must be scaled |
| `ADD Rd, Rs, imm` | `ADD Wd, Wn, #imm` | 12-bit immediate limit |
| `CMP Ra, Rb` | `CMP Wa, Wb` | Sets NZCV flags |
| `CMOV* Rd, Rs` | `CSEL Wd, Wn, Wm, cond` | Conditional select |
| `SAR Rd, imm` | `ASR Wd, Wn, #imm` | Arithmetic right |
| `SHR Rd, imm` | `LSR Wd, Wn, #imm` | Logical right |
| `IMUL Rd, Rs` | `MUL Wd, Wn, Wm` | |
| `BSR Rd, Rs` | `CLZ Wd, Wn` + `SUB` | 31 - CLZ |
| `TEST Ra, Rb` | `TST Wa, Wb` | AND, flags only |

### 7.2 Branch Instructions

| x86-64 | ARM64 |
|:-------|:------|
| `JMP rel32` | `B label` |
| `JE/JZ` | `B.EQ` |
| `JNE/JNZ` | `B.NE` |
| `JA` (unsigned >) | `B.HI` |
| `JAE` (unsigned >=) | `B.HS` |
| `JB` (unsigned <) | `B.LO` |
| `JBE` (unsigned <=) | `B.LS` |
| `JG` (signed >) | `B.GT` |
| `JGE` (signed >=) | `B.GE` |
| `JL` (signed <) | `B.LT` |
| `JLE` (signed <=) | `B.LE` |

### 7.3 SIMD Data Movement

| SSE2 | NEON | Notes |
|:-----|:-----|:------|
| `MOVD XMM, r32` | `FMOV Sd, Wn` or `INS Vd.S[0], Wn` | GP to SIMD |
| `MOVD r32, XMM` | `FMOV Wd, Sn` or `UMOV Wd, Vn.S[0]` | SIMD to GP |
| `MOVQ XMM, m64` | `LDR Dd, [Xn]` | 64-bit load |
| `MOVDQU XMM, m128` | `LDR Qd, [Xn]` | 128-bit load |

### 7.4 Unpack/Pack Operations

| SSE2 | NEON | Notes |
|:-----|:-----|:------|
| `PUNPCKLBW xmm, zero` | `UXTL Vd.8H, Vn.8B` | Zero-extend bytes |
| `PUNPCKLBW` | `ZIP1 Vd.16B, Vn.16B, Vm.16B` | Interleave low |
| `PUNPCKHBW` | `ZIP2 Vd.16B, Vn.16B, Vm.16B` | Interleave high |
| `PACKUSWB` | `SQXTUN Vd.8B, Vn.8H` | Saturate unsigned narrow |
| `PACKSSDW` | `SQXTN Vd.4H, Vn.4S` | Saturate signed narrow |

### 7.5 Arithmetic Operations

| SSE2 | NEON | Notes |
|:-----|:-----|:------|
| `PADDW` | `ADD Vd.8H, Vn.8H, Vm.8H` | 16-bit add |
| `PSUBW` | `SUB Vd.8H, Vn.8H, Vm.8H` | 16-bit subtract |
| `PMULLW` | `MUL Vd.8H, Vn.8H, Vm.8H` | 16-bit multiply low |
| `PMULHW` | `SMULL` + extract high | Signed multiply high |
| `PMULHUW` | `UMULL` + extract high | Unsigned multiply high |
| `PADDUSW` | `UQADD Vd.8H, Vn.8H, Vm.8H` | Saturating add unsigned |
| `PSUBUSW` | `UQSUB Vd.8H, Vn.8H, Vm.8H` | Saturating sub unsigned |

### 7.6 Shift Operations

| SSE2 | NEON | Notes |
|:-----|:-----|:------|
| `PSRLW imm` | `USHR Vd.8H, Vn.8H, #imm` | Logical right |
| `PSRAW imm` | `SSHR Vd.8H, Vn.8H, #imm` | Arithmetic right |
| `PSLLW imm` | `SHL Vd.8H, Vn.8H, #imm` | Left shift |
| `PSLLDQ imm` | `EXT Vd, Vzero, Vn, #(16-imm)` | Byte shift left |
| `PSRLDQ imm` | `EXT Vd, Vn, Vzero, #imm` | Byte shift right |

### 7.7 Logical Operations

| SSE2 | NEON | Notes |
|:-----|:-----|:------|
| `PAND` | `AND Vd.16B, Vn.16B, Vm.16B` | |
| `POR` | `ORR Vd.16B, Vn.16B, Vm.16B` | |
| `PXOR` | `EOR Vd.16B, Vn.16B, Vm.16B` | |
| `PANDN` | `BIC Vd.16B, Vm.16B, Vn.16B` | Note: operand order reversed |

### 7.8 Shuffle/Permute

| SSE2 | NEON | Notes |
|:-----|:-----|:------|
| `PSHUFLW imm 0xFF` | `DUP Vd.4H, Vn.H[idx]` | Broadcast element |
| `PINSRW xmm, r32, imm` | `INS Vd.H[imm], Wn` | Insert halfword |
| `PEXTRW r32, xmm, imm` | `UMOV Wd, Vn.H[imm]` | Extract halfword |

---

## Part 8: Pipeline Operation Patterns

### 8.1 Unpack RGBA to 16-bit

```c
/* SSE2: PUNPCKLBW XMM0, XMM_ZERO */
static inline void emit_unpack_rgba_to_16bit(int dst_vreg, int src_vreg)
{
    emit32(ARM64_UXTL | Rd(dst_vreg) | Rn(src_vreg));
}
```

### 8.2 Pack 16-bit to RGBA

```c
/* SSE2: PACKUSWB */
static inline void emit_pack_16bit_to_rgba(int dst_vreg, int src_vreg)
{
    emit32(ARM64_SQXTUN_8B | Rd(dst_vreg) | Rn(src_vreg));
}
```

### 8.3 Multiply with Shift (Blend Operation)

```c
/* result = (a * b) >> 8 */
static inline void emit_mul_shr8_4h(int dst, int src_a, int src_b, int scratch)
{
    emit32(ARM64_SMULL_4S_4H | Rd(scratch) | Rn(src_a) | Rm(src_b));
    emit32(ARM64_SSHR_Q | Rd(scratch) | Rn(scratch) | V_SHIFT_2S(32 - 8));
    emit32(ARM64_SQXTN_8B + 0x00400000 | Rd(dst) | Rn(scratch));
}
```

### 8.4 Broadcast Element

```c
/* PSHUFLW XMM0, XMM1, 0xFF -> DUP */
static inline void emit_dup_lane_4h(int dst, int src, int element)
{
    uint32_t index = (element & 3) << 19;
    emit32(0x0e020400 | Rd(dst) | Rn(src) | index);
}
```

### 8.5 Conditional Move

```c
static inline void emit_csel(int dst, int src_true, int src_false, int cond)
{
    emit32(ARM64_CSEL | Rd(dst) | Rn(src_true) | Rm(src_false) | CSEL_COND(cond));
}
```

---

## Part 9: macOS JIT Memory Handling

### 9.1 Memory Allocation

```c
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>

static void *alloc_jit_memory(size_t size)
{
    void *ptr = mmap(NULL, size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT,
                     -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
}

static void free_jit_memory(void *ptr, size_t size)
{
    munmap(ptr, size);
}
```

### 9.2 Write/Execute Transition

```c
static inline void jit_begin_write(void)
{
    pthread_jit_write_protect_np(0);  /* Enable write */
}

static inline void jit_end_write(void *code_ptr, size_t code_size)
{
    pthread_jit_write_protect_np(1);  /* Enable execute */
    sys_icache_invalidate(code_ptr, code_size);
}
```

### 9.3 Usage Pattern

```c
void voodoo_generate_arm64(voodoo_t *voodoo, ...)
{
    uint8_t *code_block = voodoo->arm64_data[...].code_block;
    
    jit_begin_write();
    block_pos = 0;
    emit_prologue(voodoo, params, state);
    /* ... generate pixel pipeline ... */
    emit_epilogue();
    jit_end_write(code_block, block_pos);
}
```

### 9.4 Required Entitlements

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "...">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.allow-jit</key>
    <true/>
    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
    <true/>
</dict>
</plist>
```

---

## Part 10: Platform Detection and File Organization

### 10.1 Preprocessor Conditionals

```c
#if defined(__aarch64__) || defined(_M_ARM64)
    /* ARM64-specific code */
    #if defined(__APPLE__)
        /* macOS ARM64 (JIT signing, MAP_JIT) */
    #endif
#elif defined(__x86_64__) || defined(_M_X64)
    /* x86-64-specific code */
#else
    #define NO_CODEGEN
#endif
```

### 10.2 Render Header Modification

```c
// vid_voodoo_render.h
#if defined(__aarch64__) || defined(_M_ARM64)
#    include <86box/vid_voodoo_codegen_arm64.h>
#elif defined(__x86_64__) || defined(_M_X64)
#    include <86box/vid_voodoo_codegen_x86-64.h>
#else
#    define NO_CODEGEN
#endif
```

### 10.3 Runtime Selection

```c
void voodoo_set_dynarec_mode(voodoo_t *voodoo, int use_dynarec) {
    voodoo->use_dynarec = use_dynarec;
#if defined(__aarch64__) || defined(_M_ARM64)
    if (use_dynarec)
        voodoo_codegen_init_arm64(voodoo);
#elif defined(__x86_64__) || defined(_M_X64)
    if (use_dynarec)
        voodoo_codegen_init(voodoo);
#endif
}
```

---

## Part 10.5: Detailed Integration Changes

> **This section documents ALL changes needed outside of `vid_voodoo_codegen_arm64.h`**

### 10.5.1 `vid_voodoo_render.h` Changes

**Current code (x86-64 only):**
```c
#ifndef VIDEO_VOODOO_RENDER_H
#define VIDEO_VOODOO_RENDER_H

#if !(defined __amd64__ || defined _M_X64)
#    define NO_CODEGEN
#endif

#ifndef NO_CODEGEN
void voodoo_codegen_init(voodoo_t *voodoo);
void voodoo_codegen_close(voodoo_t *voodoo);
#endif
```

**Modified code (supports both x86-64 and ARM64):**
```c
#ifndef VIDEO_VOODOO_RENDER_H
#define VIDEO_VOODOO_RENDER_H

/* Platform-specific codegen includes */
#if defined(__aarch64__) || defined(_M_ARM64)
#    include <86box/vid_voodoo_codegen_arm64.h>
#elif defined(__amd64__) || defined(_M_X64)
#    include <86box/vid_voodoo_codegen_x86-64.h>
#else
#    define NO_CODEGEN
#endif

#ifndef NO_CODEGEN
void voodoo_codegen_init(voodoo_t *voodoo);
void voodoo_codegen_close(voodoo_t *voodoo);
#endif
```

**Key changes:**
1. **Remove** x86-64-only check `#if !(defined __amd64__ ...)`
2. **Add** ARM64 check with include directive
3. **Add** x86-64 check with include directive
4. **Keep** `NO_CODEGEN` for other architectures

### 10.5.2 `vid_voodoo_codegen_arm64.h` Structure

The new file must define the same external interface as x86-64:

```c
#ifndef VIDEO_VOODOO_CODEGEN_ARM64_H
#define VIDEO_VOODOO_CODEGEN_ARM64_H

/* Register assignments matching prologue */
#define REG_STATE         REG_X19
#define REG_PARAMS        REG_X20
#define REG_REAL_Y        REG_X21
#define REG_LOGTABLE      REG_X22
#define REG_ALOOKUP       REG_X23
#define REG_AMINUSLOOKUP  REG_X24
#define REG_BILINEAR      REG_X25
#define REG_SCRATCH       REG_X16
#define REG_SCRATCH2      REG_X17

/* NEON constant registers */
#define VREG_01_W         REG_V8
#define VREG_FF_W         REG_V9
#define VREG_FF_B         REG_V10
#define VREG_MINUS254     REG_V11

/* Data structure */
typedef struct voodoo_arm64_data_t {
    uint8_t  code_block[BLOCK_SIZE];
    int      xdir;
    uint32_t alphaMode;
    uint32_t fbzMode;
    uint32_t fogMode;
    uint32_t fbzColorPath;
    uint32_t textureMode[2];
    uint32_t tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_arm64_data_t;

/* Global data array - matches x86-64 pattern */
static voodoo_arm64_data_t voodoo_arm64_data[2][BLOCK_NUM];

/* Function prototypes matching x86-64 */
static void voodoo_generate_arm64(...);
void voodoo_codegen_init(voodoo_t *voodoo);
void voodoo_codegen_close(voodoo_t *voodoo);

#endif /* VIDEO_VOODOO_CODEGEN_ARM64_H */
```

### 10.5.3 Function Name Consistency

**Important**: The ARM64 version must use the **same function names** as x86-64:

| Function | Both Platforms Use |
|:---------|:-------------------|
| Initialization | `voodoo_codegen_init()` |
| Cleanup | `voodoo_codegen_close()` |
| Generation | `voodoo_generate_*()` (internal, name can differ) |

This allows `vid_voodoo.c` to call the same functions regardless of platform.

### 10.5.4 Memory Allocation Pattern

Both platforms must follow the same allocation pattern:

**x86-64 version:**
```c
void voodoo_codegen_init(voodoo_t *voodoo)
{
    voodoo->codegen_data = plat_mmap(sizeof(voodoo_x86_data_t) * BLOCK_NUM * 4, 1);
}
```

**ARM64 version:**
```c
void voodoo_codegen_init(voodoo_t *voodoo)
{
#if defined(__APPLE__) && defined(__aarch64__)
    /* Use MAP_JIT for macOS */
    voodoo->codegen_data = mmap(NULL, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4,
                                PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
#else
    voodoo->codegen_data = plat_mmap(sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4, 1);
#endif
}
```

### 10.5.5 Constants and Tables

Both platforms share these constants (no changes needed):

```c
extern const uint64_t xmm_01_w;      /* 0x0001000100010001 */
extern const uint64_t xmm_ff_w;      /* 0x00ff00ff00ff00ff */
extern const uint64_t xmm_ff_b;      /* 0x00000000ffffffff */
extern const uint64_t minus_254;     /* 0xff02ff02ff02ff02 */
extern const uint32_t logtable[256];
extern const uint8_t  alookup[256];
extern const uint8_t  aminuslookup[256];
extern const uint8_t  bilinear_lookup[256];
```

### 10.5.6 CMakeLists.txt Changes (Optional)

If you want to explicitly track the ARM64 file:

```cmake
# In the video sources section
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(VIDEO_VOODOO_SOURCES
        ${VIDEO_VOODOO_SOURCES}
        include/86box/vid_voodoo_codegen_arm64.h
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(VIDEO_VOODOO_SOURCES
        ${VIDEO_VOODOO_SOURCES}
        include/86box/vid_voodoo_codegen_x86-64.h
    )
endif()
```

**Note**: This is optional since header-only files are automatically included.

### 10.5.7 No Changes Needed In

These files call `voodoo_codegen_init()` but don't need modification because the function name is the same:

- `src/video/vid_voodoo.c` (lines 1058, 1182)
- `src/video/vid_voodoo_banshee.c` (if applicable)
- `src/video/vid_voodoo_render.c` (uses codegen if available)

All `#ifndef NO_CODEGEN` blocks work automatically once the header is fixed.

---

### 10.6 Runtime Configuration Toggle

To enable early testing of the ARM64 dynarec before full implementation, add a runtime toggle to switch between dynarec and interpreter modes.

#### 10.6.1 Voodoo Structure Modification

Add a new field to control dynarec usage ([vid_voodoo_common.h:665-666](file:///Users/anthony/projects/code/86Box-voodoo-dynarec-v2/src/include/86box/vid_voodoo_common.h#L665-L666)):

```c
typedef struct voodoo_t {
    // ... existing fields ...
    
    int   use_recompiler;       // ← Already exists (line 665)
    void *codegen_data;         // ← Already exists (line 666)
    
    // ADD THIS NEW FIELD after codegen_data:
    int   dynarec_enabled;      // 1 = use dynarec, 0 = use interpreter
    
    // ... rest of structure ...
} voodoo_t;
```

**Implementation steps**:
1. Add `int dynarec_enabled;` to `voodoo_t` after line 666
2. Default to `1` (enabled) in `voodoo_init()`
3. Load/save value in config system

#### 10.6.2 Configuration File Integration

**Loading Config** - Add to `config.c` or device-specific init:

```c
// In voodoo device initialization (vid_voodoo.c)
static void *voodoo_init(const device_t *info) {
    voodoo_t *voodoo = malloc(sizeof(voodoo_t));
    // ... existing initialization ...
    
    // Load dynarec preference from config
    voodoo->dynarec_enabled = device_get_config_int("dynarec");
    
    return voodoo;
}
```

**Saving Config** - Add device config parameter:

```c
// In device_t config definition (vid_voodoo.c)
static const device_config_t voodoo_config[] = {
    // ... existing config items ...
    {
        .name = "dynarec",
        .description = "Enable dynamic recompiler",
        .type = CONFIG_BINARY,
        .default_int = 1  // Default to enabled
    },
    { .type = -1 }  // Terminator
};
```

**Alternative: Environment Variable** (for quick testing without config changes):

```c
// In voodoo_init()
const char *dynarec_env = getenv("VOODOO_DYNAREC");
if (dynarec_env != NULL) {
    voodoo->dynarec_enabled = atoi(dynarec_env);
} else {
    voodoo->dynarec_enabled = device_get_config_int("dynarec");
}
```

#### 10.6.3 Runtime Check in Render Path

Modify the render dispatch to check both flags:

```c
// In vid_voodoo_render.c - scanline rendering function
static void voodoo_render_scanline(voodoo_t *voodoo, voodoo_state_t *state,
                                     voodoo_params_t *params, int real_y) {
    #ifndef NO_CODEGEN
    if (voodoo->use_recompiler && voodoo->dynarec_enabled) {
        // Use generated code
        #if defined(__aarch64__) || defined(_M_ARM64)
            voodoo_arm64_data_t *data = &voodoo_arm64_data[odd_even][block];
        #elif defined(__amd64__) || defined(_M_X64)
            voodoo_x86_data_t *data = &voodoo_x86_data[odd_even][block];
        #endif
        
        // Execute JIT-compiled code
        typedef void (*scanline_func_t)(voodoo_state_t *, voodoo_params_t *, int);
        scanline_func_t func = (scanline_func_t)data->code_block;
        func(state, params, real_y);
    } else
    #endif
    {
        // Fallback to interpreter
        voodoo_render_scanline_interpreter(voodoo, state, params, real_y);
    }
}
```

#### 10.6.4 Compile-Time Toggle (for debugging)

Add CMake option to force interpreter mode:

```cmake
# CMakeLists.txt
option(VOODOO_FORCE_INTERPRETER "Disable Voodoo dynarec" OFF)

if(VOODOO_FORCE_INTERPRETER)
    add_definitions(-DNO_CODEGEN)
endif()
```

Usage:
```bash
cmake .. -DVOODOO_FORCE_INTERPRETER=ON
```

#### 10.6.5 UI Integration (Optional - for GUI builds)

If 86Box has device configuration dialogs (Qt/Windows UI):

```c
// In UI code (device configuration dialog)
QCheckBox *dynarecCheckbox = new QCheckBox("Enable dynamic recompiler");
dynarecCheckbox->setChecked(device_get_config_int("dynarec"));
connect(dynarecCheckbox, &QCheckBox::stateChanged, [](int state) {
    device_set_config_int("dynarec", state == Qt::Checked ? 1 : 0);
});
```

#### 10.6.6 Testing Workflow

**Test 1: Command-line toggle**
```bash
# Test interpreter mode
VOODOO_DYNAREC=0 ./86Box --config test.cfg

# Test dynarec mode
VOODOO_DYNAREC=1 ./86Box --config test.cfg
```

**Test 2: Config file toggle**
```ini
# test.cfg
[Voodoo 3 3000 AGP]
dynarec = 0  # Force interpreter
```

**Test 3: Runtime toggle (if UI available)**
1. Open 86Box settings
2. Go to Voodoo device configuration
3. Uncheck "Enable dynamic recompiler"
4. Restart VM
5. Verify console shows "Voodoo dynarec: DISABLED"

**Test 4: Performance comparison**
```bash
# Benchmark with interpreter
VOODOO_DYNAREC=0 ./86Box --benchmark glquake > interpreter.log

# Benchmark with dynarec
VOODOO_DYNAREC=1 ./86Box --benchmark glquake > dynarec.log

# Compare frame times
grep "frame time" interpreter.log dynarec.log
```

#### 10.6.7 Logging for Toggle State

Add logging to show toggle state at runtime:

```c
// In voodoo_init() or voodoo_codegen_init()
#ifndef NO_CODEGEN
if (voodoo->dynarec_enabled) {
    pclog("Voodoo dynarec: ENABLED (%s)\n", 
          #if defined(__aarch64__)
              "ARM64"
          #elif defined(__x86_64__)
              "x86-64"
          #endif
    );
} else {
    pclog("Voodoo dynarec: DISABLED, using interpreter\n");
}
#else
pclog("Voodoo dynarec: NOT AVAILABLE (compiled with NO_CODEGEN)\n");
#endif
```

---

## Part 11: Hardware Compatibility

| Device | TMUs | Tiled FB | Notes |
|:-------|:-----|:---------|:------|
| **Voodoo 1** | 1 | No | 3D-only, pass-through 2D |
| **Voodoo 2** | 2 | No | SLI capable |
| **Banshee** | 1 | Yes | Integrated 2D/3D |
| **Voodoo 3** | 1 | Yes | 1000/2000/3000/3500 variants |
| **Velocity 100/200** | 1 | Yes | Budget Voodoo 3 |

Variations handled by:
- `voodoo->dual_tmus` - TMU count
- `params->col_tiled` / `params->aux_tiled` - Tiled addressing
- `voodoo->trexInit1` - Feature flags

---

## Part 12: Implementation Roadmap

### 12.1 Incremental Development Strategy

**Goal**: Build functionality incrementally, validating correctness at each stage before adding complexity.

**Phase 0: Infrastructure (Days 1-2)**

Implement the runtime toggle and validation infrastructure before writing any ARM64 code:

1. Add `dynarec_enabled` field to `voodoo_t`
2. Implement config file integration
3. Add runtime dispatch logic
4. Verify interpreter fallback works
5. Set up frame capture for correctness validation (see Part 13.2)

**Phase 1: Minimal Viable Dynarec (Days 3-5)**

Implement the simplest possible rendering path - untextured, flat-shaded triangles with no alpha:

**Pipeline State**:
- `fbzMode`: Depth test disabled, no dithering
- `fbzColorPath`: RGB pass-through (no color combine)
- `alphaMode`: Alpha disabled
- `fogMode`: Fog disabled
- `textureMode[0]`: Texture disabled

**Required SIMD Operations**:
```c
// Data movement
UXTL    // Unpack 8-bit RGBA to 16-bit channels
SQXTUN  // Pack 16-bit channels to 8-bit RGBA (saturate)
FMOV    // Move GP register to SIMD (color values)
UMOV    // Move SIMD to GP register

// Basic arithmetic (if any color gradients)
ADD.4H  // 16-bit add
SUB.4H  // 16-bit subtract

// Memory access
LDR Q/D/S  // Load from memory
STR Q/D/S  // Store to memory
```

**Test Case**: Render a solid red triangle (no gradients, no depth test)
- Expected: Fills pixels with 0xFFFF0000 (red)
- Validation: Compare framebuffer against interpreter output

**Phase 2: Color Interpolation (Days 6-8)**

Add support for Gouraud-shaded triangles (RGB gradients):

**New Requirements**:
- Color gradient interpolation (dRdX, dGdX, dBdX)
- Per-pixel color computation

**Additional SIMD Operations**:
```c
SMULL   // Widening multiply (16-bit to 32-bit)
SSHR    // Shift right preserving sign
SQXTN   // Narrow with saturation
UQADD   // Saturating add (clamp to 255)
UQSUB   // Saturating subtract (clamp to 0)
```

**Test Case**: Render a triangle with R=255 at top, R=0 at bottom
- Validation: Check gradient smoothness, compare against interpreter

**Phase 3: Alpha Blending (Days 9-11)**

Add alpha channel support and blending:

**New Pipeline Modes**:
- `alphaMode`: Alpha blending enabled
- Alpha source: vertex alpha or texture alpha

**Additional SIMD Operations**:
```c
MUL.4H     // 16-bit multiply (for alpha blending)
EOR        // XOR (for some blend modes)
AND/ORR    // Logical ops for masking
```

**Alpha Blend Formula**:
```
result = (src * src_alpha + dst * (255 - src_alpha)) >> 8
```

**Test Case**: Render semi-transparent red triangle (alpha=128) over white background
- Expected: Pink pixels (0xFF808080 for R channel)

**Phase 4: Depth Buffering (Days 12-14)**

Add Z-buffer support:

**New Requirements**:
- Depth test (less, less-equal, etc.)
- Depth write
- 16-bit depth values

**Additional SIMD Operations**:
```c
CMGT  // Compare greater than
CMEQ  // Compare equal
BIC   // Bit clear (for masking)
CSEL  // Conditional select (scalar fallback)
```

**Test Case**: Render two overlapping triangles with depth test
- Front triangle should occlude back triangle
- Validation: Check depth buffer values

**Phase 5: Single Texture (Days 15-18)**

Add single TMU texture mapping (Voodoo 1 compatibility):

**New Requirements**:
- S/T coordinate interpolation
- Texture fetch (point sampling first)
- Texture format decoding (RGB565, ARGB1555)

**Additional SIMD Operations**:
```c
SHL       // Shift left (coordinate scaling)
USHR      // Logical shift right
ZIP1/ZIP2 // Interleave (for RGB565 unpacking)
```

**Test Cases**:
- 64x64 texture with checkerboard pattern
- Verify no texture swimming or coordinate errors

**Phase 6: Bilinear Filtering (Days 19-21)**

Add bilinear texture filtering:

**New Requirements**:
- Fetch 4 texels (2x2 neighborhood)
- Lerp in S direction, then T direction
- Sub-pixel precision handling

**Additional SIMD Operations**:
```c
DUP      // Broadcast element to vector
EXT      // Extract bytes from vector pair
PMULL    // Polynomial multiply (if needed for lerp)
```

**Test Case**: Rotating textured quad (verify smooth filtering at angles)

**Phase 7: Color/Alpha Combine (Days 22-24)**

Implement full `fbzColorPath` and `alphaMode` combine equations:

**Combine Modes**:
- Zero, color, texture, color+texture, etc.
- Invert, add, subtract, multiply blend modes

**Test Cases**: 3DMark 99 rendering tests (requires combine)

**Phase 8: Fog (Days 25-26)**

Add fog table lookup and blending:

**New Requirements**:
- Fog table (64 entries)
- Fog interpolation
- Fog color blending

**Test Case**: Foggy scene with depth cue

**Phase 9: Dual TMU (Days 27-28)**

Add second texture unit (Voodoo 2):

**New Requirements**:
- Second texture fetch
- Multi-texture combine modes

**Test Case**: 3DMark 99 Nature test (uses dual TMU)

**Phase 10: Voodoo 3 Features (Days 29-30)**

Add tiled framebuffer support:

**New Requirements**:
- Address remapping for tiled layout
- 32-bit color (RGBA8888) format

**Test Case**: Windows desktop rendering in Voodoo 3 2D mode

---

### 12.2 Validation at Each Phase

After implementing each phase:

1. Run interpreter with same inputs
2. Capture both framebuffers
3. Compare pixel-by-pixel (allow 0-1 LSB difference due to rounding)
4. If mismatch > 1%: Debug before proceeding
5. Run performance benchmark (should see incremental speedup)

**Cumulative Test Suite**:
- Phase 1: Flat triangles
- Phase 2: + Gouraud shading
- Phase 3: + Alpha blending
- Phase 4: + Depth test
- Phase 5: + Textured triangles
- All previous tests must still pass

---

### 12.3 Time Estimates

| Phase | Description | Estimated Time |
|:------|:------------|:---------------|
| **0. Infrastructure** | Toggle, frame capture | 1-2 days |
| **1. Minimal Dynarec** | Flat shaded, untextured | 2-3 days |
| **2. Color Interpolation** | Gouraud shading | 2-3 days |
| **3. Alpha Blending** | Alpha channel support | 2-3 days |
| **4. Depth Buffering** | Z-buffer operations | 2-3 days |
| **5. Single Texture** | TMU0 point sampling | 3-4 days |
| **6. Bilinear Filtering** | Texture filtering | 2-3 days |
| **7. Color/Alpha Combine** | Full combine modes | 2-3 days |
| **8. Fog** | Fog table and blending | 1-2 days |
| **9. Dual TMU** | Second texture unit | 1-2 days |
| **10. Voodoo 3 Features** | Tiled FB, 32-bit color | 1-2 days |
| **Testing/Polish** | Bug fixes, optimization | 3-5 days |

**Total: 22-35 days** (depending on debugging complexity)

---

## Part 13: Testing and Verification

### 13.1 Build Verification

```bash
cd /Users/anthony/projects/code/86Box-voodoo-dynarec-v2

# Build release version
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEW_DYNAREC=ON
cmake --build build
cmake --install build

# Verify dynarec symbols are present
nm dist/86Box.app/Contents/MacOS/86Box | grep voodoo_codegen_init
nm dist/86Box.app/Contents/MacOS/86Box | grep voodoo_generate

# Check that ARM64 code is included (not NO_CODEGEN)
nm dist/86Box.app/Contents/MacOS/86Box | grep voodoo_arm64_data
```

### 13.2 Correctness Validation Workflow

**Critical**: Performance doesn't matter if output is incorrect. Validate pixel-perfect correctness before optimizing.

#### 13.2.1 Frame Capture Implementation

Add frame dumping capability to 86Box:

```c
// In vid_voodoo_render.c or similar
#ifdef VOODOO_FRAME_CAPTURE
static void dump_framebuffer(voodoo_t *voodoo, const char *prefix, int frame_num) {
    char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/voodoo_frames/%s_frame_%04d.ppm", 
             prefix, frame_num);
    
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    
    // PPM header
    fprintf(f, "P6\n%d %d\n255\n", voodoo->h_disp, voodoo->v_disp);
    
    // Write RGB data (convert from framebuffer format)
    for (int y = 0; y < voodoo->v_disp; y++) {
        for (int x = 0; x < voodoo->h_disp; x++) {
            uint32_t pixel = read_pixel(voodoo, x, y);
            uint8_t rgb[3] = {
                (pixel >> 16) & 0xff,  // R
                (pixel >> 8) & 0xff,   // G
                pixel & 0xff           // B
            };
            fwrite(rgb, 3, 1, f);
        }
    }
    fclose(f);
}
#endif

// Call before and after rendering
dump_framebuffer(voodoo, "interpreter", frame_count);  // With dynarec_enabled=0
dump_framebuffer(voodoo, "dynarec", frame_count);      // With dynarec_enabled=1
```

#### 13.2.2 Pixel-Perfect Comparison Script

Create `scripts/compare_frames.py`:

```python
#!/usr/bin/env python3
import sys
import numpy as np
from PIL import Image

def compare_frames(interp_path, dynarec_path, tolerance=1):
    """Compare two frames, allowing for minor rounding differences."""
    img1 = np.array(Image.open(interp_path))
    img2 = np.array(Image.open(dynarec_path))
    
    if img1.shape != img2.shape:
        print(f"ERROR: Shape mismatch: {img1.shape} vs {img2.shape}")
        return False
    
    # Absolute difference per pixel
    diff = np.abs(img1.astype(int) - img2.astype(int))
    
    # Count pixels exceeding tolerance
    bad_pixels = np.sum(np.any(diff > tolerance, axis=2))
    total_pixels = img1.shape[0] * img1.shape[1]
    error_rate = bad_pixels / total_pixels
    
    print(f"  Pixels differing by >{tolerance}: {bad_pixels}/{total_pixels} ({error_rate*100:.3f}%)")
    
    if error_rate > 0.01:  # More than 1% error
        print(f"  FAIL: Too many mismatched pixels")
        # Save diff image
        diff_img = np.clip(diff * 50, 0, 255).astype(np.uint8)
        Image.fromarray(diff_img).save(dynarec_path.replace('.ppm', '_diff.png'))
        return False
    
    # Compute PSNR for remaining differences
    mse = np.mean(diff ** 2)
    if mse > 0:
        psnr = 10 * np.log10(255**2 / mse)
        print(f"  PSNR: {psnr:.2f} dB")
    
    print(f"  PASS")
    return True

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: compare_frames.py <interpreter.ppm> <dynarec.ppm>")
        sys.exit(1)
    
    success = compare_frames(sys.argv[1], sys.argv[2])
    sys.exit(0 if success else 1)
```

#### 13.2.3 Automated Test Harness

Create `scripts/validate_dynarec.sh`:

```bash
#!/bin/bash
set -e

VM_PATH="/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy/"
EXE="./dist/86Box.app/Contents/MacOS/86Box"
FRAMES_DIR="/tmp/voodoo_frames"
TEST_DURATION=30  # seconds

mkdir -p "$FRAMES_DIR"

echo "=== Running interpreter baseline ==="
VOODOO_DYNAREC=0 VOODOO_FRAME_CAPTURE=1 timeout $TEST_DURATION \
  $EXE -P "$VM_PATH" || true

sleep 2

echo "=== Running dynarec test ==="
VOODOO_DYNAREC=1 VOODOO_FRAME_CAPTURE=1 timeout $TEST_DURATION \
  $EXE -P "$VM_PATH" || true

echo "=== Comparing frames ==="
PASS=0
FAIL=0

for interp_frame in $FRAMES_DIR/interpreter_frame_*.ppm; do
    frame_num=$(basename "$interp_frame" | sed 's/interpreter_frame_\([0-9]*\).ppm/\1/')
    dynarec_frame="$FRAMES_DIR/dynarec_frame_${frame_num}.ppm"
    
    if [[ ! -f "$dynarec_frame" ]]; then
        echo "Missing dynarec frame: $frame_num"
        continue
    fi
    
    echo "Comparing frame $frame_num..."
    if python3 scripts/compare_frames.py "$interp_frame" "$dynarec_frame"; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=== Results ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [[ $FAIL -gt 0 ]]; then
    echo "VALIDATION FAILED - Check diff images in $FRAMES_DIR"
    exit 1
else
    echo "VALIDATION PASSED - All frames match within tolerance"
    exit 0
fi
```

#### 13.2.4 Known Good Test Cases

Create reference images for regression testing:

**Test 1: Solid Color Triangle**
- Input: Flat red triangle, no depth, no texture
- Expected hash: `md5sum` of framebuffer = known value
- Location: `test/golden/flat_triangle_red.ppm`

**Test 2: Gouraud Shaded Triangle**
- Input: RGB gradient from red (top) to blue (bottom)
- Expected: Smooth gradient, no banding
- Location: `test/golden/gouraud_triangle.ppm`

**Test 3: Alpha Blending**
- Input: Semi-transparent red triangle over white background
- Expected: Pink result (precise alpha math)
- Location: `test/golden/alpha_blend.ppm`

**Test 4: Textured Quad**
- Input: 64x64 checkerboard texture
- Expected: No texture swimming or missing texels
- Location: `test/golden/textured_quad.ppm`

#### 13.2.5 Continuous Validation

Add to CI:
```yaml
# .github/workflows/correctness.yml
- name: Run correctness validation
  run: |
    ./scripts/validate_dynarec.sh
    
- name: Upload diff images on failure
  if: failure()
  uses: actions/upload-artifact@v3
  with:
    name: frame-diffs
    path: /tmp/voodoo_frames/*_diff.png
```

### 13.3 Test Games and Demos

| Game/Demo | Features Tested | Expected Result |
|:----------|:----------------|:----------------|
| **GLQuake** | Single TMU, depth buffering, alpha | Smooth gameplay, correct lighting |
| **Quake 2** | Bilinear filtering, 32-bit color | No texture shimmering |
| **Unreal** | Dual TMU, fog, detail textures | Correct multi-texturing |
| **3DMark 99** | All features, stress test | No crashes, all scenes render |
| **Tomb Raider** | Alpha blending, depth complexity | Correct transparency |
| **Forsaken** | Particle effects, additive blending | Correct blend modes |

### 13.4 Manual Verification Checklist

Before declaring a phase complete:

- [ ] Build succeeds without warnings
- [ ] Dynarec can be toggled on/off without crashes
- [ ] Interpreter and dynarec produce identical output (pixel-perfect)
- [ ] No memory leaks (run with AddressSanitizer)
- [ ] No SIGBUS/SIGSEGV crashes during 10-minute stress test
- [ ] Performance is at least 2x interpreter speed
- [ ] Frame rate is consistent (no stuttering)
- [ ] All previous phase test cases still pass

### 13.5 Definition of Done - Acceptance Criteria

The ARM64 dynarec port is considered **complete** when all of the following criteria are met:

#### 13.5.1 Correctness (MUST PASS)

| Criterion | Measurement | Target |
|:----------|:------------|:-------|
| **Pixel-perfect rendering** | Frame comparison vs interpreter | 99% of pixels within 1 LSB |
| **All test cases pass** | Automated test suite | 100% pass rate |
| **Visual regression** | Manual inspection | No visible artifacts |
| **Golden image match** | MD5 hash of known scenes | Exact match |

**Must pass for all pipeline states**:
- [ ] Flat shaded triangles (no texture)
- [ ] Gouraud shaded triangles
- [ ] Textured triangles (point sampling)
- [ ] Textured triangles (bilinear filtering)
- [ ] Alpha blending (all blend modes)
- [ ] Depth buffering (all compare modes)
- [ ] Fog (table lookup)
- [ ] Dual TMU (Voodoo 2)
- [ ] Tiled framebuffer (Voodoo 3)

#### 13.5.2 Performance (MUST ACHIEVE)

| Benchmark | Interpreter Baseline | Dynarec Target | Minimum Acceptable |
|:----------|:--------------------|:---------------|:-------------------|
| **GLQuake 640x480** | ~15 FPS | 45-60 FPS | 38 FPS (2.5x) |
| **3DMark 99 Nature** | ~8 FPS | 25-30 FPS | 20 FPS (2.5x) |
| **Fillrate (textured)** | ~5 Mpix/s | 15-20 Mpix/s | 12.5 Mpix/s (2.5x) |
| **Fillrate (untextured)** | ~10 Mpix/s | 30-40 Mpix/s | 25 Mpix/s (2.5x) |

**Performance regression policy**:
- Dynarec must be at least 2.5x faster than interpreter (minimum)
- Target speedup is 3-4x (matching x86-64 dynarec)
- JIT compilation overhead must be <100ms per frame
- No frame stuttering or dropped frames

#### 13.5.3 Stability (MUST ACHIEVE)

| Test | Duration | Success Criteria |
|:-----|:---------|:-----------------|
| **Stress test** | 1 hour continuous rendering | No crashes |
| **Mode switching** | 1000 toggle cycles | No memory leaks |
| **Memory safety** | Run with ASan | No errors |
| **Thread safety** | 4-thread rendering | No race conditions |

**Zero tolerance for**:
- SIGBUS / SIGSEGV crashes
- Memory leaks (>10MB over 1 hour)
- Race conditions in render threads
- W^X violations

#### 13.5.4 Compatibility (MUST SUPPORT)

**Hardware variants**:
- [ ] Voodoo 1 (single TMU, no tiling)
- [ ] Voodoo 2 (dual TMU, no tiling, SLI)
- [ ] Banshee (single TMU, tiled, 2D/3D)
- [ ] Voodoo 3 (single TMU, tiled, 32-bit)

**Guest operating systems**:
- [ ] Windows 95
- [ ] Windows 98 SE
- [ ] Windows ME
- [ ] DOS (pure DOS mode)

**Must run without regression**:
- [ ] GLQuake (Voodoo 1)
- [ ] Quake 2 (Voodoo 2)
- [ ] Unreal (dual TMU)
- [ ] 3DMark 99 (all tests)
- [ ] Half-Life (alpha blending)
- [ ] Need for Speed III (fog)

#### 13.5.5 Code Quality (SHOULD ACHIEVE)

- [ ] Code follows existing 86Box style
- [ ] All functions documented
- [ ] No compiler warnings (-Wall -Wextra)
- [ ] Passes static analysis (clang-tidy)
- [ ] LLDB debugging commands documented
- [ ] Incremental development phases documented

#### 13.5.6 Documentation (MUST COMPLETE)

- [ ] All SIMD operations documented
- [ ] Instruction encoding verified
- [ ] Register allocation documented
- [ ] Known limitations documented
- [ ] Troubleshooting guide complete
- [ ] Example debug sessions documented

#### 13.5.7 Deliverables Checklist

**Code**:
- [ ] `src/include/86box/vid_voodoo_codegen_arm64.h` (complete)
- [ ] `src/include/86box/vid_voodoo_common.h` (dynarec_enabled field added)
- [ ] Runtime toggle implementation
- [ ] Frame capture/validation tools

**Testing**:
- [ ] Automated test suite
- [ ] Golden image database
- [ ] Performance benchmark suite
- [ ] CI/CD integration

**Documentation**:
- [ ] Implementation guide (this document)
- [ ] Walkthrough with validation results
- [ ] Known issues and workarounds
- [ ] Performance tuning guide

---

**Final Sign-Off**:

The port is **COMPLETE** when:
1. All MUST criteria are met
2. At least 90% of SHOULD criteria are met
3. No known crashes or rendering artifacts
4. Performance meets minimum 2.5x target
5. All documentation is current

**Expected Timeline**: 22-35 days from start to completion

---

## Part 14: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|:-----|:-----------|:-------|:-----------|
| **NEON precision** | Medium | Medium | Match exact math, avoid fused ops |
| **Branch range** | Low | High | Veneer/trampoline for long jumps |
| **W^X violations** | Medium | High | Follow CPU dynarec patterns |
| **Encoding bugs** | Medium | High | Test encoders in isolation |
| **Tiled FB bugs** | Medium | Medium | Extensive Voodoo 3/Banshee testing |

---

## Part 15: Reference Code

The existing CPU dynarec in `src/codegen_new/codegen_backend_arm64_ops.c` has working implementations:

| Function | Purpose |
|:---------|:--------|
| `host_arm64_ADD_IMM` | ADD with immediate |
| `host_arm64_mov_imm` | Loading 32/64-bit immediates |
| `host_arm64_ADD_V4H` | NEON vector add |
| `host_arm64_SMULL_V4S_4H` | Widening multiply |
| `host_arm64_ZIP1_V4H` | Vector interleave |
| `host_arm64_USHR_V4H` | Vector unsigned shift right |
| `codegen_addlong` | Core emission function |

---

## Part 16: Files Summary

### New Files
- `src/include/86box/vid_voodoo_codegen_arm64.h` (~3500 lines)

### Modified Files
- `src/include/86box/vid_voodoo_render.h` - Add ARM64 include
- `src/video/vid_voodoo.c` - Runtime dynarec selection
- `CMakeLists.txt` - Add ARM64 codegen to build

---

## Part 17: Benchmarking Infrastructure

### 17.1 Benchmark Architecture

**Goal**: Quantify performance improvement of ARM64 dynarec over interpreter and enable regression detection.

**Performance Metrics**:

| Metric | Unit | Purpose |
|:-------|:-----|:--------|
| **Frame time** | ms/frame | Real-world responsiveness |
| **Throughput** | Mpixels/sec, Mtexels/sec | Raw processing power |
| **Triangle rate** | Ktris/sec | 3D geometry performance |
| **JIT overhead** | ms | Compilation cost vs runtime savings |
| **Memory usage** | MB | Code cache impact |

**Test Workloads**:

| Workload | Type | Coverage |
|:---------|:-----|:---------|
| **GLQuake timedemo** | Real-world game | Voodoo 1 compatibility, single TMU |
| **3DMark 99 Nature** | Synthetic benchmark | Dual TMU, high fill rate, fog |
| **Custom fillrate test** | Micro-benchmark | Isolated pixel pipeline, worst-case |
| **Texture stress test** | Micro-benchmark | Bilinear filtering, LOD selection |

### 17.2 Benchmark Implementation

Create `benchmarks/voodoo_dynarec_bench.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "86box/86box.h"
#include "86box/vid_voodoo.h"

/* High-resolution timer */
static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

typedef struct benchmark_result_t {
    const char *workload_name;
    double interpreter_fps;
    double dynarec_fps;
    double speedup;
    uint64_t pixels_rendered;
    uint64_t texels_fetched;
    uint64_t triangles;
    uint64_t compile_time_ns;
    double compile_overhead_pct;
} benchmark_result_t;

benchmark_result_t run_benchmark(voodoo_t *voodoo, 
                                   const char *workload_name,
                                   int frame_count) {
    benchmark_result_t result = {0};
    result.workload_name = workload_name;
    
    /* Reset counters */
    voodoo->pixel_count[0] = voodoo->pixel_count[1] = 0;
    voodoo->pixel_count[2] = voodoo->pixel_count[3] = 0;
    voodoo->texel_count[0] = voodoo->texel_count[1] = 0;
    voodoo->texel_count[2] = voodoo->texel_count[3] = 0;
    voodoo->tri_count = 0;
    
    /* === Interpreter Baseline === */
    voodoo->dynarec_enabled = 0;
    uint64_t t0 = get_time_ns();
    
    for (int i = 0; i < frame_count; i++) {
        render_test_frame(voodoo);  /* Defined per workload */
    }
    
    uint64_t t1 = get_time_ns();
    result.interpreter_fps = frame_count / ((t1 - t0) / 1e9);
    
    /* === Dynarec Test === */
    voodoo->dynarec_enabled = 1;
    
    /* Measure compilation time */
    uint64_t compile_start = get_time_ns();
    #ifndef NO_CODEGEN
    voodoo_codegen_init(voodoo);
    /* Force compilation of all blocks by rendering one frame */
    render_test_frame(voodoo);
    #endif
    uint64_t compile_end = get_time_ns();
    result.compile_time_ns = compile_end - compile_start;
    
    /* Measure runtime performance */
    voodoo->pixel_count[0] = voodoo->pixel_count[1] = 0;
    voodoo->pixel_count[2] = voodoo->pixel_count[3] = 0;
    voodoo->texel_count[0] = voodoo->texel_count[1] = 0;
    voodoo->texel_count[2] = voodoo->texel_count[3] = 0;
    voodoo->tri_count = 0;
    
    t0 = get_time_ns();
    for (int i = 0; i < frame_count; i++) {
        render_test_frame(voodoo);
    }
    t1 = get_time_ns();
    result.dynarec_fps = frame_count / ((t1 - t0) / 1e9);
    
    /* Calculate metrics */
    result.speedup = result.dynarec_fps / result.interpreter_fps;
    result.pixels_rendered = voodoo->pixel_count[0] + voodoo->pixel_count[1] +
                             voodoo->pixel_count[2] + voodoo->pixel_count[3];
    result.texels_fetched = voodoo->texel_count[0] + voodoo->texel_count[1] +
                            voodoo->texel_count[2] + voodoo->texel_count[3];
    result.triangles = voodoo->tri_count;
    result.compile_overhead_pct = (result.compile_time_ns / 1e9) / 
                                   ((frame_count / result.dynarec_fps) * 100.0);
    
    /* Print results */
    printf("=== %s ===\n", workload_name);
    printf("  Interpreter:  %.2f FPS\n", result.interpreter_fps);
    printf("  Dynarec:      %.2f FPS\n", result.dynarec_fps);
    printf("  Speedup:      %.2fx\n", result.speedup);
    printf("  Pixels:       %llu (%.2f Mpix/s dynarec)\n", 
           result.pixels_rendered, 
           (result.pixels_rendered / 1e6) * result.dynarec_fps);
    printf("  Texels:       %llu (%.2f Mtex/s dynarec)\n",
           result.texels_fetched,
           (result.texels_fetched / 1e6) * result.dynarec_fps);
    printf("  Triangles:    %llu\n", result.triangles);
    printf("  Compile time: %.2f ms (%.3f%% overhead)\n",
           result.compile_time_ns / 1e6, result.compile_overhead_pct);
    printf("\n");
    
    return result;
}

/* Example workload: fillrate stress test */
static void render_fillrate_test(voodoo_t *voodoo) {
    /* Render full-screen quad (640x480) */
    voodoo_params_t params = {0};
    params.command = CMD_FASTTRIANGLE;
    
    /* Vertex A (top-left) */
    params.vertexAx = 0 << 16;
    params.vertexAy = 0 << 16;
    
    /* Vertex B (bottom-left) */
    params.vertexBx = 0 << 16;
    params.vertexBy = 480 << 16;
    
    /* Vertex C (top-right) */
    params.vertexCx = 640 << 16;
    params.vertexCy = 0 << 16;
    
    /* Solid red color */
    params.startR = 0xff0000;
    params.startG = 0;
    params.startB = 0;
    params.startA = 0xff;
    
    /* Pipeline state */
    params.fbzMode = 0; /* No depth test */
    params.alphaMode = 0; /* No alpha test */
    params.textureMode[0] = 0; /* No texture */
    
    voodoo_queue_triangle(voodoo, &params);
    voodoo_queue_triangle(voodoo, &params); /* Second tri for full quad */
}

int main(int argc, char **argv) {
    /* Initialize 86Box emulator */
    voodoo_t *voodoo = voodoo_init(&voodoo3_3000_device);
    
    /* Run benchmarks */
    benchmark_result_t results[3];
    results[0] = run_benchmark(voodoo, "Fillrate Stress Test", 100);
    results[1] = run_benchmark(voodoo, "GLQuake Timedemo", 50);
    results[2] = run_benchmark(voodoo, "3DMark99 Nature", 30);
    
    /* Summary */
    printf("=== SUMMARY ===\n");
    double avg_speedup = 0;
    for (int i = 0; i < 3; i++) {
        avg_speedup += results[i].speedup;
    }
    avg_speedup /= 3;
    printf("Average speedup: %.2fx\n", avg_speedup);
    
    /* Cleanup */
    voodoo_close(voodoo);
    return 0;
}
```

### 17.3 Automated Test Harness

Create `scripts/run_voodoo_bench.sh`:

```bash
#!/bin/bash
set -e

WORKLOADS=("fillrate" "glquake_timedemo" "3dmark99_nature")
ITERATIONS=5
RESULTS_DIR="benchmark_results_$(date +%Y%m%d_%H%M%S)"
BUILD_DIR="${BUILD_DIR:-build}"

mkdir -p "$RESULTS_DIR"

echo "Building 86Box with benchmarking support..."
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_VOODOO_BENCH=ON
make -j$(sysctl -n hw.ncpu) voodoo_dynarec_bench
cd ..

echo "Running benchmarks (${ITERATIONS} iterations)..."
for workload in "${WORKLOADS[@]}"; do
    echo "  Workload: $workload"
    for iter in $(seq 1 $ITERATIONS); do
        echo -n "    Iteration $iter/$ITERATIONS... "
        ./"$BUILD_DIR"/voodoo_dynarec_bench "$workload" > \
            "$RESULTS_DIR/${workload}_${iter}.json" 2>&1
        echo "done"
    done
done

echo "Analyzing results..."
python3 scripts/analyze_bench.py "$RESULTS_DIR"

echo "Results saved to: $RESULTS_DIR"
echo "Summary: $RESULTS_DIR/summary.json"
```

Create `scripts/analyze_bench.py`:

```python
#!/usr/bin/env python3
import json
import sys
import glob
from pathlib import Path
from statistics import mean, stdev

def parse_bench_output(filepath):
    """Parse benchmark output JSON."""
    with open(filepath) as f:
        return json.load(f)

def aggregate_results(results_dir):
    """Aggregate multiple benchmark runs."""
    workloads = {}
    
    for json_file in glob.glob(f"{results_dir}/*.json"):
        filename = Path(json_file).stem
        workload_name = "_".join(filename.split("_")[:-1])
        
        result = parse_bench_output(json_file)
        
        if workload_name not in workloads:
            workloads[workload_name] = []
        workloads[workload_name].append(result)
    
    # Calculate statistics
    summary = {}
    for workload, runs in workloads.items():
        speedups = [r["speedup"] for r in runs]
        dynarec_fps = [r["dynarec_fps"] for r in runs]
        
        summary[workload] = {
            "speedup_mean": mean(speedups),
            "speedup_stdev": stdev(speedups) if len(speedups) > 1 else 0,
            "dynarec_fps_mean": mean(dynarec_fps),
            "dynarec_fps_stdev": stdev(dynarec_fps) if len(dynarec_fps) > 1 else 0,
            "iterations": len(runs)
        }
    
    # Save summary
    summary_path = f"{results_dir}/summary.json"
    with open(summary_path, "w") as f:
        json.dump(summary, f, indent=2)
    
    # Print summary
    print("\n=== BENCHMARK SUMMARY ===")
    for workload, stats in summary.items():
        print(f"\n{workload}:")
        print(f"  Speedup:     {stats['speedup_mean']:.2f}x ± {stats['speedup_stdev']:.2f}")
        print(f"  Dynarec FPS: {stats['dynarec_fps_mean']:.1f} ± {stats['dynarec_fps_stdev']:.1f}")
        print(f"  Iterations:  {stats['iterations']}")
    
    return summary

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: analyze_bench.py <results_dir>")
        sys.exit(1)
    
    results_dir = sys.argv[1]
    aggregate_results(results_dir)
```

### 17.4 Performance Regression Detection

Add CI integration (`.github/workflows/voodoo_perf.yml`):

```yaml
name: Voodoo Dynarec Performance Test

on:
  pull_request:
    paths:
      - 'src/video/vid_voodoo*.c'
      - 'src/include/86box/vid_voodoo*.h'
      - 'benchmarks/**'

jobs:
  benchmark:
    runs-on: macos-14  # Apple Silicon runner
    
    steps:
      - name: Checkout code
        uses: actions/checkout@v4
      
      - name: Install dependencies
        run: |
          brew install cmake llvm
      
      - name: Build with ARM64 dynarec
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_VOODOO_BENCH=ON
          make -j$(sysctl -n hw.ncpu) voodoo_dynarec_bench
      
      - name: Run benchmarks
        run: |
          chmod +x scripts/run_voodoo_bench.sh
          ./scripts/run_voodoo_bench.sh
      
      - name: Download baseline results
        uses: actions/download-artifact@v3
        with:
          name: baseline-results
          path: baseline_results/
        continue-on-error: true
      
      - name: Compare against baseline
        run: |
          python3 scripts/compare_perf.py \
            --baseline baseline_results/summary.json \
            --current benchmark_results_*/summary.json \
            --threshold 0.95
      
      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: current-results
          path: benchmark_results_*/
```

Create `scripts/compare_perf.py`:

```python
#!/usr/bin/env python3
import json
import sys
import argparse

def compare_performance(baseline_file, current_file, threshold=0.95):
    """Compare current performance against baseline."""
    with open(baseline_file) as f:
        baseline = json.load(f)
    with open(current_file) as f:
        current = json.load(f)
    
    failed = False
    for workload in baseline:
        if workload not in current:
            print(f"⚠️  Missing workload in current results: {workload}")
            continue
        
        baseline_speedup = baseline[workload]["speedup_mean"]
        current_speedup = current[workload]["speedup_mean"]
        ratio = current_speedup / baseline_speedup
        
        status = "✅" if ratio >= threshold else "❌"
        print(f"{status} {workload}: {current_speedup:.2f}x "
              f"(baseline: {baseline_speedup:.2f}x, ratio: {ratio:.2%})")
        
        if ratio < threshold:
            failed = True
    
    if failed:
        print(f"\n❌ Performance regression detected (threshold: {threshold:.0%})")
        sys.exit(1)
    else:
        print(f"\n✅ All benchmarks passed (threshold: {threshold:.0%})")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--current", required=True)
    parser.add_argument("--threshold", type=float, default=0.95)
    args = parser.parse_args()
    
    compare_performance(args.baseline, args.current, args.threshold)
```

### 17.5 Manual Performance Testing

For developers without CI access:

**Step 1: Build with profiling support**
```bash
cd /Users/anthony/projects/code/86Box-voodoo-dynarec-v2
mkdir -p build-bench && cd build-bench

cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_VOODOO_BENCH=ON

make -j$(sysctl -n hw.ncpu)
```

**Step 2: Run Quake timedemo**
```bash
# Start 86Box
./86Box.app/Contents/MacOS/86Box --config voodoo_test.cfg

# In Quake console (once booted):
timedemo demo1

# Check logs for performance data
tail -f 86box.log | grep -E "Voodoo|FPS|dynarec"
```

**Step 3: Capture macOS xctrace profile**
```bash
# Profile with Time Profiler
instruments -t "Time Profiler" \
  -D voodoo_trace.trace \
  ./86Box.app/Contents/MacOS/86Box

# Analyze hotspots
open voodoo_trace.trace
# Look for: voodoo_render_scanline, generated code blocks
```

**Step 4: Compare interpreter vs dynarec**
```bash
# Interpreter baseline
VOODOO_DYNAREC=0 ./86Box --benchmark fillrate > interp.txt

# Dynarec test
VOODOO_DYNAREC=1 ./86Box --benchmark fillrate > dynarec.txt

# Compare
diff -y interp.txt dynarec.txt
```

### 17.6 Expected Results

Based on x86-64 dynarec performance, ARM64 targets:

| Workload | Resolution | Interpreter FPS | Dynarec FPS (target) | Speedup |
|:---------|:-----------|:----------------|:---------------------|:--------|
| GLQuake timedemo | 640×480 | ~15 FPS | 45-60 FPS | 3-4x |
| 3DMark 99 Nature (low) | 640×480 | ~8 FPS | 25-30 FPS | 3-3.5x |
| Fillrate test (textured) | 640×480 | ~5 Mpix/s | 15-20 Mpix/s | 3-4x |
| Fillrate test (untextured) | 640×480 | ~10 Mpix/s | 30-40 Mpix/s | 3-4x |

**Regression criteria**:
- Speedup must be ≥2.5x (minimum acceptable)
- Target is 3-4x (matching x86-64 dynarec)
- No rendering artifacts or crashes
- Compile time <100ms for typical scene

---

## Reference Links

- [ARM64 ISA Documentation](https://developer.arm.com/documentation/ddi0487/latest/)
- [ARM NEON Programming Guide](https://developer.arm.com/documentation/den0018/latest/)
- [Apple Silicon JIT Memory](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon)
- [3dfx Voodoo Hardware Reference](http://voodoo.mirrors.sk/files/voodoo12v1.1.pdf)

## Companion Documents

This guide is supplemented by three companion documents for specific topics:

### 1. [WORKED_EXAMPLE_PMULLW.md](file:///Users/anthony/projects/code/86Box-voodoo-dynarec-v2/porting_docs/WORKED_EXAMPLE_PMULLW.md)

**Complete walkthrough of porting a single SIMD operation**

Step-by-step example showing how to port the `PMULLW` (multiply low word) instruction from x86-64 SSE2 to ARM64 NEON, including:
- Understanding the x86-64 operation semantics
- Finding the ARM64 NEON equivalent
- Encoding the instruction with register fields
- Verifying bytecode with LLDB disassembly
- Testing with known input vectors
- Integration checklist

**When to read**: After understanding Part 7 (SSE2 to NEON Translation), use this as a template for porting each SIMD operation.

### 2. [ARM64_COMMON_PITFALLS.md](file:///Users/anthony/projects/code/86Box-voodoo-dynarec-v2/porting_docs/ARM64_COMMON_PITFALLS.md)

**ARM64-specific gotchas when porting from x86-64**

Covers 10 common issues:
1. Immediate value encoding restrictions (12-bit limits)
2. Load/store offset scaling requirements
3. Condition code differences (JA→B.HI, etc.)
4. NEON lane indexing vs SSE element selection
5. Stack alignment (16-byte requirement)
6. Register clobbering (X16/X17 scratch registers)
7. NEON vs SSE saturation differences
8. Shift amount encoding per element size
9. Branch range limitations (±1MB for conditional)
10. W vs X register confusion

**When to read**: Keep this open while implementing. Consult when encountering SIGBUS, SIGILL, or unexpected results.

### 3. [PERFORMANCE_OPTIMIZATION.md](file:///Users/anthony/projects/code/86Box-voodoo-dynarec-v2/porting_docs/PERFORMANCE_OPTIMIZATION.md)

**Post-correctness performance tuning guide**

Optimization techniques after achieving pixel-perfect correctness:
- Profiling with Instruments
- Branch prediction hints
- NEON instruction scheduling
- Cache-friendly data layout
- JIT compilation overhead reduction
- NEON-specific optimizations
- Parallelization strategies
- Code size optimization
- Benchmarking methodology

**When to read**: Only after Part 13.5 (Definition of Done) correctness criteria are met. Never optimize before output is pixel-perfect.

---

## Changelog Maintenance

**IMPORTANT**: Maintain a detailed changelog throughout the port development.

All progress, bugs, performance metrics, and deviations from the plan should be recorded in [port_changelog.md](file:///Users/anthony/projects/code/86Box-voodoo-dynarec-v2/porting_docs/port_changelog.md).

**Update the changelog**:
- After completing each phase (Part 12.1)
- When encountering significant bugs
- After achieving performance milestones
- When making scope or timeline changes
- Daily during active development

The changelog serves as:
1. Progress tracker for the AI agent
2. Documentation of decisions and tradeoffs
3. Performance history
4. Bug log and resolution notes
5. Final project report

---
