# LuaJIT Internals: Fast Functions, Optimizations, and Best Practices

This document provides a comprehensive analysis of how LuaJIT works internally, focusing on fast functions, JIT optimizations, and patterns for writing efficient Lua code.

## Table of Contents
1. [Fast Functions](#1-fast-functions)
2. [JIT Compiler Optimizations](#2-jit-compiler-optimizations)
3. [Writing Efficient Lua for LuaJIT](#3-writing-efficient-lua-for-luajit)

---

## 1. Fast Functions

### 1.1 What Are Fast Functions?

Fast functions are LuaJIT's mechanism for implementing builtin library functions (like `math.abs`, `assert`, `tonumber`) with maximum performance. They have **two implementations**:

1. **Fast path**: Hand-written assembly code inlined directly in the bytecode interpreter
2. **Fallback handler**: C function for complex cases (type coercion, error handling)

The key insight is that common cases (e.g., `math.abs` on a number) execute entirely in assembly without any C function call overhead.

### 1.2 Implementation Architecture

#### Key Files
- `src/lib_*.c` - Fast function definitions (lib_base.c, lib_math.c, lib_string.c, etc.)
- `src/vm_x86.dasc` - x86/x64 assembly fast paths (vm_arm.dasc, vm_mips.dasc for other architectures)
- `src/lj_lib.h` - Macro definitions for declaring fast functions
- `src/lj_lib.c` - Registration and initialization
- `src/lj_ffrecord.c` - JIT recording handlers for fast functions
- `src/host/buildvm_lib.c` - Build-time code generation

#### Fast Function IDs (FFID)
Each fast function gets a unique numeric ID:
```c
// From lj_ff.h (generated)
enum {
  FF_LUA = 0,      // Regular Lua function
  FF_C = 1,        // Regular C function
  FF_assert,       // Fast functions start here
  FF_type,
  FF_next,
  FF_pairs,
  FF_tonumber,
  FF_tostring,
  // ... more fast functions
};
```

The FFID is stored in the function's `ffid` field and determines which assembly fast path to execute.

### 1.3 Defining a Fast Function

Fast functions are defined in `lib_*.c` files using macros:

```c
// Example from lib_base.c
LJLIB_ASM(assert)       LJLIB_REC(.)
{
  // This C code is the FALLBACK handler
  // Only runs if assembly fast path can't handle the call
  lj_lib_checkany(L, 1);
  if (L->top == L->base+1)
    lj_err_caller(L, LJ_ERR_ASSERT);
  // Error handling code...
  return FFH_UNREACHABLE;
}

LJLIB_ASM(tonumber)     LJLIB_REC(.)
{
  // Fallback for complex cases (base conversion, etc.)
  int32_t base = lj_lib_optint(L, 2, 10);
  if (base == 10) {
    TValue *o = lj_lib_checkany(L, 1);
    if (lj_strscan_number(strV(o), o)) {
      copyTV(L, L->base-1-LJ_FR2, o);
      return FFH_RES(1);
    }
  }
  // ... more conversion code
  return FFH_RES(0);  // Return nil on failure
}
```

#### Macro Meanings
- `LJLIB_ASM(name)` - Declares a fast function with assembly fast path
- `LJLIB_CF(name)` - Declares a regular C function (no assembly fast path)
- `LJLIB_REC(.)` - Function has a JIT recording handler
- `LJLIB_NOREG` - Don't auto-register in library table
- `LJLIB_PUSH(value)` - Push constant during registration

#### Return Values from Fallback Handler
- `FFH_UNREACHABLE` - Code path should never be reached (throws error)
- `FFH_RETRY` (0) - Retry the fast path
- `FFH_RES(n)` - Return n results
- `FFH_TAILCALL` (-1) - Perform a tail call

### 1.4 Assembly Fast Path Implementation

The assembly fast paths are in `vm_*.dasc` files using DynASM syntax. Example from `vm_x86.dasc`:

```asm
// math.abs - single argument numeric function
.ffunc_1 math_abs
|  cmp dword [BASE+4], LJ_TISNUM    // Check if arg is number
|  jae ->fff_fallback               // If not, use C fallback
|  movsd xmm0, qword [BASE]         // Load the number
|  andps xmm0, xmm1                 // Clear sign bit (absolute value)
|  jmp ->fff_resxmm0                // Return result in xmm0

// assert - check first arg is truthy
.ffunc_1 assert
|  mov RB, [BASE]                   // Load first argument
|  cmp dword [BASE+4], LJ_TISTRUECOND  // Is it truthy?
|  jb ->fff_fallback                // If false/nil, fall back to error
|  mov PC, [BASE-4]                 // Restore PC
|  mov [BASE-8], RB                 // Copy result
|  mov RD, 1+1                      // 1 result
|  jmp ->fff_res                    // Return
```

#### Common Fast Path Patterns
1. **Argument count check**: `cmp NARGS:RD, n+1; jb ->fff_fallback`
2. **Type check**: `cmp dword [BASE+offset], LJ_TTYPE; jae ->fff_fallback`
3. **Operation**: Inline assembly for the actual work
4. **Result return**: Jump to `->fff_res1`, `->fff_resxmm0`, etc.

### 1.5 JIT Recording for Fast Functions

For JIT compilation, fast functions need recording handlers in `lj_ffrecord.c`:

```c
// Recording handler for assert
static void LJ_FASTCALL recff_assert(jit_State *J, RecordFFData *rd)
{
  // Arguments already type-specialized by trace recording
  rd->nres = J->maxslot;  // Pass through all arguments
}

// Recording handler for math.abs
static void LJ_FASTCALL recff_math_abs(jit_State *J, RecordFFData *rd)
{
  TRef tr = lj_ir_tonum(J, J->base[0]);  // Convert to number IR
  J->base[0] = emitir(IRTN(IR_ABS), tr, lj_ir_ksimd(J, LJ_KSIMD_ABS));
  UNUSED(rd);
}
```

Functions without recording handlers use `recff_nyi` which either stitches traces or aborts recording.

### 1.6 How to Implement a New Fast Function

#### Step 1: Add the C Fallback Handler

In the appropriate `lib_*.c` file:

```c
LJLIB_ASM(myfunction)   LJLIB_REC(.)
{
  // Validate arguments
  TValue *o = lj_lib_checkany(L, 1);

  // Handle complex cases that can't be done in assembly
  if (!tvisnum(o)) {
    // Type coercion, error handling, etc.
    return FFH_RETRY;  // Or return actual result
  }

  // This shouldn't be reached if fast path handles common case
  return FFH_UNREACHABLE;
}
```

#### Step 2: Add Assembly Fast Path

In `vm_x86.dasc` (and equivalent for other architectures):

```asm
|.ffunc_1 myfunction              // 1-argument fast function
|  cmp dword [BASE+4], LJ_TISNUM  // Type check
|  jae ->fff_fallback             // Fall back if not number
|  // ... perform operation ...
|  jmp ->fff_res1                 // Return 1 result
```

For multi-architecture support, you need to implement in:
- `vm_x86.dasc` - x86/x64
- `vm_arm.dasc` - 32-bit ARM
- `vm_arm64.dasc` - 64-bit ARM
- `vm_mips.dasc` / `vm_mips64.dasc` - MIPS
- `vm_ppc.dasc` - PowerPC

#### Step 3: Add JIT Recording Handler

In `lj_ffrecord.c`:

```c
static void LJ_FASTCALL recff_myfunction(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];  // Get first argument

  // Emit IR for the operation
  J->base[0] = emitir(IRTN(IR_MYOP), tr, ...);

  UNUSED(rd);
}
```

#### Step 4: Register in Recording Table

Add entry to the recording dispatch table in `lj_ffrecord.c`:

```c
static const RecordFunc recff_func[] = {
  // ... existing entries ...
  recff_myfunction,
};
```

#### Step 5: Update Build System

The build system (`buildvm`) automatically:
1. Parses `LJLIB_ASM` macros
2. Assigns FFIDs
3. Generates `lj_ffdef.h` with function enum
4. Generates bytecode initialization sequences

### 1.7 Fast Function Categories

| Category | Examples | Has Assembly Fast Path |
|----------|----------|----------------------|
| Type checking | `type`, `rawtype` | Yes |
| Assertions | `assert`, `error` | Yes |
| Iteration | `next`, `pairs`, `ipairs` | Yes |
| Type conversion | `tonumber`, `tostring` | Yes |
| Math operations | `math.abs`, `math.sqrt`, `math.sin` | Yes |
| Bit operations | `bit.band`, `bit.bor`, `bit.bnot` | Yes |
| Table operations | `rawget`, `rawset` | Yes |
| String operations | Many have fallbacks only | Mixed |

---

## 2. JIT Compiler Optimizations

### 2.1 Trace Compiler Overview

LuaJIT uses a **trace-based JIT compiler**:

1. **Hot loop detection**: Bytecode loops are counted; when hot (default: 56 iterations), recording starts
2. **Trace recording**: Bytecode is converted to SSA IR (Intermediate Representation)
3. **Optimization**: Multiple passes optimize the IR
4. **Assembly**: IR is compiled to native machine code
5. **Execution**: Hot paths run as native code; guards handle unexpected cases

#### Trace Types
- **Root traces**: Started from hot loops or function calls
- **Side traces**: Branch from existing traces when guards fail frequently
- **Linked traces**: Multiple traces connected together

### 2.2 IR (Intermediate Representation)

LuaJIT's IR is in SSA (Static Single Assignment) form:

```
IR Instruction Format (64 bits):
+-------+-------+---+---+---+---+
|  op1  |  op2  | t | o | r | s |
+-------+-------+---+---+---+---+
  16b     16b    8b  8b  8b  8b

op1/op2: Operand references
t: Type (INT, NUM, STR, TAB, etc.)
o: Opcode
r: Register allocation
s: Spill slot
```

#### Key IR Instructions

**Arithmetic:**
- `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `POW`, `NEG`, `ABS`
- `ADDOV`, `SUBOV`, `MULOV` - with overflow checking

**Memory:**
- `AREF`, `HREF`, `HREFK` - reference computations
- `ALOAD`, `HLOAD`, `SLOAD` - loads
- `ASTORE`, `HSTORE` - stores

**Control:**
- `LT`, `GE`, `LE`, `GT`, `EQ`, `NE` - comparisons (guarded)
- `LOOP` - loop marker
- `PHI` - merge values from different paths

**Allocations:**
- `TNEW`, `TDUP` - table creation
- `SNEW` - string creation

### 2.3 Optimization Passes

LuaJIT applies these optimizations (configurable via `-O` flags):

#### 2.3.1 Constant Folding (FOLD)
**File:** `lj_opt_fold.c` (2,655 lines, 380 rules)

Evaluates operations on constants at compile time:
```lua
-- Before folding
local x = 2 + 3
local y = x * 4

-- After folding
local x = 5
local y = 20
```

Also performs algebraic simplification:
- `x + 0 → x`
- `x * 1 → x`
- `x * 0 → 0`
- `x - x → 0`
- `x * 2 → x << 1` (strength reduction)

#### 2.3.2 Common Subexpression Elimination (CSE)
Reuses previously computed values:
```lua
-- Before CSE
local a = x + y
local b = x + y  -- Same computation

-- After CSE
local a = x + y
local b = a  -- Reuse result
```

#### 2.3.3 Dead Code Elimination (DCE)
**File:** `lj_opt_dce.c`

Removes code that doesn't affect output:
```lua
-- Before DCE
local unused = expensive_computation()
return actual_result

-- After DCE
return actual_result  -- unused computation removed
```

#### 2.3.4 Load/Store Forwarding (FWD)
**File:** `lj_opt_mem.c`

Eliminates redundant loads:
```lua
-- Before forwarding
t[i] = 5
x = t[i]  -- Load from same location

-- After forwarding
t[i] = 5
x = 5  -- Value forwarded, no load needed
```

#### 2.3.5 Dead Store Elimination (DSE)
Removes stores that are overwritten:
```lua
-- Before DSE
t[i] = 1
t[i] = 2  -- Overwrites previous store

-- After DSE
t[i] = 2  -- First store eliminated
```

#### 2.3.6 Loop Optimization (LOOP)
**File:** `lj_opt_loop.c`

Uses copy-substitution with redundancy elimination:
1. **Pre-roll**: First iteration with all guards
2. **Loop body**: Only loop-variant code
3. **PHI nodes**: Handle loop-carried dependencies

Loop-invariant code is automatically hoisted:
```lua
for i = 1, n do
  x = a + b      -- Loop-invariant, hoisted to pre-roll
  t[i] = x + i   -- Loop-variant, stays in body
end
```

#### 2.3.7 Narrowing (NARROW)
**File:** `lj_opt_narrow.c`

Converts floating-point operations to integers when safe:
```lua
-- Lua uses doubles by default
for i = 1, n do
  t[i] = value  -- i narrowed to integer for array indexing
end
```

Three strategies:
1. **Predictive**: Detect integer induction variables in FOR loops
2. **Demand-driven**: Array indices need integers
3. **Backpropagation**: Convert FP ops to INT + overflow checks

#### 2.3.8 Allocation Sinking (SINK)
**File:** `lj_opt_sink.c`

Defers allocations that might not be needed:
```lua
for i = 1, n do
  local t = {}  -- Allocation sunk to side exit
  t[1] = i
  if condition then
    use(t)  -- Only allocate if actually used
  end
end
```

If the object never escapes the trace, allocation is deferred to side exits.

#### 2.3.9 Array Bounds Check Elimination (ABC)
Hoists bounds checks out of loops:
```lua
-- Without ABC
for i = 1, n do
  check_bounds(t, i)  -- Every iteration
  t[i] = value
end

-- With ABC
check_bounds(t, n)  -- Once before loop
for i = 1, n do
  t[i] = value  -- No per-iteration check
end
```

### 2.4 Guard and Side Exit System

#### Guards
Guards are runtime checks that verify JIT assumptions:
- **Type guards**: Verify value types match expectations
- **Value guards**: Verify specific constant values
- **Bounds guards**: Verify array indices are in range

#### Side Exits
When a guard fails:
1. Execution jumps to an exit stub
2. Stack state is restored from a snapshot
3. Control returns to the interpreter
4. If exit is hot, a side trace is recorded

```
Root Trace: for i=1,n do
              |
              v
        [type check: is number?]
              |
        yes   |   no
              v    \--> Side Exit --> Side Trace
        [fast path]     (restore stack, return to interpreter)
```

### 2.5 Optimization Pipeline Order

```
Bytecode Recording
       ↓
[FOLD] Constant Folding (on-the-fly)
       ↓
[CSE]  Common Subexpression Elimination
       ↓
[DCE]  Dead Code Elimination (pre-loop)
       ↓
[LOOP] Loop Optimization
       ↓
[NARROW] Type Narrowing
       ↓
[FWD/DSE] Memory Optimizations
       ↓
[SINK] Allocation Sinking
       ↓
[SPLIT] 64-bit Splitting (32-bit targets)
       ↓
[ASM]  Assembly Generation
```

### 2.6 JIT Parameters

Key tuning parameters (defaults):
```
maxtrace    = 1000   -- Max cached traces
maxrecord   = 4000   -- Max IR instructions per trace
maxside     = 100    -- Max side traces per root
maxsnap     = 500    -- Max snapshots per trace
hotloop     = 56     -- Iterations before loop is hot
hotexit     = 10     -- Side exits before side trace
instunroll  = 4      -- Max unroll for unstable loops
loopunroll  = 15     -- Max unroll for loop ops
callunroll  = 3      -- Max unroll for calls
sizemcode   = 64KB   -- Size of each mcode area
maxmcode    = 2MB    -- Max total mcode size
```

---

## 3. Writing Efficient Lua for LuaJIT

### 3.1 Type Stability

**LuaJIT specializes traces based on observed types.** Consistent types enable better optimization.

```lua
-- BAD: Type changes mid-loop
local x = 0
for i = 1, n do
  if i > threshold then
    x = "string"  -- Type change causes trace exit
  end
  use(x)
end

-- GOOD: Consistent types
local x = 0
for i = 1, n do
  x = x + 1  -- Always number
  use(x)
end
```

**Guidelines:**
- Don't mix integers and floats in tight loops
- Don't change variable types mid-function
- Avoid `nil` in numeric arrays (creates sparse arrays)

### 3.2 Loop Patterns

#### Prefer Numeric FOR Loops
```lua
-- BEST: Numeric FOR loop (directly optimized)
for i = 1, n do
  process(arr[i])
end

-- GOOD: ipairs (optimized for arrays)
for i, v in ipairs(arr) do
  process(v)
end

-- SLOWER: pairs (hash iteration)
for k, v in pairs(tbl) do
  process(v)
end

-- SLOWER: while loop (less optimization opportunity)
local i = 1
while i <= n do
  process(arr[i])
  i = i + 1
end
```

#### Keep Loop Bodies Simple
```lua
-- BAD: Complex conditionals in hot loop
for i = 1, n do
  if complex_condition1 then
    if complex_condition2 then
      -- many branches cause side exits
    end
  end
end

-- GOOD: Simple loop body
for i = 1, n do
  simple_operation(data[i])
end
```

#### Hoist Invariant Computations
```lua
-- Let LuaJIT hoist, but be explicit when helpful
local constant = compute_once()
for i = 1, n do
  result[i] = data[i] * constant
end
```

### 3.3 Table/Array Access

#### Use Integer Indices for Arrays
```lua
-- FAST: Array part access (integer keys 1..n)
local arr = {}
for i = 1, 1000 do
  arr[i] = i * 2  -- Direct array access
end

-- SLOWER: Hash part access (string keys)
local tbl = {}
for i = 1, 1000 do
  tbl["key" .. i] = i * 2  -- Hash lookup required
end
```

#### Pre-allocate Arrays
```lua
-- BAD: Array grows dynamically
local arr = {}
for i = 1, 10000 do
  arr[i] = i  -- May cause multiple reallocations
end

-- GOOD: Use table.new (LuaJIT extension)
local arr = require("table.new")(10000, 0)
for i = 1, 10000 do
  arr[i] = i
end
```

#### Consistent Table Shapes
```lua
-- BAD: Dynamic field addition
local function create_object()
  local obj = {}
  obj.x = 0
  if condition then
    obj.extra = "value"  -- Changes table shape
  end
  return obj
end

-- GOOD: Consistent structure
local function create_object()
  return {
    x = 0,
    extra = nil  -- Always present, even if nil
  }
end
```

#### Use Constant Keys
```lua
-- FAST: Constant string key (HREFK optimization)
local x = t.field
local y = t["constant"]

-- SLOWER: Variable key (full hash lookup)
local key = get_key()
local x = t[key]
```

### 3.4 Function Calls

#### Monomorphic Call Sites
```lua
-- FAST: Always same function (monomorphic)
for i = 1, n do
  math.sin(data[i])  -- Same function every time
end

-- SLOWER: Different functions (polymorphic)
local funcs = {math.sin, math.cos, math.tan}
for i = 1, n do
  funcs[i % 3 + 1](data[i])  -- Different function each time
end
```

#### Use Builtin Functions
Builtin functions have optimized fast paths:
```lua
-- FAST: Builtin math functions
local abs = math.abs
local sqrt = math.sqrt
for i = 1, n do
  result[i] = sqrt(abs(data[i]))
end

-- SLOWER: Custom implementations
local function my_abs(x)
  return x < 0 and -x or x
end
```

Optimized builtins include:
- `math.*` - abs, sqrt, sin, cos, tan, log, exp, floor, ceil, etc.
- `bit.*` - band, bor, bxor, bnot, lshift, rshift, etc.
- `string.*` - byte, char, sub, etc.
- `table.*` - insert, remove, concat, etc.

#### Avoid Excessive Function Call Depth
```lua
-- BAD: Deep recursion in hot path
local function fib(n)
  if n <= 1 then return n end
  return fib(n-1) + fib(n-2)  -- Many recursive calls
end

-- GOOD: Iterative approach
local function fib(n)
  local a, b = 0, 1
  for i = 2, n do
    a, b = b, a + b
  end
  return b
end
```

### 3.5 String Operations

#### Minimize Concatenation in Loops
```lua
-- BAD: Repeated concatenation
local s = ""
for i = 1, n do
  s = s .. data[i]  -- Creates new string each time
end

-- GOOD: Use table.concat
local parts = {}
for i = 1, n do
  parts[i] = data[i]
end
local s = table.concat(parts)
```

#### Use string.format for Complex Formatting
```lua
-- Reasonably efficient
local s = string.format("%s: %d (%.2f)", name, count, ratio)
```

### 3.6 FFI Best Practices

#### Use FFI for Performance-Critical C Interop
```lua
local ffi = require("ffi")
ffi.cdef[[
  double sin(double x);
  double cos(double x);
]]

-- Direct C calls, very fast
for i = 1, n do
  result[i] = ffi.C.sin(data[i])
end
```

#### Use Typed Arrays
```lua
local ffi = require("ffi")

-- FAST: Typed C array
local arr = ffi.new("double[?]", n)
for i = 0, n-1 do
  arr[i] = i * 1.5
end

-- SLOWER: Lua table
local arr = {}
for i = 1, n do
  arr[i] = i * 1.5
end
```

#### Struct Access with Constant Fields
```lua
ffi.cdef[[
  typedef struct { double x, y, z; } Vec3;
]]

local v = ffi.new("Vec3", {1, 2, 3})

-- FAST: Constant field access
local sum = v.x + v.y + v.z

-- SLOWER: Dynamic field access
local fields = {"x", "y", "z"}
local sum = 0
for _, f in ipairs(fields) do
  sum = sum + v[f]
end
```

### 3.7 Avoiding Trace Aborts

Common reasons traces abort:
1. **NYI (Not Yet Implemented)**: Some operations can't be JIT-compiled
2. **Type instability**: Types change unpredictably
3. **Too many side exits**: Control flow is too complex
4. **Trace too long**: Exceeds IR/snapshot limits

#### Check NYI Operations
```lua
-- Use jit.v for verbose JIT output
local jit = require("jit")
jit.on()
-- jit.opt.start("hotloop=1")  -- For testing

-- NYI examples (check LuaJIT docs for current list):
-- - yield() inside traces
-- - Some string patterns
-- - Unroll limits exceeded
```

#### Profile and Monitor
```lua
-- Enable verbose JIT logging
local jit = require("jit")
require("jit.v").on()  -- Verbose mode

-- Or use the profiler
require("jit.p").start("vl")  -- Profile with VM states
-- ... run code ...
require("jit.p").stop()
```

### 3.8 Summary: Performance Checklist

1. **Types**: Keep types consistent; avoid runtime type changes
2. **Loops**: Use numeric FOR loops; keep bodies simple
3. **Tables**: Use integer indices; pre-allocate; consistent shapes
4. **Functions**: Monomorphic call sites; prefer builtins
5. **Strings**: Avoid concatenation in loops; use table.concat
6. **FFI**: Use for C interop; typed arrays; constant field access
7. **Control flow**: Minimize branches in hot paths
8. **Profiling**: Use `jit.v` and `jit.p` to identify issues

### 3.9 Anti-Patterns to Avoid

```lua
-- AVOID: Mixing types
local x = 1
x = "string"  -- Type change

-- AVOID: Sparse arrays
local arr = {}
arr[1] = "a"
arr[1000000] = "b"  -- Sparse, uses hash part

-- AVOID: Dynamic metatables in hot code
setmetatable(obj, mt)  -- In a hot loop

-- AVOID: Excessive string concatenation
local s = a .. b .. c .. d .. e  -- Multiple temps

-- AVOID: Polymorphic call sites
local f = condition and func1 or func2
for i = 1, n do f(i) end  -- Varies each iteration

-- AVOID: pairs() when ipairs() works
for k, v in pairs(array) do end  -- Use ipairs for arrays

-- AVOID: Creating functions in loops
for i = 1, n do
  local f = function() return i end  -- New closure each iteration
end
```

---

## Appendix: Key Source Files Reference

| File | Purpose | Lines |
|------|---------|-------|
| `lj_jit.h` | JIT engine definitions | ~200 |
| `lj_ir.h` | IR instruction definitions | ~400 |
| `lj_record.c` | Bytecode to IR recording | ~2,900 |
| `lj_opt_fold.c` | Constant folding (380 rules) | ~2,650 |
| `lj_opt_mem.c` | Memory optimizations | ~990 |
| `lj_opt_loop.c` | Loop optimizations | ~300 |
| `lj_opt_narrow.c` | Type narrowing | ~200 |
| `lj_opt_dce.c` | Dead code elimination | ~200 |
| `lj_opt_sink.c` | Allocation sinking | ~300 |
| `lj_asm.c` | IR to assembly | ~2,640 |
| `lj_trace.c` | Trace management | ~1,000 |
| `lj_snap.c` | Snapshot handling | ~1,030 |
| `lj_ffrecord.c` | Fast function recording | ~800 |
| `lj_lib.c` | Library infrastructure | ~300 |
| `vm_x86.dasc` | x86/x64 VM + fast paths | ~6,000 |

---

## References

- [LuaJIT Official Documentation](https://luajit.org/luajit.html)
- [LuaJIT Performance Tips](https://wiki.luajit.org/Numerical-Computing-Performance-Guide)
- [LuaJIT FFI Tutorial](https://luajit.org/ext_ffi_tutorial.html)
