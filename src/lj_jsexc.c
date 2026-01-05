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

/* Push a JavaScript exception handler. */
const BCIns *lj_jsexc_catch(lua_State *L, BCReg catch_reg, const BCIns *catch_pc)
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

  h->base_ofs = L->base - tvref(L->stack);
  h->catch_pc = catch_pc;
  h->catch_reg = catch_reg;

  return catch_pc;
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

/* Throw a JavaScript exception. Returns new PC and sets *newbase, or NULL if no handler. */
const BCIns *lj_jsexc_throw(lua_State *L, TValue *exc, TValue **newbase)
{
  TValue exc_value;
  TValue *current_base = L->base;
  TValue *stack_base = tvref(L->stack);

  /* Copy exception value in case it gets overwritten during unwind. */
  copyTV(L, &exc_value, exc);

  /* First check overflow list. */
  while (L->js_exc_overflow != NULL) {
    JSExcHandlerNode *h = L->js_exc_overflow;
    TValue *handler_base = stack_base + h->base_ofs;

    if (handler_base <= current_base) {
      /* Valid handler - unwind and jump. */
      lj_func_closeuv(L, handler_base);
      L->js_exc_overflow = h->prev;
      *newbase = handler_base;
      copyTV(L, handler_base + h->catch_reg, &exc_value);
      const BCIns *catch_pc = h->catch_pc;
      lj_mem_free(G(L), h, sizeof(JSExcHandlerNode));
      return catch_pc;
    }
    /* Stale handler (frame already returned), remove it. */
    L->js_exc_overflow = h->prev;
    lj_mem_free(G(L), h, sizeof(JSExcHandlerNode));
  }

  /* Then check fixed array. */
  while (L->js_exc_depth > 0) {
    JSExcHandlerNode *h = &L->js_exc_stack[--L->js_exc_depth];
    TValue *handler_base = stack_base + h->base_ofs;

    if (handler_base <= current_base) {
      /* Valid handler - unwind and jump. */
      lj_func_closeuv(L, handler_base);
      *newbase = handler_base;
      copyTV(L, handler_base + h->catch_reg, &exc_value);
      return h->catch_pc;
    }
    /* Stale handler, already decremented depth. */
  }

  /* No handler found - return NULL to indicate we should raise a Lua error. */
  *newbase = NULL;
  /* Copy exception to top for error handling. */
  copyTV(L, L->top++, &exc_value);
  return NULL;
}
