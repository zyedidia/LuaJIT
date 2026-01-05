# LuaJIT Modifications for LunaJS

This document describes the modifications made to LuaJIT to support JavaScript exception handling in LunaJS.

## Overview

LunaJS needs to implement JavaScript's `try`/`catch`/`throw` semantics. Rather than using Lua's `pcall` wrapper approach, we added native bytecode instructions that integrate with Lua's error handling via `setjmp`/`longjmp`.

## New Bytecode Instructions

### `BC_JSCATCH` - Register Exception Handler

```
jscatch <catch_reg>, <label>
```

- Pushes an exception handler onto the JS exception stack
- Calls `setjmp` directly from the VM to establish a return point
- On normal execution, continues to the next instruction
- When an exception is caught (via `longjmp`), jumps to `<label>` with the exception value in `<catch_reg>`

### `BC_JSUNCATCH` - Remove Exception Handler

```
jsuncatch
```

- Pops the current exception handler from the JS exception stack
- Called when exiting a try block normally (without an exception)

## Implementation Details

### Exception Handler Structure (`lj_obj.h`)

```c
typedef struct JSExcHandlerNode {
  jmp_buf jmp;               /* setjmp buffer for longjmp unwinding */
  struct lua_State *L;       /* Lua state (needed after longjmp) */
  ptrdiff_t base_ofs;        /* Stack offset (survives realloc) */
  const BCIns *catch_pc;     /* PC to jump to on catch */
  BCReg catch_reg;           /* Register to store exception value */
  struct JSExcHandlerNode *prev;  /* Previous handler (overflow list) */
} JSExcHandlerNode;
```

### State Extensions (`lua_State`)

```c
JSExcHandlerNode js_exc_stack[32];  /* Fixed handler array */
uint8_t js_exc_depth;               /* Handler count */
JSExcHandlerNode *js_exc_overflow;  /* Overflow list for deep nesting */
TValue js_exc_value;                /* Pending exception value */
const BCIns *js_catch_pc;           /* Catch PC for VM resumption */
```

### Key Functions (`lj_jsexc.c`)

- `lj_jsexc_push_handler()` - Pushes handler, returns `jmp_buf*` for VM to call `setjmp`
- `lj_jsexc_catch_resume()` - Called after `longjmp`, sets up catch state
- `lj_jsexc_uncatch()` - Pops handler on normal try block exit
- `lj_jsexc_try_catch()` - Called from `lj_err_throw`, performs `longjmp` if handler exists

### Integration with Error Handling (`lj_err.c`)

At the start of `lj_err_throw()`:

```c
if (errcode == LUA_ERRRUN && lj_jsexc_try_catch(L)) {
    return;  /* Handled by JS catch - longjmp already done */
}
```

### VM Assembly (`vm_x64.dasc`)

`BC_JSCATCH` calls `setjmp` directly from the VM:

```asm
case BC_JSCATCH:
  |  ins_AD
  |  lea RB, [PC+RD*4-BCBIAS_J*4]      // catch_pc
  |  mov L:CARG1, SAVE_L
  |  mov L:CARG1->base, BASE
  |  mov CARG2d, RAd                    // catch_reg
  |  mov CARG3, RB                      // catch_pc
  |  call extern lj_jsexc_push_handler  // Returns jmp_buf*
  |  mov CARG1, rax
  |  call extern _setjmp                // setjmp from VM frame
  |  test eax, eax
  |  jnz >1                             // longjmp returned
  |  mov L:RB, SAVE_L
  |  mov BASE, L:RB->base
  |  ins_next                           // Normal path
  |1:
  |  mov L:CARG1, SAVE_L
  |  call extern lj_jsexc_catch_resume  // Set up catch state
  |  mov L:RB, SAVE_L
  |  mov BASE, L:RB->base
  |  mov PC, L:RB->js_catch_pc
  |  ins_next                           // Jump to catch handler
```

## Design Rationale

### Why setjmp in VM Assembly?

The key insight is that `setjmp` must be called from a stack frame that remains active until `longjmp` is called. If we called `setjmp` from a C function that returns, the jmp_buf would be invalid.

By calling `setjmp` directly from the VM's interpreter loop, the jmp_buf remains valid for the entire duration of the try block, even across nested Lua/C function calls.

### Why Not pcall Wrappers?

Using `pcall` would require wrapping try blocks in closures:

```lua
local ok, err = pcall(function()
    -- try block
end)
if not ok then
    -- catch block
end
```

This approach:
- Changes code structure significantly
- Has performance overhead from closure creation
- Complicates variable scoping

The native bytecode approach preserves the original code structure and integrates seamlessly with Lua's existing error handling.

## Usage Example

```lua
local exc
jscatch exc, catch_label
-- try block code
error("something went wrong")
jsuncatch
goto end_label

::catch_label::
print("Caught:", exc)

::end_label::
```

## Files Modified

| File | Changes |
|------|---------|
| `src/lj_obj.h` | Added `JSExcHandlerNode`, extended `lua_State` |
| `src/lj_jsexc.c` | Exception handling implementation |
| `src/lj_jsexc.h` | Function declarations |
| `src/lj_err.c` | Hook into `lj_err_throw()` |
| `src/lj_state.c` | Initialize new `lua_State` fields |
| `src/lj_bc.h` | Added `BC_JSCATCH`, `BC_JSUNCATCH` |
| `src/lj_lex.h` | Added `jscatch`, `jsuncatch` keywords |
| `src/lj_parse.c` | Parser for new statements |
| `src/vm_x64.dasc` | VM implementation of new bytecodes |
| `src/Makefile` | Added `lj_jsexc.o` to build |
