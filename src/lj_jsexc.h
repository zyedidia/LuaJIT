/*
** JavaScript exception handling for LunaJS.
** Copyright (C) 2025 LunaJS Authors.
*/

#ifndef _LJ_JSEXC_H
#define _LJ_JSEXC_H

#include "lj_obj.h"

/* Push a JavaScript exception handler. Returns the catch PC on success. */
LJ_FUNC const BCIns *lj_jsexc_catch(lua_State *L, BCReg catch_reg, const BCIns *catch_pc);

/* Pop the current JavaScript exception handler. */
LJ_FUNC void lj_jsexc_uncatch(lua_State *L);

/* Throw a JavaScript exception. Returns new PC and sets *newbase, or NULL if no handler. */
LJ_FUNC const BCIns *lj_jsexc_throw(lua_State *L, TValue *exc, TValue **newbase);

#endif
