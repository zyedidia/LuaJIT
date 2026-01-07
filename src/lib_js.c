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
** neq - JavaScript loose inequality (!=)
**
** Simply the negation of eq.
*/
LJLIB_CF(js_neq)		LJLIB_REC(js_neq)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result = 0;

  /* Fast path: identical values */
  if (lj_obj_equal(a, b)) {
    /* But NaN != NaN is true */
    if (tvisnumber(a)) {
      lua_Number n = numberVnum(a);
      result = (n != n);  /* true for NaN */
    } else {
      result = 0;
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

  /* null == undefined, so null != undefined is false */
  if ((a_is_null || a_is_undef) && (b_is_null || b_is_undef)) {
    result = 0;
    goto done;
  }

  /* If one is null/undefined and other is not, they're not equal (result = true) */
  if (a_is_null || a_is_undef || b_is_null || b_is_undef) {
    result = 1;
    goto done;
  }

  /* Same type comparison */
  if (tvisnumber(a) && tvisnumber(b)) {
    result = 1;  /* Already checked lj_obj_equal, so they're different */
    goto done;
  }
  if (tvisstr(a) && tvisstr(b)) {
    result = 1;
    goto done;
  }

  /* Number == String: convert string to number */
  if (tvisnumber(a) && tvisstr(b)) {
    lua_Number nb;
    if (js_str_to_number(strV(b), &nb)) {
      lua_Number na = numberVnum(a);
      result = (na != nb);
    } else {
      result = 1;  /* Non-numeric string != number */
    }
    goto done;
  }
  if (tvisstr(a) && tvisnumber(b)) {
    lua_Number na;
    if (js_str_to_number(strV(a), &na)) {
      lua_Number nb = numberVnum(b);
      result = (na != nb);
    } else {
      result = 1;
    }
    goto done;
  }

  /* Boolean == X: convert boolean to number */
  if (tvistrue(a) || tvisfalse(a)) {
    lua_Number na = tvistrue(a) ? 1.0 : 0.0;
    if (tvisnumber(b)) {
      result = (na != numberVnum(b));
    } else if (tvisstr(b)) {
      lua_Number nb;
      if (js_str_to_number(strV(b), &nb)) {
        result = (na != nb);
      } else {
        result = 1;
      }
    } else if (tvistrue(b) || tvisfalse(b)) {
      lua_Number nb = tvistrue(b) ? 1.0 : 0.0;
      result = (na != nb);
    } else {
      result = 1;
    }
    goto done;
  }
  if (tvistrue(b) || tvisfalse(b)) {
    lua_Number nb = tvistrue(b) ? 1.0 : 0.0;
    if (tvisnumber(a)) {
      result = (numberVnum(a) != nb);
    } else if (tvisstr(a)) {
      lua_Number na;
      if (js_str_to_number(strV(a), &na)) {
        result = (na != nb);
      } else {
        result = 1;
      }
    } else {
      result = 1;
    }
    goto done;
  }

  /* Tables (objects) - reference equality already checked */
  result = 1;

done:
  lua_pushboolean(L, result);
  return 1;
}

/*
** lte - JavaScript less than or equal (<=)
**
** Fast path for number/number and string/string.
*/
LJLIB_CF(js_lte)		LJLIB_REC(js_lte)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  /* Fast path: both numbers */
  if (tvisnumber(a) && tvisnumber(b)) {
    lua_Number na = numberVnum(a);
    lua_Number nb = numberVnum(b);
    result = na <= nb;
  }
  /* Fast path: both strings */
  else if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) <= 0;
  }
  /* Slow path: convert to numbers */
  else {
    lua_Number na = toNumber_for_cmp(L, a);
    lua_Number nb = toNumber_for_cmp(L, b);
    /* Return false if either is NaN */
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na <= nb;
    }
  }

  lua_pushboolean(L, result);
  return 1;
}

/*
** gte - JavaScript greater than or equal (>=)
**
** Fast path for number/number and string/string.
*/
LJLIB_CF(js_gte)		LJLIB_REC(js_gte)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  /* Fast path: both numbers */
  if (tvisnumber(a) && tvisnumber(b)) {
    lua_Number na = numberVnum(a);
    lua_Number nb = numberVnum(b);
    result = na >= nb;
  }
  /* Fast path: both strings */
  else if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) >= 0;
  }
  /* Slow path: convert to numbers */
  else {
    lua_Number na = toNumber_for_cmp(L, a);
    lua_Number nb = toNumber_for_cmp(L, b);
    /* Return false if either is NaN */
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na >= nb;
    }
  }

  lua_pushboolean(L, result);
  return 1;
}

/*
** add - JavaScript addition (+)
**
** Handles both numeric addition and string concatenation.
** If either operand is a string, concatenates; otherwise adds as numbers.
*/
LJLIB_CF(js_add)		LJLIB_REC(js_add)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* Fast path: both numbers */
  if (tvisnumber(a) && tvisnumber(b)) {
    lua_pushnumber(L, numberVnum(a) + numberVnum(b));
    return 1;
  }

  /* If either is a string, concatenate */
  if (tvisstr(a) || tvisstr(b)) {
    /* Convert both to strings and concatenate */
    /* For now, fall through to Lua for string handling */
    /* This would require string allocation which is complex in C */
    return 0;  /* Signal to use Lua fallback */
  }

  /* Both non-string: convert to numbers and add */
  lua_Number na, nb;

  if (tvisnumber(a)) {
    na = numberVnum(a);
  } else if (tvisstr(a)) {
    TValue tmp;
    if (lj_strscan_number(strV(a), &tmp)) {
      na = numberVnum(&tmp);
    } else {
      na = 0.0 / 0.0;  /* NaN */
    }
  } else if (tvistrue(a)) {
    na = 1.0;
  } else if (tvisfalse(a)) {
    na = 0.0;
  } else if (tvisnil(a)) {
    na = 0.0 / 0.0;  /* nil -> NaN */
  } else if (tvistab(a)) {
    GCtab *t = tabV(a);
    int is_null, is_undef;
    is_null_or_undefined(L, t, &is_null, &is_undef);
    if (is_null) {
      na = 0.0;
    } else if (is_undef) {
      na = 0.0 / 0.0;
    } else {
      na = 0.0 / 0.0;  /* Regular object -> NaN (simplified) */
    }
  } else {
    na = 0.0 / 0.0;
  }

  if (tvisnumber(b)) {
    nb = numberVnum(b);
  } else if (tvisstr(b)) {
    TValue tmp;
    if (lj_strscan_number(strV(b), &tmp)) {
      nb = numberVnum(&tmp);
    } else {
      nb = 0.0 / 0.0;
    }
  } else if (tvistrue(b)) {
    nb = 1.0;
  } else if (tvisfalse(b)) {
    nb = 0.0;
  } else if (tvisnil(b)) {
    nb = 0.0 / 0.0;
  } else if (tvistab(b)) {
    GCtab *t = tabV(b);
    int is_null, is_undef;
    is_null_or_undefined(L, t, &is_null, &is_undef);
    if (is_null) {
      nb = 0.0;
    } else if (is_undef) {
      nb = 0.0 / 0.0;
    } else {
      nb = 0.0 / 0.0;
    }
  } else {
    nb = 0.0 / 0.0;
  }

  lua_pushnumber(L, na + nb);
  return 1;
}

/* Registry key for the inline cache */
#define JS_ICACHE_KEY "__js_icache"

/* Helper: Check if proto is in obj's prototype chain */
static int is_in_proto_chain(lua_State *L, GCtab *obj, GCtab *proto, GCstr *proto_str)
{
  GCtab *t = obj;
  int depth = 0;
  while (t != NULL && depth < 100) {  /* Limit depth to prevent infinite loops */
    if (t == proto) return 1;
    cTValue *proto_tv = lj_tab_getstr(t, proto_str);
    if (proto_tv && tvistab(proto_tv)) {
      t = tabV(proto_tv);
    } else {
      t = NULL;
    }
    depth++;
  }
  return 0;
}

/*
** get - Inline-cached property access
**
** Usage: js.get(obj, key) -> value or undefined
**
** This function implements inline caching for property access:
** 1. Check if property exists directly on obj
** 2. Check inline cache for (obj, key) -> prototype mapping
** 3. If cache hit and prototype still valid, return from cached prototype
** 4. Otherwise, walk prototype chain and update cache
**
** The cache is a weak table stored in the registry.
*/
LJLIB_CF(js_get)
{
  TValue *obj_tv = lj_lib_checkany(L, 1);
  TValue *key_tv = lj_lib_checkany(L, 2);
  GCstr *proto_str;
  cTValue *val;

  /* Get undefined from registry for return value */
  lua_getfield(L, LUA_REGISTRYINDEX, JS_UNDEFINED_KEY);

  /* If obj is not a table, return undefined */
  if (!tvistab(obj_tv)) {
    return 1;
  }

  GCtab *obj = tabV(obj_tv);

  /* Only cache string keys (most common case) */
  if (!tvisstr(key_tv)) {
    /* Fall back to uncached lookup for non-string keys */
    goto uncached_lookup;
  }

  GCstr *key = strV(key_tv);

  /* Step 1: Check if property exists directly on obj */
  val = lj_tab_getstr(obj, key);
  if (val && !tvisnil(val)) {
    copyTV(L, L->top - 1, val);
    return 1;
  }

  /* Step 2: Check inline cache */
  proto_str = lj_str_newlit(L, "__proto__");

  /* Get the cache table from registry */
  lua_getfield(L, LUA_REGISTRYINDEX, JS_ICACHE_KEY);
  if (lua_istable(L, -1)) {
    /* Look up obj in cache */
    lua_pushvalue(L, 1);  /* Push obj */
    lua_rawget(L, -2);    /* cache[obj] */
    if (lua_istable(L, -1)) {
      /* Look up key in obj's cache */
      lua_pushvalue(L, 2);  /* Push key */
      lua_rawget(L, -2);    /* cache[obj][key] */
      if (lua_istable(L, -1)) {
        /* Cache hit - verify the cached prototype is still valid */
        GCtab *cached_proto = tabV(L->top - 1);
        if (is_in_proto_chain(L, obj, cached_proto, proto_str)) {
          /* Valid cache hit - get value from cached prototype */
          val = lj_tab_getstr(cached_proto, key);
          if (val && !tvisnil(val)) {
            /* Found! Replace undefined with value */
            lua_pop(L, 4);  /* Pop cached_proto, cache[obj], cache, undefined */
            lua_getfield(L, LUA_REGISTRYINDEX, JS_UNDEFINED_KEY);
            copyTV(L, L->top - 1, val);
            return 1;
          }
        }
        /* Cache invalid or value not found - fall through to uncached lookup */
      }
      lua_pop(L, 1);  /* Pop cache[obj][key] result */
    }
    lua_pop(L, 1);  /* Pop cache[obj] result */
  }
  lua_pop(L, 1);  /* Pop cache table */

uncached_lookup:
  /* Step 3: Walk prototype chain */
  proto_str = lj_str_newlit(L, "__proto__");
  GCtab *t = obj;
  GCtab *found_proto = NULL;

  /* Skip the object itself (already checked) */
  cTValue *proto_tv = lj_tab_getstr(t, proto_str);
  if (proto_tv && tvistab(proto_tv)) {
    t = tabV(proto_tv);
  } else {
    t = NULL;
  }

  while (t != NULL) {
    if (tvisstr(key_tv)) {
      val = lj_tab_getstr(t, strV(key_tv));
    } else {
      val = lj_tab_get(L, t, key_tv);
    }

    if (val && !tvisnil(val)) {
      found_proto = t;
      break;
    }

    proto_tv = lj_tab_getstr(t, proto_str);
    if (proto_tv && tvistab(proto_tv)) {
      t = tabV(proto_tv);
    } else {
      t = NULL;
    }
  }

  if (found_proto && tvisstr(key_tv)) {
    /* Step 4: Update cache */
    lua_getfield(L, LUA_REGISTRYINDEX, JS_ICACHE_KEY);
    if (lua_istable(L, -1)) {
      /* Get or create cache[obj] */
      lua_pushvalue(L, 1);  /* Push obj */
      lua_rawget(L, -2);    /* cache[obj] */
      if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        /* Create new table for this object */
        lua_newtable(L);
        lua_pushvalue(L, 1);  /* Push obj as key */
        lua_pushvalue(L, -2); /* Push new table as value */
        lua_rawset(L, -4);    /* cache[obj] = new_table */
      }
      /* Now cache[obj] is on top */
      lua_pushvalue(L, 2);  /* Push key */
      settabV(L, L->top, found_proto);
      L->top++;
      lua_rawset(L, -3);    /* cache[obj][key] = found_proto */
      lua_pop(L, 2);        /* Pop cache[obj] and cache */
    } else {
      lua_pop(L, 1);
    }

    /* Return the found value */
    copyTV(L, L->top - 1, val);
    return 1;
  }

  /* Not found, return undefined (already on stack) */
  return 1;
}

/*
** initCache - Initialize the inline cache table
**
** Creates a weak-keyed table for the inline cache.
*/
LJLIB_CF(js_initCache)
{
  /* Create weak table for cache */
  lua_newtable(L);  /* cache table */
  lua_newtable(L);  /* metatable */
  lua_pushstring(L, "k");  /* weak keys */
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, -2);
  lua_setfield(L, LUA_REGISTRYINDEX, JS_ICACHE_KEY);
  return 0;
}

/*
** protoGet - Walk prototype chain to find a property
**
** Usage: js.protoGet(obj, key) -> value or undefined
**
** This function walks the __proto__ chain looking for the key.
** It does NOT handle getters - the Lua code should check for __getters__ first.
**
** Returns:
**   - The value if found directly on obj or any prototype
**   - undefined if not found
**   - undefined if obj is not a table
**
** This is the hot path for property access, so it's optimized:
**   - Uses direct table lookups (lj_tab_get)
**   - Caches the "__proto__" string
**   - Avoids unnecessary allocations
*/
LJLIB_CF(js_protoGet)
{
  TValue *obj_tv = lj_lib_checkany(L, 1);
  TValue *key_tv = lj_lib_checkany(L, 2);
  GCstr *proto_str;

  /* Get undefined from registry for return value */
  lua_getfield(L, LUA_REGISTRYINDEX, JS_UNDEFINED_KEY);

  /* If obj is not a table, return undefined */
  if (!tvistab(obj_tv)) {
    return 1;  /* undefined is already on stack */
  }

  /* Get the "__proto__" string for lookups */
  proto_str = lj_str_newlit(L, "__proto__");

  /* Start with the object itself */
  GCtab *t = tabV(obj_tv);

  /* Try to find the key in the prototype chain */
  while (t != NULL) {
    cTValue *val;

    /* Look up the key in current table */
    if (tvisstr(key_tv)) {
      val = lj_tab_getstr(t, strV(key_tv));
    } else if (tvisnumber(key_tv)) {
      val = lj_tab_get(L, t, key_tv);
    } else {
      /* For other key types, use generic lookup */
      val = lj_tab_get(L, t, key_tv);
    }

    /* If found and not nil, return it */
    if (val && !tvisnil(val)) {
      copyTV(L, L->top - 1, val);  /* Replace undefined with found value */
      return 1;
    }

    /* Move to next prototype */
    cTValue *proto_tv = lj_tab_getstr(t, proto_str);
    if (proto_tv && tvistab(proto_tv)) {
      t = tabV(proto_tv);
    } else {
      t = NULL;
    }
  }

  /* Not found, return undefined (already on stack) */
  return 1;
}

/*
** isNotNullish - Check if value is neither null nor undefined
**
** Usage: js.isNotNullish(val) -> boolean
**
** Returns true if val is not null and not undefined and not nil.
** This is the fast path for JavaScript's `val != null` check,
** which is true when val is neither null nor undefined.
**
** This is a hot path in many benchmarks (e.g., Richards schedule loop).
*/
LJLIB_CF(js_isNotNullish)
{
  TValue *o = lj_lib_checkany(L, 1);
  int result = 1;  /* Default: not nullish */

  /* Check for nil (Lua nil) */
  if (tvisnil(o)) {
    result = 0;
  }
  /* Check for null/undefined tables (from registry) */
  else if (tvistab(o)) {
    GCtab *t = tabV(o);
    int is_null, is_undef;
    if (is_null_or_undefined(L, t, &is_null, &is_undef)) {
      result = 0;
    }
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
