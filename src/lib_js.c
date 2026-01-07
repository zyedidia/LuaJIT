/*
** JavaScript support library for LunaJS.
** Copyright (C) 2025. See Copyright Notice in luajit.h
*/

#define lib_js_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_lib.h"

/* Registry keys for JS special values */
#define JS_NULL_KEY "__js_null"
#define JS_UNDEFINED_KEY "__js_undefined"

/* -- JavaScript library functions ----------------------------------------- */

#define LJLIB_MODULE_js

/*
** toBoolean - JavaScript truthiness check
**
** Returns false for:
**   - nil (Lua nil)
**   - false (Lua false)
**   - null (JS null - stored in registry)
**   - undefined (JS undefined - stored in registry)
**   - 0 (number zero)
**   - NaN (not a number)
**   - "" (empty string)
**
** Returns true for everything else.
**
** Note: js.init() must be called first to register null/undefined.
*/
LJLIB_CF(js_toBoolean)		LJLIB_REC(js_toBoolean)
{
  TValue *o = lj_lib_checkany(L, 1);
  int result = 1;  /* Default: truthy */

  /* Check for nil */
  if (tvisnil(o)) {
    result = 0;
  }
  /* Check for false */
  else if (tvisfalse(o)) {
    result = 0;
  }
  /* Check for number (0 and NaN are falsy) */
  else if (tvisnumber(o)) {
    lua_Number n = numberVnum(o);
    /* 0 is falsy, NaN is falsy (NaN != NaN) */
    if (n == 0 || n != n) {
      result = 0;
    }
  }
  /* Check for empty string */
  else if (tvisstr(o)) {
    GCstr *s = strV(o);
    if (s->len == 0) {
      result = 0;
    }
  }
  /* Check for null/undefined tables (from registry) */
  else if (tvistab(o)) {
    GCtab *t = tabV(o);
    /* Get null from registry */
    lua_getfield(L, LUA_REGISTRYINDEX, JS_NULL_KEY);
    if (lua_istable(L, -1) && tabV(L->top - 1) == t) {
      result = 0;
    } else {
      lua_pop(L, 1);
      /* Get undefined from registry */
      lua_getfield(L, LUA_REGISTRYINDEX, JS_UNDEFINED_KEY);
      if (lua_istable(L, -1) && tabV(L->top - 1) == t) {
        result = 0;
      }
    }
    lua_pop(L, 1);
  }

  lua_pushboolean(L, result);
  return 1;
}

/*
** Initialize the js library.
** This function should be called from Lua after creating null/undefined:
**   js.init(null, undefined)
**
** This stores null/undefined in the registry so toBoolean can access them.
** The builtin js.toBoolean fast function remains unchanged.
*/
LJLIB_CF(js_init)
{
  /* Get null and undefined from arguments */
  lj_lib_checktab(L, 1);  /* null */
  lj_lib_checktab(L, 2);  /* undefined */

  /* Store in registry for toBoolean to access */
  lua_pushvalue(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, JS_NULL_KEY);
  lua_pushvalue(L, 2);
  lua_setfield(L, LUA_REGISTRYINDEX, JS_UNDEFINED_KEY);

  return 0;
}

/* -- Library registration ------------------------------------------------- */

#include "lj_libdef.h"

LUALIB_API int luaopen_js(lua_State *L)
{
  LJ_LIB_REG(L, "js", js);
  return 1;
}
