/*
** JavaScript exception handling for LunaJS.
** Copyright (C) 2025 LunaJS Authors.
*/

#define lj_jsexc_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_func.h"
#include "lj_jsexc.h"

/* Push a JavaScript exception handler.
** Returns pointer to the jmp_buf for the VM to call setjmp on. */
jmp_buf *lj_jsexc_push_handler(lua_State *L, BCReg catch_reg, const BCIns *catch_pc)
{
  JSExcHandlerNode *h;

  if (L->js_exc_depth < LJ_JSEXC_FIXED) {
    /* Fast path: use fixed array. */
    h = &L->js_exc_stack[L->js_exc_depth++];
  } else {
    /* Slow path: malloc overflow node. */
    h = (JSExcHandlerNode *)lj_mem_new(L, sizeof(JSExcHandlerNode));
    h->prev = L->js_exc_overflow;
    L->js_exc_overflow = h;
  }

  h->L = L;
  h->base_ofs = L->base - tvref(L->stack);
  h->catch_pc = catch_pc;
  h->catch_reg = catch_reg;

  return &h->jmp;
}

/* Called after longjmp returns to the VM. Sets up catch state. */
void lj_jsexc_catch_resume(lua_State *L)
{
  /* Find the current handler (it's still on top since we haven't popped it). */
  JSExcHandlerNode *h;
  if (L->js_exc_overflow != NULL) {
    h = L->js_exc_overflow;
  } else {
    h = &L->js_exc_stack[L->js_exc_depth - 1];
  }

  TValue *stack_base = tvref(L->stack);
  TValue *handler_base = stack_base + h->base_ofs;

  /* Copy exception value to catch register. */
  copyTV(L, handler_base + h->catch_reg, &L->js_exc_value);

  /* Set up state for VM. */
  L->base = handler_base;
  L->js_catch_pc = h->catch_pc;

  /* Pop the handler. */
  if (L->js_exc_overflow == h) {
    L->js_exc_overflow = h->prev;
    lj_mem_free(G(L), h, sizeof(JSExcHandlerNode));
  } else {
    L->js_exc_depth--;
  }
}

/* Pop the current JavaScript exception handler. */
void lj_jsexc_uncatch(lua_State *L)
{
  if (L->js_exc_overflow != NULL) {
    /* Pop from overflow list first (LIFO). */
    JSExcHandlerNode *h = L->js_exc_overflow;
    L->js_exc_overflow = h->prev;
    lj_mem_free(G(L), h, sizeof(JSExcHandlerNode));
  } else if (L->js_exc_depth > 0) {
    /* Pop from fixed array. */
    L->js_exc_depth--;
  }
}

/* Find a valid JS exception handler. Returns NULL if none found. */
static JSExcHandlerNode *find_js_handler(lua_State *L)
{
  TValue *current_base = L->base;
  TValue *stack_base = tvref(L->stack);

  /* First check overflow list. */
  while (L->js_exc_overflow != NULL) {
    JSExcHandlerNode *h = L->js_exc_overflow;
    TValue *handler_base = stack_base + h->base_ofs;

    if (handler_base <= current_base) {
      return h;  /* Valid handler found */
    }
    /* Stale handler (frame already returned), remove it. */
    L->js_exc_overflow = h->prev;
    lj_mem_free(G(L), h, sizeof(JSExcHandlerNode));
  }

  /* Then check fixed array. */
  while (L->js_exc_depth > 0) {
    JSExcHandlerNode *h = &L->js_exc_stack[L->js_exc_depth - 1];
    TValue *handler_base = stack_base + h->base_ofs;

    if (handler_base <= current_base) {
      return h;  /* Valid handler found */
    }
    /* Stale handler, remove it. */
    L->js_exc_depth--;
  }

  return NULL;  /* No handler found */
}

/* Try to handle a JS exception via longjmp. Returns 1 if handled, 0 if not.
** Called from lj_err_throw before doing normal Lua error handling. */
int lj_jsexc_try_catch(lua_State *L)
{
  JSExcHandlerNode *h = find_js_handler(L);
  if (!h) {
    return 0;  /* No handler, let Lua handle it */
  }

  TValue *stack_base = tvref(L->stack);
  TValue *handler_base = stack_base + h->base_ofs;

  /* Close upvalues between current frame and handler. */
  lj_func_closeuv(L, handler_base);

  /* Copy exception value (it's at L->top - 1 per Lua convention). */
  if (L->top > L->base) {
    copyTV(L, &L->js_exc_value, L->top - 1);
  } else {
    setnilV(&L->js_exc_value);
  }

  /* Do NOT pop the handler here - lj_jsexc_catch_resume will do that.
  ** Just longjmp back to the VM. */
  longjmp(h->jmp, 1);

  /* Should not reach here */
  return 0;
}
