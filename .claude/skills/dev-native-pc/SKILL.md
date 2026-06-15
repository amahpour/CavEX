---
name: dev-native-pc
description: Build and run CavEX natively on Linux (GLFW/OpenGL) for fast iteration on game logic — with live asserts, gdb backtraces, ASan, and an autonomous verify rig (CAVEX_AUTOSHOT / CAVEX_AUTOPLAY env vars). Use when developing/debugging world, NBT, lighting, mesher, or gameplay code, when the user says "run it natively/locally/PC build", or when a crash needs a real backtrace. NOT for Wii-platform issues (heap pressure, GX, WPAD) — those don't reproduce natively; use run-wii-dolphin.
---

# Native PC dev build

## Purpose

Seconds-long edit→run loop with real debugging tools. Found-in-minutes here
vs hours of frame forensics on the Wii side. Game logic is shared, so fixes
carry back to the `.dol`.

## Steps

1. **Build** (Debug keeps asserts ALIVE — upstream guards malloc/file-read
   failures with `assert`, so NDEBUG builds turn failures into NULL-deref
   crashes; this exact class caused both the Wii NULL-write storm and the PC
   shader crash):

   ```bash
   cd ~/code/CavEX/build_pc
   cmake .. -DCMAKE_BUILD_TYPE=Debug   # only needed once / after CMakeLists changes
   make -j"$(nproc)"
   ```

2. **Run** — must be from the staged run dir (assets symlink, PC config,
   saves):

   ```bash
   cd ~/code/CavEX/build_pc/run && ../cavex
   ```

   If the run dir is missing, recreate: `ln -sfn ../../assets assets`,
   `cp ../../config_pc.json config.json`, copy a world into `saves/`
   (see `gen-beta-world`). Shaders load from `assets/` (already there),
   NOT from cwd. Controls: native WASD + true mouselook, LMB/RMB, Space,
   Left-Shift sneak, E inventory, F2 screenshot, Enter menu.

3. **Autonomous verify** (agent eyes + hands, no screenshot tool or input
   injection needed):

   ```bash
   CAVEX_AUTOPLAY=1 CAVEX_AUTOSHOT=120 timeout 45 ../cavex
   # AUTOPLAY: auto-enters first world ~4s after the menu appears
   # AUTOSHOT=N: dumps framebuffer to ./autoshot_NNNNNN.png every N frames
   # exit 124 = survived; Read the autoshot PNGs to see what rendered
   ```

4. **Crash? gdb one-liner** (binary has symbols):

   ```bash
   CAVEX_AUTOPLAY=1 timeout 40 gdb -batch -ex 'set debuginfod enabled off' \
     -ex run -ex 'bt 15' --args ../cavex
   ```

5. **Memory bugs / second opinion — ASan build** (`build_asan/`, same run dir):

   ```bash
   cd ~/code/CavEX/build_asan && make -j"$(nproc)"
   cd ../build_pc/run && CAVEX_AUTOPLAY=1 timeout 45 ../../build_asan/cavex
   ```

   **Interpretation rule learned the hard way:** ASan clean + normal build
   crashes ⇒ suspect *alignment*, not corruption (ASan's allocator
   over-aligns; it masked the `-march=native` AVX-store crash).

## Gotchas

- Never reintroduce `-march=native` (CMakeLists now `-O2 -g`): AVX 32-byte
  aligned stores into plain-malloc'd structs (`struct chunk` leads with a
  cglm `mat4`) segfault on world entry. The flag line applies to ALL build
  types, so even "Debug" was -O3-native before.
- GLFW must use X11 (`glfwInitHint` patch in `pc/gfx.c`): Wayland GLFW +
  distro GLEW (GLX-only) = "Could not load extended OpenGL functions!" then
  segfault on the first extension call.
- Empty log after a crash ≠ no output — stdout was block-buffered and lost;
  rerun in foreground or under gdb.
- PC and Wii keep **separate saves** (`build_pc/run/saves/` vs the SD image).
