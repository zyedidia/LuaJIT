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
#include "lj_strscan.h"

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
** inc - JavaScript increment (a + 1)
**
** Fast path for numbers, converts other types per JS ToNumber semantics:
** - null -> 0, so null + 1 = 1
** - undefined -> NaN
** - strings -> parsed number or NaN
** - booleans -> 0 or 1
** - other -> NaN
*/
LJLIB_CF(js_inc)		LJLIB_REC(js_inc)
{
  TValue *o = lj_lib_checkany(L, 1);
  lua_Number n;

  if (tvisnumber(o)) {
    n = numberVnum(o) + 1.0;
  } else if (tvisstr(o) && lj_strscan_number(strV(o), o)) {
    n = numberVnum(o) + 1.0;
  } else if (tvistrue(o)) {
    n = 2.0;  /* true -> 1, + 1 = 2 */
  } else if (tvisfalse(o)) {
    n = 1.0;  /* false -> 0, + 1 = 1 */
  } else if (tvistab(o)) {
    /* Check for null (converts to 0) */
    GCtab *t = tabV(o);
    lua_getfield(L, LUA_REGISTRYINDEX, JS_NULL_KEY);
    if (lua_istable(L, -1) && tabV(L->top - 1) == t) {
      lua_pop(L, 1);
      n = 1.0;  /* null -> 0, + 1 = 1 */
    } else {
      lua_pop(L, 1);
      /* undefined or other table -> NaN */
      n = 0.0 / 0.0;
    }
  } else {
    /* Non-numeric value -> NaN */
    n = 0.0 / 0.0;
  }

  lua_pushnumber(L, n);
  return 1;
}

/*
** dec - JavaScript decrement (a - 1)
**
** Fast path for numbers, converts other types per JS ToNumber semantics.
*/
LJLIB_CF(js_dec)		LJLIB_REC(js_dec)
{
  TValue *o = lj_lib_checkany(L, 1);
  lua_Number n;

  if (tvisnumber(o)) {
    n = numberVnum(o) - 1.0;
  } else if (tvisstr(o) && lj_strscan_number(strV(o), o)) {
    n = numberVnum(o) - 1.0;
  } else if (tvistrue(o)) {
    n = 0.0;  /* true -> 1, - 1 = 0 */
  } else if (tvisfalse(o)) {
    n = -1.0;  /* false -> 0, - 1 = -1 */
  } else if (tvistab(o)) {
    /* Check for null (converts to 0) */
    GCtab *t = tabV(o);
    lua_getfield(L, LUA_REGISTRYINDEX, JS_NULL_KEY);
    if (lua_istable(L, -1) && tabV(L->top - 1) == t) {
      lua_pop(L, 1);
      n = -1.0;  /* null -> 0, - 1 = -1 */
    } else {
      lua_pop(L, 1);
      /* undefined or other table -> NaN */
      n = 0.0 / 0.0;
    }
  } else {
    /* Non-numeric value -> NaN */
    n = 0.0 / 0.0;
  }

  lua_pushnumber(L, n);
  return 1;
}

/* Helper: Convert value to number for comparison (JS ToNumber semantics) */
static lua_Number toNumber_for_cmp(lua_State *L, TValue *o)
{
  if (tvisnumber(o)) {
    return numberVnum(o);
  } else if (tvisstr(o)) {
    TValue tmp;
    if (lj_strscan_number(strV(o), &tmp)) {
      return numberVnum(&tmp);
    }
    return 0.0 / 0.0;  /* NaN */
  } else if (tvistrue(o)) {
    return 1.0;
  } else if (tvisfalse(o)) {
    return 0.0;
  } else if (tvistab(o)) {
    /* Check for null (converts to 0) */
    GCtab *t = tabV(o);
    lua_getfield(L, LUA_REGISTRYINDEX, JS_NULL_KEY);
    if (lua_istable(L, -1) && tabV(L->top - 1) == t) {
      lua_pop(L, 1);
      return 0.0;  /* null -> 0 */
    }
    lua_pop(L, 1);
    return 0.0 / 0.0;  /* undefined or other table -> NaN */
  } else if (tvisnil(o)) {
    return 0.0 / 0.0;  /* nil -> NaN */
  }
  return 0.0 / 0.0;  /* NaN for unknown types */
}

/*
** lt - JavaScript less than (a < b)
**
** Fast path for number/number and string/string.
** For mixed types: convert both to number, return false if either is NaN.
*/
LJLIB_CF(js_lt)		LJLIB_REC(js_lt)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  /* Fast path: both numbers */
  if (tvisnumber(a) && tvisnumber(b)) {
    lua_Number na = numberVnum(a);
    lua_Number nb = numberVnum(b);
    result = na < nb;
  }
  /* Fast path: both strings (lexicographic comparison) */
  else if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) < 0;
  }
  /* Slow path: convert to numbers */
  else {
    lua_Number na = toNumber_for_cmp(L, a);
    lua_Number nb = toNumber_for_cmp(L, b);
    /* Return false if either is NaN */
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na < nb;
    }
  }

  lua_pushboolean(L, result);
  return 1;
}

/*
** gt - JavaScript greater than (a > b)
**
** Fast path for number/number and string/string.
** For mixed types: convert both to number, return false if either is NaN.
*/
LJLIB_CF(js_gt)		LJLIB_REC(js_gt)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  /* Fast path: both numbers */
  if (tvisnumber(a) && tvisnumber(b)) {
    lua_Number na = numberVnum(a);
    lua_Number nb = numberVnum(b);
    result = na > nb;
  }
  /* Fast path: both strings (lexicographic comparison) */
  else if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) > 0;
  }
  /* Slow path: convert to numbers */
  else {
    lua_Number na = toNumber_for_cmp(L, a);
    lua_Number nb = toNumber_for_cmp(L, b);
    /* Return false if either is NaN */
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na > nb;
    }
  }

  lua_pushboolean(L, result);
  return 1;
}

/* Helper: Convert string to number with JS semantics (empty string -> 0) */
static int js_str_to_number(GCstr *s, lua_Number *out)
{
  if (s->len == 0) {
    *out = 0.0;  /* Empty string converts to 0 in JavaScript */
    return 1;
  }
  TValue tmp;
  if (lj_strscan_number(s, &tmp)) {
    *out = numberVnum(&tmp);
    return 1;
  }
  return 0;  /* Non-numeric string */
}

/* Helper: Check if a table is null or undefined from registry */
static int is_null_or_undefined(lua_State *L, GCtab *t, int *is_null, int *is_undef)
{
  *is_null = 0;
  *is_undef = 0;
  lua_getfield(L, LUA_REGISTRYINDEX, JS_NULL_KEY);
  if (lua_istable(L, -1) && tabV(L->top - 1) == t) {
    *is_null = 1;
    lua_pop(L, 1);
    return 1;
  }
  lua_pop(L, 1);
  lua_getfield(L, LUA_REGISTRYINDEX, JS_UNDEFINED_KEY);
  if (lua_istable(L, -1) && tabV(L->top - 1) == t) {
    *is_undef = 1;
    lua_pop(L, 1);
    return 1;
  }
  lua_pop(L, 1);
  return 0;
}

/*
** eq - JavaScript loose equality (==)
**
** Implements JavaScript's Abstract Equality Comparison:
** 1. Same type: use === (strict equality)
** 2. null == undefined (and vice versa)
** 3. Number == String: convert string to number
** 4. Boolean == X: convert boolean to number, then compare
** 5. Object == primitive: not fully supported, returns false
*/
LJLIB_CF(js_eq)		LJLIB_REC(js_eq)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result = 0;

  /* Fast path: identical values */
  if (lj_obj_equal(a, b)) {
    /* But NaN != NaN */
    if (tvisnumber(a)) {
      lua_Number n = numberVnum(a);
      result = (n == n);  /* false for NaN */
    } else {
      result = 1;
    }
    goto done;
  }

  /* Check for null/undefined */
  int a_is_null = 0, a_is_undef = 0, b_is_null = 0, b_is_undef = 0;
  if (tvistab(a)) {
    is_null_or_undefined(L, tabV(a), &a_is_null, &a_is_undef);
  }
  if (tvistab(b)) {
    is_null_or_undefined(L, tabV(b), &b_is_null, &b_is_undef);
  }

  /* null == undefined */
  if ((a_is_null || a_is_undef) && (b_is_null || b_is_undef)) {
    result = 1;
    goto done;
  }

  /* If one is null/undefined and other is not, they're not equal */
  if (a_is_null || a_is_undef || b_is_null || b_is_undef) {
    result = 0;
    goto done;
  }

  /* Same type comparison */
  if (tvisnumber(a) && tvisnumber(b)) {
    /* Already checked lj_obj_equal, so they're different or NaN */
    result = 0;
    goto done;
  }
  if (tvisstr(a) && tvisstr(b)) {
    /* String comparison - already checked lj_obj_equal */
    result = 0;
    goto done;
  }

  /* Number == String: convert string to number */
  if (tvisnumber(a) && tvisstr(b)) {
    lua_Number nb;
    if (js_str_to_number(strV(b), &nb)) {
      lua_Number na = numberVnum(a);
      result = (na == nb);
    } else {
      result = 0;  /* Non-numeric string != number */
    }
    goto done;
  }
  if (tvisstr(a) && tvisnumber(b)) {
    lua_Number na;
    if (js_str_to_number(strV(a), &na)) {
      lua_Number nb = numberVnum(b);
      result = (na == nb);
    } else {
      result = 0;
    }
    goto done;
  }

  /* Boolean == X: convert boolean to number (0 or 1) */
  if (tvistrue(a) || tvisfalse(a)) {
    lua_Number na = tvistrue(a) ? 1.0 : 0.0;
    if (tvisnumber(b)) {
      result = (na == numberVnum(b));
    } else if (tvisstr(b)) {
      lua_Number nb;
      if (js_str_to_number(strV(b), &nb)) {
        result = (na == nb);
      } else {
        result = 0;
      }
    } else if (tvistrue(b) || tvisfalse(b)) {
      lua_Number nb = tvistrue(b) ? 1.0 : 0.0;
      result = (na == nb);
    } else {
      result = 0;
    }
    goto done;
  }
  if (tvistrue(b) || tvisfalse(b)) {
    lua_Number nb = tvistrue(b) ? 1.0 : 0.0;
    if (tvisnumber(a)) {
      result = (numberVnum(a) == nb);
    } else if (tvisstr(a)) {
      lua_Number na;
      if (js_str_to_number(strV(a), &na)) {
        result = (na == nb);
      } else {
        result = 0;
      }
    } else {
      result = 0;
    }
    goto done;
  }

  /* Tables (objects) - reference equality already checked via lj_obj_equal */
  /* Different objects are not equal */
  result = 0;

done:
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
