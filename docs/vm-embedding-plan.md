# Plan: an embeddable VIG VM (library + standalone executable)

Status: agreed; in progress. Phase 0 is up for review as
[TheBozzz34/vig#1](https://github.com/TheBozzz34/vig/pull/1).

Decisions (settled):

- **AGM2 is removed.** It was a leftover from a cancelled feature (Phase 0).
- **The library lives in `vig`.**
- **The IDE supports both** building the library from source with the pinned
  Zig, and a prebuilt library from CI.
- **Foreign calls are allowed by default** in IDE runs, with no prompt.
- **VIG64 comes first.** VIG64 is refactored, exposed and debugged before
  VIG32. VIG32 follows the same pattern.

## Goal

Build the VIG VM, assembler and linker as a library the IDE links directly,
from the same Zig sources that build the `vig`, `vigasm` and `vigld`
executables, so the IDE can:

- assemble and link in-process, and put errors on the right source line,
- run a program without blocking the UI, with output streamed to the Output
  panel and stdin supplied from the IDE,
- pause, step (into/over/out), continue, and stop at breakpoints,
- show live VM state: registers, operand stack, call stack, memory, and a
  disassembly view when there's no source.

**Non-goals for this plan:** changing the instruction set, the container format
or VM semantics; embedding vigcc (lcc, written in C). The same pattern can
cover it later.

**Hard constraints:**

- The `vig`, `vigasm` and `vigld` command-line tools behave exactly as they do
  today.
- The fast interpreter loop gets no slower. Debugging features are compiled
  in only where they're used.
- Both builds keep working: `zig build` and Bazel (`rules_zig`).

## What's there today

Findings from `vig` @ `e7d5d83`, `vig-assembler` @ `cc7d4a0`, `vig-bytecode` @ `2b3ee18`:

| Area | State | Consequence |
| --- | --- | --- |
| `vig/src/machine.zig` `VM.run()` | One monolithic `while (ip < code_len) switch (op)` loop; VIG64 has a second loop, `runVig64()`. Traps are Zig errors returned from inside the switch. | No way to pause, single-step or stop at a breakpoint. |
| VM I/O | Output is an injected `*Io.Writer`, input an injected `*Io.Reader`. Only **one** direct stderr write (`std.debug.print` for an invalid opcode, line ~394). | The core is already almost host-agnostic. |
| Input ops | `read_i32` / `read_byte` block on the reader (`readI32`, `takeByte`). | An embedded host needs "waiting for input" instead of blocking. |
| Trap location | `ip` has already moved past the opcode when a trap fires; verifier failures record an offset (`verification_failure`). | Runtime traps don't report which instruction faulted. |
| `vig/src/utils.zig` `loadProgramFromFile` | **Doesn't compile.** The latest commit left a half-written AGM2 loader (a cancelled feature). | Fixed by Phase 0 ([vig#1](https://github.com/TheBozzz34/vig/pull/1)). |
| VIG64 build path | `vigasm` only emits VIG64 as a relocatable object (`-c --vig64`); a runnable VIG64 program always comes from `vigld --vig64` (entry symbol `_start` by default). | Source-level VIG64 debugging needs the line table to survive linking. The library includes the linker. |
| Assembler errors | `Diagnostics` holds only `verification: ?verify.Failure`. Parse errors surface as a bare error name (`Assembly failed: UnknownInstruction`), with no line or column. | The IDE can't mark the offending line. |
| Source ↔ code mapping | No line table or symbol export. The two assembler passes know both the line and the code offset, but throw that away. | Breakpoints and stepping by source line need this added. |
| Build | `build.zig` builds `vig` plus vendored libffi 3.5.2. `BUILD.bazel` already loads `zig_static_library` and `zig_c_library`. | A static library target fits both build systems. |
| Toolchain | Pinned Zig nightly `0.17.0-dev.1543+6db520a4c`. It's no longer on `ziglang.org/builds/` (404, pruned), but community mirrors serve it with the SHA-256 in `vig-bytecode/zig-index.json`. | The IDE build needs that exact Zig, or prebuilt artifacts. Toolchain download should use mirrors and verify the hash. |

## Target architecture

```
                    ┌────────────────────────── vig repo ─────────────────────────┐
 vig-bytecode ─────▶│ core:  machine.zig (VM, execute loop)  foreign.zig  loader  │
 (opcodes, format,  │           ▲                         ▲                      │
  verifier, disasm) │ Zig API:  src/vig.zig  (Vm, Session, StopReason, ...)      │
                    │           ▲                         ▲                      │
 vig-assembler ────▶│ C ABI:    src/c_api.zig ── include/vig.h                    │
 vig-linker ───────▶│           (VM + assembler + linker + disassembler)         │
 (+ debug info)     │  targets: vig (exe, uses Zig API)   libvig.a (static lib)   │
                    └───────────┼────────────────────────────────────────────────┘
                                ▼
                       vigidec (C++) links libvig.a + libffi.a
```

- **One Zig core, three layers.** The CLI calls the Zig API directly. The C ABI
  is a thin wrapper over the same Zig API. Nothing in the core knows which one
  is calling.
- **One static library for everything the IDE needs.** The VM, assembler and
  linker C APIs go into a single `libvig.a`, built from one Zig root in `vig`.
  Two separately built Zig static libraries each carry their own copy of the
  Zig runtime and `vig_bytecode`, and linking both into one executable risks
  duplicate-symbol errors. `vig` already depends on `vig_assembler` (for its
  tests); it gains a path dependency on `vig_linker` (`../vig-linker`, and the
  matching Bazel module dependency).

## Phase 0 — make `vig` build again (done, in review)

[vig#1](https://github.com/TheBozzz34/vig/pull/1) restores `src/utils.zig` to
the pre-AGM2 loader (aca252d). `zig build` passes, and `zig build test` gives
96 passed, 3 skipped, 0 failed.

Format detection moves out of the file-reading function in Phase 2, into
`loader.loadBytes(vm, bytes)` (container or raw VIG32 bytecode). The CLI reads
the file and calls it; the library calls it with the bytes the IDE hands over.

## Phase 1 — assembler and linker: locations and debug info

No behaviour change for valid programs, and no file format change. VIG64
object assembly (`assembleVig64Object`) and `linkVig64` are done first.

### 1a. `vig-assembler`

1. **Located diagnostics.** Extend `Diagnostics`:
   ```zig
   pub const Diagnostics = struct {
       verification: ?verify.Failure = null,
       /// Where assembly stopped, if it failed before producing a program.
       location: ?struct { line: u32, column: u32 } = null,
       /// Human-readable text for the error (static strings, no allocation).
       message: []const u8 = "",
   };
   ```
   This covers all three entry points: programs, VIG32 objects and VIG64
   objects. Both passes iterate `lines.next()`. Keep a 1-based line counter in each, and
   on the error path (`catch |err| { diag.location = ...; return err; }`) record
   the line, plus the column of the offending token where the parser knows it.
   `message` comes from a table mapping error names to text ("unknown
   instruction 'pussh'" style where the token is at hand). The CLI prints
   `file:line:col: error: message`.
2. **Debug info from the emission pass.** An optional out-parameter. For an
   object, offsets are relative to that object's code section:
   ```zig
   pub const DebugInfo = struct {
       /// One entry per emitted instruction, sorted by offset.
       lines: []LineEntry,   // .{ .offset: u64, .line: u32 }
       /// Labels with their section and offset (code/data/bss).
       symbols: []Symbol,    // .{ .name, .section, .offset }
       pub fn deinit(self: *DebugInfo, gpa: Allocator) void;
   };
   pub fn assembleVig64ObjectWithDebugInfo(gpa, source, *Diagnostics, *DebugInfo) ![]u8;
   pub fn assembleObjectWithDebugInfo(gpa, source, *Diagnostics, *DebugInfo) ![]u8;
   pub fn assembleWithDebugInfo(gpa, source, options, *Diagnostics, *DebugInfo) ![]u8;
   ```
   Verification failures are reported as a code offset. With the line table
   they map to a source line, so the IDE can point at a verifier error too.
3. **Tests:** a failing program per diagnostic path asserts `line`/`column`;
   every example asserts the line table is sorted, covers every instruction
   offset, and that the symbols match the labels.

### 1b. `vig-linker`

1. **Carry debug info through the link.** `Input` gains an optional
   `debug: ?*const DebugInfo`. The linker already computes each object's
   `code_base`, `data_base` and `bss_base`, so it relocates each input's line
   entries and symbols by those bases and merges them into one output
   `DebugInfo`. Each line entry also carries a `file` index (the input's
   position), so a program built from several sources maps each offset back to
   the right file and line. Objects the link drops (unused lazy inputs)
   contribute nothing.
2. **Located diagnostics.** `Diagnostics` already names the object and symbol;
   it gains the same `message` text, so "undefined symbol `foo` referenced
   from main.vigas" reaches the IDE as a sentence. With the object's debug info
   it can also carry a line.
3. **Tests:** link two objects with debug info, and check every final
   instruction offset maps back to the right file and line; lazy objects left
   out contribute no entries.

Debug info inside the container file (a debug section, or a `.vigdbg` sidecar)
is a later, separate decision. The IDE assembles and links in memory, so it
isn't needed now.

## Phase 2 — VM core refactor (`vig`)

### 2.1 One loop, two compiled modes

Replace the bodies of `run()` / `runVig64()` with a shared, comptime-specialised
loop. The instruction switch stays exactly as it is, so the semantics can't drift:

```zig
pub const Mode = enum { fast, debug };

pub const StopReason = union(enum) {
    halted,
    breakpoint,           // ip is on a breakpointed instruction, not yet executed
    budget_exhausted,     // ran `budget` instructions
    need_input,           // read_i32/read_byte found no input; ip rewound to the op
    trap: Trap,           // .{ .err: anyerror, .offset: u64 } — offset of the faulting op
};

fn execute(self: *VM, comptime mode: Mode, budget: u64) StopReason { ... }

pub fn run(self: *VM) !void {               // unchanged contract for the CLI
    switch (self.execute(.fast, std.math.maxInt(u64))) {
        .halted => {},
        .trap => |t| return t.err,
        else => unreachable,
    }
}
```

- `mode == .fast` compiles to today's loop: no budget counter, no breakpoint
  check, no input gate. `comptime` guarantees those branches vanish.
- `mode == .debug` adds, at the top of each iteration:
  `if (breakpoints.isSet(ip) and !first_iteration) return .breakpoint;` and
  `if (budget == 0) return .budget_exhausted; budget -= 1;`. Breakpoints are a
  bit set over the code region (one bit per byte of code), allocated only when
  one is set.
- **Trap offset:** record `const start = self.ip;` before decoding, and turn
  errors into `.trap = .{ .err, .offset = start }`. The invalid-opcode
  `std.debug.print` is removed; the CLI prints the message from the trap it
  receives (same text as now).
- **Input gate (debug only):** before `read_i32` / `read_byte`, ask the host
  input whether a whole token or byte is available. If not, set `ip = start`
  and return `.need_input`. The instruction re-executes cleanly when input
  arrives, because nothing has been popped or pushed yet. In fast mode, and for
  the CLI, reads block as today.
- **VIG64 first.** `runVig64()` is converted first, to
  `executeVig64(comptime mode, budget)`, and everything in this phase is built
  and tested against it before the VIG32 `run()` loop gets the same treatment
  (same PR or a follow-up). The two loops stay separate, as they are today.

### 2.2 Stepping helpers (Zig API)

Built on `execute(.debug, …)` and the call stack depth (`csp`):

- `stepInstruction()` → `execute(.debug, 1)`
- `stepOver()`: if the next op is `call` / `call_indirect` / `foreign_call`,
  run until `csp` returns to its current depth (or any other stop); otherwise
  step one instruction.
- `stepOut()`: run until `csp < current depth`.

Source-line stepping (step until the line-table entry changes) lives in the IDE,
which has the line table.

### 2.3 State inspection

Read-only accessors, so the C layer never reaches into `VM` fields:
`registers()` (abi, ip, sp, csp, frame_pointer, code_len, program_len,
memory size), `operandStack()` (raw `u64` slots for VIG64, `i32` for VIG32,
bottom-first; the caller picks signed, unsigned or hex display), the foreign
import table (library, symbol and signature, VIG64 first),
`callFrames()` (return ip, frame base, arguments, locals), and
`readMemory(addr, out)` with bounds checks.

### 2.4 Guardrails

- **Equivalence test:** every example (VIG64 ones first, built through the
  object + link path) runs once with `run()`, and again by
  looping `execute(.debug, 1)` until halted. Output, final registers and
  memory must match. The same with breakpoints on every instruction.
- **Performance:** `bazelisk run //:bench -- loop` (and `vig --stats`) before
  and after. The acceptance bar is no measurable regression in the fast path.
- The full existing test suite passes untouched.

## Phase 3 — library surface (`vig`)

### 3.1 Zig API (`src/vig.zig`)

A `Session` owns a `VM`, its allocator, a growable output buffer (an
`Io.Writer.Allocating`), a host input buffer (bytes plus a closed flag, backing
the `Io.Reader` and the input gate), breakpoints, and the loaded program bytes
(for restart). The CLI keeps using `VM` directly with stdio. The C API wraps
`Session`.

### 3.2 C API (`include/vig.h`, `src/c_api.zig`)

A hand-written header, kept honest by a C test compiled against it in
`zig build test`. `-femit-h` isn't reliable on nightlies. Sketch:

```c
#define VIG_API_VERSION 1
uint32_t vig_api_version(void);

typedef struct vig_session vig_session;

typedef enum { VIG_OK = 0, VIG_ERR_NO_MEMORY, VIG_ERR_INVALID_ARGUMENT,
               VIG_ERR_BAD_PROGRAM, VIG_ERR_VERIFY, VIG_ERR_FOREIGN,
               VIG_ERR_STATE } vig_status;

typedef enum { VIG_STOP_HALTED, VIG_STOP_BREAKPOINT, VIG_STOP_BUDGET,
               VIG_STOP_NEED_INPUT, VIG_STOP_TRAP } vig_stop_reason;

typedef struct { vig_stop_reason reason; uint64_t offset;   /* trap/breakpoint */
                 int32_t trap_code; const char *trap_name; } vig_stop;

typedef struct { size_t memory_size, stack_size, call_stack_size; } vig_config;

vig_status  vig_session_create(const vig_config *cfg, vig_session **out);
void        vig_session_destroy(vig_session *s);
vig_status  vig_session_load(vig_session *s, const uint8_t *bytes, size_t len);
vig_status  vig_session_restart(vig_session *s);        /* reload the same bytes */
const char *vig_session_last_error(const vig_session *s);/* incl. verifier offset */

vig_stop    vig_session_run(vig_session *s, uint64_t max_instructions);
vig_stop    vig_session_step(vig_session *s);
vig_stop    vig_session_step_over(vig_session *s, uint64_t max_instructions);
vig_stop    vig_session_step_out(vig_session *s, uint64_t max_instructions);
vig_status  vig_session_set_breakpoint(vig_session *s, uint64_t offset, bool on);
void        vig_session_clear_breakpoints(vig_session *s);

size_t      vig_session_read_output(vig_session *s, char *buf, size_t cap); /* drains */
vig_status  vig_session_write_input(vig_session *s, const char *buf, size_t len);
void        vig_session_close_input(vig_session *s);                        /* EOF */

typedef struct { uint8_t abi; uint64_t ip, sp, csp, frame_pointer,
                 code_len, program_len, memory_size; } vig_registers;
void        vig_session_registers(const vig_session *s, vig_registers *out);
size_t      vig_session_stack(const vig_session *s, uint64_t *out, size_t cap); /* raw slots */
size_t      vig_session_call_stack(const vig_session *s, vig_frame *out, size_t cap);
vig_status  vig_session_read_memory(const vig_session *s, uint64_t addr,
                                    uint8_t *out, size_t len);

/* Assembler and linker. Both produce a vig_build_result, which exists even on
   failure (it carries the diagnostics). */
typedef struct { uint32_t file, line, column; int32_t code; const char *message; } vig_diagnostic;
typedef struct { uint64_t offset; uint32_t file, line; } vig_line_entry;
typedef struct { const char *name; uint8_t section; uint64_t offset; } vig_symbol;
typedef struct vig_build_result vig_build_result;

enum { VIG_ASM_OBJECT = 1u << 0, VIG_ASM_VIG64 = 1u << 1, VIG_ASM_CHECK_STACK = 1u << 2 };
vig_status  vig_assemble(const char *source, size_t len, uint32_t flags,
                         vig_build_result **out);   /* program or object */

typedef struct { const char *name; const vig_build_result *object; bool lazy; } vig_link_input;
vig_status  vig_link(const vig_link_input *inputs, size_t count, bool vig64,
                     const char *entry_symbol /* NULL = "_start" */,
                     vig_build_result **out);   /* relocates and merges debug info */

bool        vig_build_ok(const vig_build_result *r);
void        vig_build_bytes(const vig_build_result *r, const uint8_t **bytes, size_t *len);
size_t      vig_build_diagnostics(const vig_build_result *r, const vig_diagnostic **out);
size_t      vig_build_lines(const vig_build_result *r, const vig_line_entry **out);
size_t      vig_build_symbols(const vig_build_result *r, const vig_symbol **out);
void        vig_build_result_free(vig_build_result *r);

/* Disassembly (vig-bytecode's disasm), for programs without source */
vig_status  vig_disassemble(const uint8_t *program, size_t len, char **text, size_t *text_len);
void        vig_free(void *p);
```

Design rules for the C layer:

- **Stable codes.** Zig error values aren't stable across builds, so traps and
  statuses map through explicit tables (`error.StackUnderflow →
  VIG_TRAP_STACK_UNDERFLOW`), with the error name also available as a string.
- **No callbacks.** Output is drained by polling and input is pushed. No
  re-entrancy, no threading assumptions; the host calls in from one thread.
- **Allocator:** `std.heap.c_allocator`, since libc is linked anyway for the
  foreign-call loader. Everything the library allocates is freed by the library.
- **No global state.** Multiple sessions can coexist.
- **ABI versioning:** `vig_api_version()` is checked by the IDE at startup.

### 3.3 Build targets

- **`build.zig`:** a `lib` step that builds `libvig.a` from `src/c_api.zig`
  (importing `vig_bytecode`, `vig_assembler` and `vig_linker`), links libc, and installs it
  with `include/vig.h`. libffi remains its own archive (`libffi.a`), installed
  alongside, because Zig doesn't merge linked static libraries into another
  archive. Consumers link both.
- **`BUILD.bazel`:** a `zig_static_library(name = "libvig", …)` with the same
  root, plus a `cc_library` exposing the header, so Bazel C/C++ consumers work
  too. vig-meta gets an alias, `//:libvig`.
- **CI artifacts (in `vig`):** the GitHub Actions workflow builds `libvig` for
  Windows (MSVC ABI), Linux x86_64 and macOS, and uploads
  `libvig-<target>.zip` (lib, libffi, header, version file) on each release
  tag. This is the IDE's prebuilt option.
- **Target ABI must match the consumer:** `x86_64-windows-msvc` for an MSVC IDE
  build, `-gnu` for MinGW, the host triple on Linux and macOS. The IDE passes
  `-Dtarget` explicitly.

## Phase 4 — IDE integration (`vigidec`)

1. **CMake:** `cmake/Vig.cmake` provides an imported target, `vig::vig`, in one
   of two ways:
   - **From source:** `VIGIDE_VIG_SOURCE_DIR` (a checkout of `vig` with its
     siblings, e.g. a vig-meta submodule). Find `zig` (`VIGIDE_ZIG`, default
     from `PATH`) and check it's the pinned version. Optionally download it
     from the community mirrors, verifying the SHA-256 from
     `vig-bytecode/zig-index.json`. Then run `zig build lib
     -Doptimize=ReleaseSafe -Dtarget=… --prefix <build>/vig` as a custom
     command.
   - **Prebuilt:** `VIGIDE_VIG_PREBUILT_DIR`, or `VIGIDE_VIG_VERSION` to fetch
     the matching CI artifact, for contributors without the Zig nightly.
   - Both give the same imported target, and the IDE checks
     `vig_api_version()` at startup.
2. **`VmSession` (C++):** an RAII wrapper over `vig_session`. It runs the VM in
   time slices from the main loop (for example, instruction budgets tuned to
   about 2 ms per frame), so there are no threads and the UI stays responsive.
   Each frame it drains output into the Output panel.
3. **Build:** in-process, with no temporary files. The default target is
   VIG64: `vig_assemble(…, VIG_ASM_OBJECT | VIG_ASM_VIG64)` for each source
   in the project, then `vig_link(…, vig64 = true)`, with the entry symbol
   `_start` (templates start with it). A per-project setting switches to
   VIG32 direct assembly. Diagnostics go to a Problems list in the Output panel and
   to gutter and underline markers in the editor; clicking one jumps to it.
   Verifier offsets map through the line table.
4. **Run / Stop / Restart**, with an input field that appears on
   `VIG_STOP_NEED_INPUT` (plus EOF). Foreign calls are allowed with no prompt.
5. **Debugging:** breakpoints toggled in a gutter margin (and F9), mapped from
   line to offset through the line table. Continue (F5), Step Into (F11), Step
   Over (F10), Step Out (Shift+F11). The paused line is highlighted.
6. **VM panel:** registers, the operand stack (VIG64 `u64` slots, shown as
   signed, unsigned or hex), resolved foreign imports, the call stack (return addresses
   shown as `label+off` through the symbols, and as lines), a memory viewer
   over static data and BSS, and a disassembly tab.
7. **C sources:** vigcc and the linker stay out of process for now. Their
   output can still be run and stepped at the instruction level.

## Order of PRs

| # | Repo | PR | Depends on |
| --- | --- | --- | --- |
| 0 | vig | Remove the unfinished AGM2 loader ([#1](https://github.com/TheBozzz34/vig/pull/1)) | — |
| 1a | vig-assembler | Located diagnostics, `file:line:col` CLI errors, `DebugInfo` (VIG64 objects first) | — |
| 1b | vig-linker | Relocate and merge `DebugInfo` through `linkVig64` (then `link`), diagnostic messages | 1a |
| 2 | vig | `executeVig64(comptime mode, budget)` first, then VIG32; stop reasons, trap offsets, input gate, step helpers, state accessors; equivalence tests and benchmark; `loader.loadBytes` | 0 |
| 3 | vig | `Session` Zig API, `c_api.zig` + `include/vig.h` (VM, assembler, linker, disasm), `lib` targets in `build.zig` and Bazel, C header test, CI artifacts | 1b, 2 |
| 4 | vig-meta | Bump submodules, alias `//:libvig`, Zig mirrors for the toolchain | 3 |
| 5 | vigidec | `Vig.cmake` (source and prebuilt), `VmSession`, VIG64 build with diagnostics, Run/Stop/input | 3 |
| 6 | vigidec | Breakpoints, stepping, VM panel, disassembly view | 5 |

PRs 1a and 2 are independent and can proceed in parallel. Each PR keeps the CLI
tools and all existing tests green.

## Still open

- Whether debug info should eventually be stored in `.vig`/`.vigo` files (a
  section or a sidecar), for debugging programs built outside the IDE, e.g. by
  vigcc.
- Whether vig-meta should switch to a newer Zig pin now that the current one
  has been pruned from ziglang.org.

## Building the Zig side

The pinned Zig can be fetched from a community mirror and checked against the
SHA-256 in `vig-bytecode/zig-index.json`. In this cloud environment, Zig's own
HTTP client can't get through the proxy, so the libffi dependency is
downloaded with curl and unpacked into `vig/zig-pkg/<hash>` ahead of time.
With that, `zig build` and `zig build test` run normally here.
