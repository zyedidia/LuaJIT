/*
** JavaScript exception handling for LunaJS.
** Copyright (C) 2025 LunaJS Authors.
*/

#ifndef _LJ_JSEXC_H
#define _LJ_JSEXC_H

#include "lj_obj.h"
#include <setjmp.h>

/* Push a JavaScript exception handler.
** Returns pointer to the jmp_buf for the VM to call setjmp on. */
LJ_FUNC jmp_buf *lj_jsexc_push_handler(lua_State *L, BCReg catch_reg, const BCIns *catch_pc);

/* Called after longjmp returns to the VM. Sets up catch state. */
LJ_FUNC void lj_jsexc_catch_resume(lua_State *L);

/* Pop the current JavaScript exception handler. */
LJ_FUNC void lj_jsexc_uncatch(lua_State *L);

/* Try to handle a JS exception via longjmp. Returns 1 if handled, 0 if not.
** Called from lj_err_throw before doing normal Lua error handling. */
LJ_FUNC int lj_jsexc_try_catch(lua_State *L);

#endif
