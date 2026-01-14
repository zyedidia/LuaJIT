/*
** JavaScript support library for LunarJS.
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
#include "lj_buf.h"
#include "lj_lib.h"
#include "lj_strscan.h"

#include <math.h>

/* Registry keys for JS special values */
#define JS_NULL_KEY "__js_null"
#define JS_UNDEFINED_KEY "__js_undefined"

/* Forward declarations for helper functions */
static int is_null(lua_State *L, GCtab *t);
static int is_undefined(lua_State *L, GCtab *t);

/* -- JavaScript library functions ----------------------------------------- */

#define LJLIB_MODULE_js

/*
** toBoolean - JavaScript truthiness check
**
** Returns false for:
**   - nil (Lua nil = JS undefined)
**   - false (Lua false)
**   - null (JS null - stored in registry)
**   - 0 (number zero)
**   - NaN (not a number)
**   - "" (empty string)
**
** Returns true for everything else.
*/
/*
** toBoolean - Convert JS value to boolean
**
** Fast path in assembly handles: nil, false, true, numbers (0/NaN check).
** Fallback handles: strings (empty check), tables (null check).
*/
LJLIB_ASM(js_toBoolean)		LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);
  int result = 1;  /* Default: truthy */

  /* Check for empty string */
  if (tvisstr(o)) {
    GCstr *s = strV(o);
    if (s->len == 0) {
      result = 0;
    }
  }
  /* Check for null or undefined table (from registry) */
  else if (tvistab(o)) {
    GCtab *t = tabV(o);
    if (is_null(L, t) || is_undefined(L, t)) {
      result = 0;
    }
  }
  /* Other types handled by assembly fast path, but fallback still works */
  else if (tvisnil(o) || tvisfalse(o)) {
    result = 0;
  }
  else if (tvisnumber(o)) {
    lua_Number n = numberVnum(o);
    if (n == 0 || n != n) {
      result = 0;
    }
  }

  setboolV(L->base-1-LJ_FR2, result);
  return FFH_RES(1);
}

/* Helper: Check if a table is the null sentinel */
static int is_null(lua_State *L, GCtab *t)
{
  lua_getfield(L, LUA_REGISTRYINDEX, JS_NULL_KEY);
  int result = lua_istable(L, -1) && tabV(L->top - 1) == t;
  lua_pop(L, 1);
  return result;
}

/* Helper: Check if a table is the undefined sentinel */
static int is_undefined(lua_State *L, GCtab *t)
{
  lua_getfield(L, LUA_REGISTRYINDEX, JS_UNDEFINED_KEY);
  int result = lua_istable(L, -1) && tabV(L->top - 1) == t;
  lua_pop(L, 1);
  return result;
}

/*
** inc - JavaScript increment (a + 1)
**
** Fast path in assembly handles number case.
** This fallback handles type coercion.
*/
LJLIB_ASM(js_inc)		LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);
  lua_Number n;

  if (tvisstr(o) && lj_strscan_number(strV(o), o)) {
    n = numberVnum(o) + 1.0;
  } else if (tvistrue(o)) {
    n = 2.0;  /* true -> 1, + 1 = 2 */
  } else if (tvisfalse(o)) {
    n = 1.0;  /* false -> 0, + 1 = 1 */
  } else if (tvisnil(o)) {
    n = 0.0 / 0.0;  /* undefined (nil) -> NaN */
  } else if (tvistab(o)) {
    GCtab *t = tabV(o);
    if (is_null(L, t)) {
      n = 1.0;  /* null -> 0, + 1 = 1 */
    } else {
      n = 0.0 / 0.0;
    }
  } else {
    n = 0.0 / 0.0;
  }

  setnumV(L->base-1-LJ_FR2, n);
  return FFH_RES(1);
}

/*
** dec - JavaScript decrement (a - 1)
**
** Fast path in assembly handles number case.
** This fallback handles type coercion.
*/
LJLIB_ASM(js_dec)		LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);
  lua_Number n;

  if (tvisstr(o) && lj_strscan_number(strV(o), o)) {
    n = numberVnum(o) - 1.0;
  } else if (tvistrue(o)) {
    n = 0.0;  /* true -> 1, - 1 = 0 */
  } else if (tvisfalse(o)) {
    n = -1.0;  /* false -> 0, - 1 = -1 */
  } else if (tvisnil(o)) {
    n = 0.0 / 0.0;  /* undefined (nil) -> NaN */
  } else if (tvistab(o)) {
    GCtab *t = tabV(o);
    if (is_null(L, t)) {
      n = -1.0;  /* null -> 0, - 1 = -1 */
    } else {
      n = 0.0 / 0.0;
    }
  } else {
    n = 0.0 / 0.0;
  }

  setnumV(L->base-1-LJ_FR2, n);
  return FFH_RES(1);
}

/* Helper: Convert value to number (JS ToNumber semantics) */
static lua_Number js_toNumber_impl(lua_State *L, TValue *o)
{
  if (tvisnumber(o)) {
    return numberVnum(o);
  } else if (tvisstr(o)) {
    GCstr *s = strV(o);
    if (s->len == 0) {
      return 0.0;  /* Empty string -> 0 */
    }
    TValue tmp;
    if (lj_strscan_number(s, &tmp)) {
      return numberVnum(&tmp);
    }
    return 0.0 / 0.0;  /* NaN */
  } else if (tvistrue(o)) {
    return 1.0;
  } else if (tvisfalse(o)) {
    return 0.0;
  } else if (tvisnil(o)) {
    return 0.0 / 0.0;  /* undefined (nil) -> NaN */
  } else if (tvistab(o)) {
    GCtab *t = tabV(o);
    if (is_null(L, t)) {
      return 0.0;  /* null -> 0 */
    }
    return 0.0 / 0.0;  /* object -> NaN (simplified) */
  }
  return 0.0 / 0.0;
}

/*
** lt - JavaScript less than (a < b)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_lt)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  /* Fast path: both strings (lexicographic comparison) */
  if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) < 0;
  }
  /* Slow path: convert to numbers */
  else {
    lua_Number na = js_toNumber_impl(L, a);
    lua_Number nb = js_toNumber_impl(L, b);
    /* Return false if either is NaN */
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na < nb;
    }
  }

  setboolV(L->base-1-LJ_FR2, result);
  return FFH_RES(1);
}

/*
** gt - JavaScript greater than (a > b)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_gt)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) > 0;
  }
  else {
    lua_Number na = js_toNumber_impl(L, a);
    lua_Number nb = js_toNumber_impl(L, b);
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na > nb;
    }
  }

  setboolV(L->base-1-LJ_FR2, result);
  return FFH_RES(1);
}

/*
** lte - JavaScript less than or equal (<=)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_lte)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) <= 0;
  }
  else {
    lua_Number na = js_toNumber_impl(L, a);
    lua_Number nb = js_toNumber_impl(L, b);
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na <= nb;
    }
  }

  setboolV(L->base-1-LJ_FR2, result);
  return FFH_RES(1);
}

/*
** gte - JavaScript greater than or equal (>=)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_gte)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result;

  if (tvisstr(a) && tvisstr(b)) {
    GCstr *sa = strV(a);
    GCstr *sb = strV(b);
    result = lj_str_cmp(sa, sb) >= 0;
  }
  else {
    lua_Number na = js_toNumber_impl(L, a);
    lua_Number nb = js_toNumber_impl(L, b);
    if (na != na || nb != nb) {
      result = 0;
    } else {
      result = na >= nb;
    }
  }

  setboolV(L->base-1-LJ_FR2, result);
  return FFH_RES(1);
}

/* -- Arithmetic operators ------------------------------------------------- */

/* Helper: Convert value to string for concatenation */
static GCstr *js_toString_impl(lua_State *L, TValue *o)
{
  if (tvisstr(o)) {
    return strV(o);
  } else if (tvisnumber(o)) {
    char buf[64];
    lua_Number n = numberVnum(o);
    if (n != n) {
      return lj_str_newlit(L, "NaN");
    } else if (n == 1.0/0.0) {
      return lj_str_newlit(L, "Infinity");
    } else if (n == -1.0/0.0) {
      return lj_str_newlit(L, "-Infinity");
    } else {
      /* Use Lua's number formatting */
      int len = snprintf(buf, sizeof(buf), LUA_NUMBER_FMT, n);
      return lj_str_new(L, buf, len);
    }
  } else if (tvistrue(o)) {
    return lj_str_newlit(L, "true");
  } else if (tvisfalse(o)) {
    return lj_str_newlit(L, "false");
  } else if (tvisnil(o)) {
    return lj_str_newlit(L, "undefined");
  } else if (tvistab(o)) {
    GCtab *t = tabV(o);
    if (is_null(L, t)) {
      return lj_str_newlit(L, "null");
    }
    return lj_str_newlit(L, "[object Object]");
  } else if (tvisfunc(o)) {
    return lj_str_newlit(L, "function");
  }
  return lj_str_newlit(L, "undefined");
}

/*
** toNumber - JavaScript ToNumber conversion
**
** Explicitly convert a value to a number per JS semantics.
*/
LJLIB_CF(js_toNumber)		LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);
  lua_pushnumber(L, js_toNumber_impl(L, o));
  return 1;
}

/*
** add - JavaScript addition (a + b)
**
** If either operand is a string, performs string concatenation.
** Otherwise converts both to numbers and adds.
**
** Fast path in assembly handles number+number case.
** This fallback handles string concatenation and type coercion.
*/
LJLIB_ASM(js_add)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* String concatenation if either is a string */
  if (tvisstr(a) || tvisstr(b)) {
    GCstr *sa = js_toString_impl(L, a);
    GCstr *sb = js_toString_impl(L, b);
    /* Concatenate strings */
    GCstr *result = lj_buf_cat2str(L, sa, sb);
    setstrV(L, L->base-1-LJ_FR2, result);
    return FFH_RES(1);
  }

  /* Convert to numbers and add */
  lua_Number na = js_toNumber_impl(L, a);
  lua_Number nb = js_toNumber_impl(L, b);
  setnumV(L->base-1-LJ_FR2, na + nb);
  return FFH_RES(1);
}

/*
** sub - JavaScript subtraction (a - b)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_sub)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* Slow path: convert to numbers */
  lua_Number na = js_toNumber_impl(L, a);
  lua_Number nb = js_toNumber_impl(L, b);
  setnumV(L->base-1-LJ_FR2, na - nb);
  return FFH_RES(1);
}

/*
** mul - JavaScript multiplication (a * b)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_mul)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* Slow path: convert to numbers */
  lua_Number na = js_toNumber_impl(L, a);
  lua_Number nb = js_toNumber_impl(L, b);
  setnumV(L->base-1-LJ_FR2, na * nb);
  return FFH_RES(1);
}

/*
** div - JavaScript division (a / b)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_div)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* Slow path: convert to numbers */
  lua_Number na = js_toNumber_impl(L, a);
  lua_Number nb = js_toNumber_impl(L, b);
  setnumV(L->base-1-LJ_FR2, na / nb);
  return FFH_RES(1);
}

/*
** mod - JavaScript modulo (a % b)
**
** Uses fmod semantics where result has same sign as dividend (like C).
** This differs from Lua's % which has same sign as divisor.
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_mod)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* Slow path: convert to numbers */
  lua_Number na = js_toNumber_impl(L, a);
  lua_Number nb = js_toNumber_impl(L, b);
  setnumV(L->base-1-LJ_FR2, fmod(na, nb));
  return FFH_RES(1);
}

/*
** pow - JavaScript exponentiation (a ** b)
**
** Fast path in assembly handles number+number case.
*/
LJLIB_ASM(js_pow)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* Slow path: convert to numbers */
  lua_Number na = js_toNumber_impl(L, a);
  lua_Number nb = js_toNumber_impl(L, b);
  setnumV(L->base-1-LJ_FR2, pow(na, nb));
  return FFH_RES(1);
}

/*
** neg - JavaScript unary negation (-a)
**
** Fast path in assembly handles number case.
*/
LJLIB_ASM(js_neg)		LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);

  /* Slow path: convert to number */
  lua_Number n = js_toNumber_impl(L, o);
  setnumV(L->base-1-LJ_FR2, -n);
  return FFH_RES(1);
}

/*
** pos - JavaScript unary plus (+a)
**
** Simply converts the value to a number (ToNumber).
** Fast path in assembly handles number case (just returns it).
*/
LJLIB_ASM(js_pos)		LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);

  /* Slow path: convert to number */
  setnumV(L->base-1-LJ_FR2, js_toNumber_impl(L, o));
  return FFH_RES(1);
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

/*
** eq - JavaScript loose equality (==)
*/
LJLIB_CF(js_eq)		LJLIB_REC(.)
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
  if (tvisnil(a)) {
    a_is_undef = 1;
  } else if (tvistab(a)) {
    GCtab *t = tabV(a);
    if (is_null(L, t)) a_is_null = 1;
    else if (is_undefined(L, t)) a_is_undef = 1;
  }
  if (tvisnil(b)) {
    b_is_undef = 1;
  } else if (tvistab(b)) {
    GCtab *t = tabV(b);
    if (is_null(L, t)) b_is_null = 1;
    else if (is_undefined(L, t)) b_is_undef = 1;
  }

  /* null == undefined (loose equality) */
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
    result = 0;
    goto done;
  }
  if (tvisstr(a) && tvisstr(b)) {
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
      result = 0;
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

  /* Boolean == X: convert boolean to number */
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

  result = 0;

done:
  lua_pushboolean(L, result);
  return 1;
}

/*
** neq - JavaScript loose inequality (!=)
*/
LJLIB_CF(js_neq)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result = 0;

  /* Fast path: identical values */
  if (lj_obj_equal(a, b)) {
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
  if (tvisnil(a)) {
    a_is_undef = 1;
  } else if (tvistab(a)) {
    GCtab *t = tabV(a);
    if (is_null(L, t)) a_is_null = 1;
    else if (is_undefined(L, t)) a_is_undef = 1;
  }
  if (tvisnil(b)) {
    b_is_undef = 1;
  } else if (tvistab(b)) {
    GCtab *t = tabV(b);
    if (is_null(L, t)) b_is_null = 1;
    else if (is_undefined(L, t)) b_is_undef = 1;
  }

  if ((a_is_null || a_is_undef) && (b_is_null || b_is_undef)) {
    result = 0;
    goto done;
  }

  if (a_is_null || a_is_undef || b_is_null || b_is_undef) {
    result = 1;
    goto done;
  }

  if (tvisnumber(a) && tvisnumber(b)) {
    result = 1;
    goto done;
  }
  if (tvisstr(a) && tvisstr(b)) {
    result = 1;
    goto done;
  }

  if (tvisnumber(a) && tvisstr(b)) {
    lua_Number nb;
    if (js_str_to_number(strV(b), &nb)) {
      lua_Number na = numberVnum(a);
      result = (na != nb);
    } else {
      result = 1;
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

  result = 1;

done:
  lua_pushboolean(L, result);
  return 1;
}

/*
** seq - JavaScript strict equality (===)
**
** Returns true only if both values have the same type AND same value.
** No type coercion is performed.
*/
LJLIB_CF(js_seq)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result = 0;

  /* Check if both are undefined (nil or undefined sentinel) */
  /* This handles the case where missing args (nil) are compared to undefined sentinel */
  int a_is_undef = tvisnil(a) || (tvistab(a) && is_undefined(L, tabV(a)));
  int b_is_undef = tvisnil(b) || (tvistab(b) && is_undefined(L, tabV(b)));
  if (a_is_undef && b_is_undef) {
    result = 1;
    goto done;
  }

  /* Check if types match first */
  if (itype(a) != itype(b)) {
    /* Different types - but handle int/num case */
    if (tvisnumber(a) && tvisnumber(b)) {
      lua_Number na = numberVnum(a);
      lua_Number nb = numberVnum(b);
      /* NaN !== NaN */
      result = (na == nb) && (na == na);
    } else {
      result = 0;
    }
    goto done;
  }

  /* Same type comparison */
  if (tvisnumber(a)) {
    lua_Number na = numberVnum(a);
    lua_Number nb = numberVnum(b);
    /* NaN !== NaN */
    result = (na == nb) && (na == na);
  } else if (tvisstr(a)) {
    /* Strings are interned, so pointer comparison works */
    result = (strV(a) == strV(b));
  } else if (tvistrue(a) || tvisfalse(a)) {
    /* Same boolean type already checked by itype comparison */
    result = 1;
  } else if (tvisnil(a)) {
    /* undefined === undefined */
    result = 1;
  } else if (tvistab(a)) {
    /* Tables: same reference only */
    result = (tabV(a) == tabV(b));
  } else if (tvisfunc(a)) {
    /* Functions: same reference only */
    result = (funcV(a) == funcV(b));
  } else {
    /* Other types: use raw equality */
    result = lj_obj_equal(a, b);
  }

done:
  lua_pushboolean(L, result);
  return 1;
}

/*
** nseq - JavaScript strict inequality (!==)
**
** Returns true if values have different types OR different values.
** No type coercion is performed.
*/
LJLIB_CF(js_nseq)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);
  int result = 1;

  /* Check if both are undefined (nil or undefined sentinel) */
  /* This handles the case where missing args (nil) are compared to undefined sentinel */
  int a_is_undef = tvisnil(a) || (tvistab(a) && is_undefined(L, tabV(a)));
  int b_is_undef = tvisnil(b) || (tvistab(b) && is_undefined(L, tabV(b)));
  if (a_is_undef && b_is_undef) {
    result = 0;  /* undefined !== undefined is false */
    goto done;
  }

  /* Check if types match first */
  if (itype(a) != itype(b)) {
    /* Different types - but handle int/num case */
    if (tvisnumber(a) && tvisnumber(b)) {
      lua_Number na = numberVnum(a);
      lua_Number nb = numberVnum(b);
      /* NaN !== NaN is true */
      result = (na != nb) || (na != na);
    } else {
      result = 1;
    }
    goto done;
  }

  /* Same type comparison */
  if (tvisnumber(a)) {
    lua_Number na = numberVnum(a);
    lua_Number nb = numberVnum(b);
    /* NaN !== NaN is true */
    result = (na != nb) || (na != na);
  } else if (tvisstr(a)) {
    result = (strV(a) != strV(b));
  } else if (tvistrue(a) || tvisfalse(a)) {
    result = 0;
  } else if (tvisnil(a)) {
    result = 0;
  } else if (tvistab(a)) {
    result = (tabV(a) != tabV(b));
  } else if (tvisfunc(a)) {
    result = (funcV(a) != funcV(b));
  } else {
    result = !lj_obj_equal(a, b);
  }

done:
  lua_pushboolean(L, result);
  return 1;
}

/*
** ushr - JavaScript unsigned right shift (a >>> b)
**
** Converts a to unsigned 32-bit, shifts right by (b & 31), returns unsigned result.
** Result is always a non-negative number in range [0, 2^32).
*/
LJLIB_CF(js_ushr)		LJLIB_REC(.)
{
  TValue *a = lj_lib_checkany(L, 1);
  TValue *b = lj_lib_checkany(L, 2);

  /* Convert to 32-bit integers */
  int32_t ia, ib;
  if (tvisnumber(a)) {
    ia = (int32_t)numberVnum(a);
  } else {
    ia = (int32_t)js_toNumber_impl(L, a);
  }
  if (tvisnumber(b)) {
    ib = (int32_t)numberVnum(b);
  } else {
    ib = (int32_t)js_toNumber_impl(L, b);
  }

  /* Unsigned right shift: treat ia as unsigned, shift by (ib & 31) */
  uint32_t ua = (uint32_t)ia;
  uint32_t shift = (uint32_t)ib & 31;
  uint32_t result = ua >> shift;

  /* Return as Lua number (always non-negative) */
  lua_pushnumber(L, (lua_Number)result);
  return 1;
}

/*
** typeof - JavaScript typeof operator
**
** Returns a string representing the type of the value:
** - "undefined" for undefined sentinel or nil
** - "object" for null (JS quirk) and tables
** - "boolean" for true/false
** - "number" for numbers
** - "string" for strings
** - "function" for functions
*/
LJLIB_CF(js_typeof)	LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);

  if (tvisnil(o)) {
    lua_pushliteral(L, "undefined");
  } else if (tvistrue(o) || tvisfalse(o)) {
    lua_pushliteral(L, "boolean");
  } else if (tvisnumber(o)) {
    lua_pushliteral(L, "number");
  } else if (tvisstr(o)) {
    lua_pushliteral(L, "string");
  } else if (tvisfunc(o)) {
    lua_pushliteral(L, "function");
  } else if (tvistab(o)) {
    GCtab *t = tabV(o);
    /* Check for undefined sentinel */
    if (is_undefined(L, t)) {
      lua_pushliteral(L, "undefined");
    } else {
      /* Note: typeof null === "object" in JavaScript */
      lua_pushliteral(L, "object");
    }
  } else {
    /* Catch-all for other types (shouldn't normally happen in JS) */
    lua_pushliteral(L, "object");
  }

  return 1;
}

/*
** isNullish - Check if value is null or undefined
**
** Returns true if val is null, undefined sentinel, or nil.
*/
LJLIB_CF(js_isNullish)		LJLIB_REC(.)
{
  TValue *o = lj_lib_checkany(L, 1);
  int result = 0;

  if (tvisnil(o)) {
    result = 1;
  }
  else if (tvistab(o)) {
    GCtab *t = tabV(o);
    if (is_null(L, t) || is_undefined(L, t)) {
      result = 1;
    }
  }

  lua_pushboolean(L, result);
  return 1;
}

/* -- Array operations ----------------------------------------------------- */

/* Helper: Check if value is a valid array index (non-negative integer) */
static int is_array_index(TValue *tv, lua_Number *out_idx)
{
  if (tvisnumber(tv)) {
    lua_Number n = numberVnum(tv);
    if (n >= 0 && n == (lua_Number)(int64_t)n) {
      *out_idx = n;
      return 1;
    }
  } else if (tvisstr(tv)) {
    GCstr *s = strV(tv);
    TValue tmp;
    if (lj_strscan_number(s, &tmp)) {
      lua_Number n = numberVnum(&tmp);
      /* Check it's a non-negative integer and string representation matches */
      if (n >= 0 && n == (lua_Number)(int64_t)n) {
        /* Verify string is canonical (e.g., "0" not "00" or "0.0") */
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%.0f", n);
        if ((size_t)len == s->len && memcmp(buf, strdata(s), len) == 0) {
          *out_idx = n;
          return 1;
        }
      }
    }
  }
  return 0;
}

/*
** getElem - Get array/object element with JS semantics
**
** For valid array indices (non-negative integers or numeric strings),
** uses 1-indexed internal storage for LuaJIT optimization.
** For other keys, uses direct access.
**
** Assembly fast path handles: table + non-negative integer index in array bounds.
** Fallback handles: string keys, string indices, hash part lookups.
*/
LJLIB_ASM(js_getElem)		LJLIB_REC(.)
{
  GCtab *t = lj_lib_checktab(L, 1);
  TValue *idx = lj_lib_checkany(L, 2);
  lua_Number array_idx;

  if (is_array_index(idx, &array_idx)) {
    /* Array access: use 1-indexed storage */
    TValue key;
    setnumV(&key, array_idx + 1);
    cTValue *v = lj_tab_get(L, t, &key);
    if (v) {
      copyTV(L, L->base-1-LJ_FR2, v);
    } else {
      setnilV(L->base-1-LJ_FR2);
    }
  } else {
    /* Hash access: use key directly */
    cTValue *v = lj_tab_get(L, t, idx);
    if (v) {
      copyTV(L, L->base-1-LJ_FR2, v);
    } else {
      setnilV(L->base-1-LJ_FR2);
    }
  }
  return FFH_RES(1);
}

/*
** setElem - Set array/object element with JS semantics
**
** For valid array indices, uses 1-indexed internal storage and
** updates 'length' property if needed.
** For other keys, uses direct access without affecting length.
**
** Assembly fast path handles: table + non-negative integer index in array bounds.
** Fast path skips length update (assumes index < current length).
** Fallback handles: string keys, string indices, hash part, length updates.
*/
LJLIB_ASM(js_setElem)		LJLIB_REC(.)
{
  GCtab *t = lj_lib_checktab(L, 1);
  TValue *idx = lj_lib_checkany(L, 2);
  TValue *val = lj_lib_checkany(L, 3);
  lua_Number array_idx;

  if (is_array_index(idx, &array_idx)) {
    /* Array access: use 1-indexed storage */
    TValue key;
    setnumV(&key, array_idx + 1);
    TValue *slot = lj_tab_set(L, t, &key);
    copyTV(L, slot, val);
    lj_gc_anybarriert(L, t);

    /* Update length if needed */
    GCstr *length_str = lj_str_newlit(L, "length");
    cTValue *length_tv = lj_tab_getstr(t, length_str);
    if (length_tv && tvisnumber(length_tv)) {
      lua_Number len = numberVnum(length_tv);
      if (array_idx >= len) {
        TValue new_len;
        setnumV(&new_len, array_idx + 1);
        TValue *len_slot = lj_tab_setstr(L, t, length_str);
        copyTV(L, len_slot, &new_len);
      }
    }
  } else {
    /* Hash access: use key directly */
    TValue *slot = lj_tab_set(L, t, idx);
    copyTV(L, slot, val);
    lj_gc_anybarriert(L, t);
  }
  return FFH_RES(0);
}

/*
** init - Initialize the js library with null and undefined sentinels.
**
** Usage: js.init(null, undefined)
**
** This stores the sentinels in the registry so fast functions can access them.
*/
LJLIB_CF(js_init)
{
  lj_lib_checktab(L, 1);  /* null */
  lj_lib_checktab(L, 2);  /* undefined */

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
  LJ_LIB_REG(L, LUA_JSLIBNAME, js);
  return 1;
}
