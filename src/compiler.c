/* 
  Copyright (C) 2011 Rafael R. Sevilla

  This file is part of Arcueid

  Arcueid is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 3 of the
  License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#include "arcueid.h"
#include "vmengine.h"
#include "symbols.h"

/* Macro expansion.  This will look for any macro applications in e
   and attempt to expand them.

   This current implementation can only expand macros which are defined
   by global symbols.  I don't know if you should be able to define a
   macro in a local environment or even create an anonymous macro and
   apply it directly, e.g. (+ 10 ((annotate 'mac (fn () (list '+ 1
   2))).

   Note that neither reference Arc nor Anarki can handle this case, as
   it seems that macro definitions should be known at read (i.e. compile)
   time, when macros are expanded.  I don't know that anonymous or local
   macros are particularly useful either.

   Also, I don't see any other atomic types that can evaluate to
   macros, so the eval inside the reference Arc implementation becomes
   nothing but a lookup in the global symbol table here.
*/
value arc_macex(arc *c, value e)
{
  value op;

  if (!CONS_P(e))
    return(e);
  op = car(e);
  /* I don't know if it's possible to make any other type of atom evaluate
     to a macro. */
  if (!SYMBOL_P(op))
    return(e);

  /* Look up the symbol's binding in the global symbol table */
  op = arc_hash_lookup(c, c->genv, op);
  if (TYPE(op) != T_MACRO)
    return(e);			/* not a macro */
  return(arc_macapply(c, op, cdr(e)));
}

static value compile_literal(arc *c, value lit, value ctx, value cont);
static value compile_ident(arc *c, value ident, value ctx, value env,
			   value cont);
static value compile_list(arc *c, value list, value ctx, value env,
			  value cont);
static value compile_continuation(arc *c, value ctx, value cont);
static int find_literal(arc *c, value ctx, value lit);

/* Given an expression nexpr, a compilation context ctx, and a continuation
   flag, return the compilation context after the expression is compiled.
   NOTE: all macros must be fully expanded before compiling! */
value arc_compile(arc *c, value nexpr, value ctx, value env, value cont)
{
  value expr;

  expr = arc_macex(c, nexpr);
  if (expr == ARC_BUILTIN(c, S_T) || expr == ARC_BUILTIN(c, S_NIL)
      || expr == CNIL || expr == CTRUE
      || TYPE(expr) == T_CHAR || TYPE(expr) == T_STRING
      || TYPE(expr) == T_FIXNUM || TYPE(expr) == T_BIGNUM
      || TYPE(expr) == T_FLONUM || TYPE(expr) == T_RATIONAL
      || TYPE(expr) == T_RATIONAL || TYPE(expr) == T_COMPLEX)
    return(compile_literal(c, expr, ctx, cont));
  if (SYMBOL_P(expr))
    return(compile_ident(c, expr, ctx, env, cont));
  if (CONS_P(expr))
    return(compile_list(c, expr, ctx, env, cont));
  c->signal_error(c, "invalid_expression %p", expr);
  return(ctx);
}

static value compile_continuation(arc *c, value ctx, value cont)
{
  if (cont != CNIL)
    arc_gcode(c, ctx, iret);
  return(ctx);
}

/* Find a literal lit in ctx.  If not found, create it and add to the
   literals in the ctx. */
static int find_literal(arc *c, value ctx, value lit)
{
  value lits;
  int i;

  lits = CCTX_LITS(ctx);
  if (lits != CNIL) {
    for (i=0; i<VECLEN(lits); i++) {
      if (arc_iso(c, VINDEX(lits, i), lit))
	return(i);
    }
  }
  /* create the literal since it doesn't exist */
  return(arc_literal(c, ctx, lit));
}

static value compile_literal(arc *c, value lit, value ctx, value cont)
{
  if (lit == ARC_BUILTIN(c, S_NIL) || lit == CNIL) {
    arc_gcode(c, ctx, inil);
  } else if (lit == ARC_BUILTIN(c, S_T) || lit == CTRUE) {
    arc_gcode(c, ctx, itrue);
  } else if (FIXNUM_P(lit)) {
    arc_gcode1(c, ctx, ildi, lit);
  } else {
    arc_gcode1(c, ctx, ildl, find_literal(c, ctx, lit));
  }
  return(compile_continuation(c, ctx, cont));
}

/* Find the symbol var in the environment env.  Each environment frame
   is simply a list of hash tables, each hash table key being a symbol
   name, and each value being the index inside the environment frame.
   Returns CNIL if var is a name unbound in the current environment.
   Returns CTRUE otherwise, and sets frameno to the frame number of the
   environment, and idx to the index in that environment. */
static value find_var(arc *c, value var, value env, int *frameno, int *idx)
{
  value vidx;
  int fnum;

  for (fnum=0; env; env = cdr(env), fnum++) {
    if ((vidx = arc_hash_lookup(c, car(env), var)) != CUNBOUND) {
      *frameno = fnum;
      *idx = FIX2INT(vidx);
      return(CTRUE);
    }
  }
  return(CNIL);
}

static value compile_ident(arc *c, value ident, value ctx, value env,
			   value cont)
{
  int level, offset;

  /* look for the variable in the environment first */
  if (find_var(c, ident, env, &level, &offset) == CTRUE) {
    arc_gcode2(c, ctx, ilde, level, offset);
  } else {
    /* If the variable is not bound in the current environment, check
       it in the global symbol table. */
    arc_gcode1(c, ctx, ildg, find_literal(c, ctx, ident));
  }
  return(compile_continuation(c, ctx, cont));
}

static value compile_if(arc *c, value args, value ctx, value env,
			value cont)
{
  int jumpaddr, jumpaddr2;

  /* If we run out of arguments, the last value becomes nil */
  if (NIL_P(args)) {
    arc_gcode(c, ctx, inil);
    return(compile_continuation(c, ctx, cont));
  }

  /* If the next is the end of the line, compile the tail end if no
     additional */
  if (NIL_P(cdr(args))) {
    arc_compile(c, car(args), ctx, env, CNIL);
    return(compile_continuation(c, ctx, cont));
  }

  /* In the final case, we have the conditional (car), the then
     portion (cadr), and the else portion (cddr). */
  /* First, compile the conditional */
  arc_compile(c, car(args), ctx, env, CNIL);
  /* this jump address will be the address of the jf instruction
     which we are about to generate.  We have to patch it with the
     address of the start of the else portion once we know it. */
  jumpaddr = FIX2INT(CCTX_VCPTR(ctx));
  /* this jf instruction has to be patched with the address of the
     else portion. */
  arc_gcode1(c, ctx, ijf, 0);
  arc_compile(c, cadr(args), ctx, env, CNIL);
  /* This jump address the address of the unconditional jump at the end.
     It should be patched after the else portion is compiled. */
  jumpaddr2 = FIX2INT(CCTX_VCPTR(ctx));
  arc_gcode1(c, ctx, ijmp, 0);
  /* patch jumpaddr so that it will jump to the address of the else
     portion which is about to be compiled */
  VINDEX(CCTX_VCODE(ctx), jumpaddr+1) = FIX2INT(CCTX_VCPTR(ctx)) - jumpaddr;
  /* the actual if portion gets compiled now */
  compile_if(c, cddr(args), ctx, env, cont);
  /* Fix the target address of the unconditional jump at the end of the
     then portion (jumpaddr2). */
  VINDEX(CCTX_VCODE(ctx), jumpaddr2+1) = FIX2INT(CCTX_VCPTR(ctx)) - jumpaddr2;
  return(compile_continuation(c, ctx, cont));
}

/* Add a new environment frame with names names to the list of
   environments env. */
value add_env_frame(arc *c, value names, value env)
{
  value envframe;
  int idx;

  envframe = arc_mkhash(c, 8);
  for (idx=0; names; names = cdr(names), idx++)
    arc_hash_insert(c, envframe, car(names), INT2FIX(idx));
  return(cons(c, envframe, env));
}

static value arglist(arc *c, value args, value ctx, value env, int *nargs)
{
  value rn = CNIL, ahd, cur, nahd;

  *nargs = 0;
  for (;;) {
    if (SYMBOL_P(car(args))) {
      /* Ordinary symbol arg.  When we see this, cons it up to the list
	 of names, and create a mvarg instruction for it. */
      rn = cons(c, car(args), rn);
      arc_gcode1(c, ctx, imvarg, (*nargs)++);
    } else if (CONS_P(car(args))) {
      /* XXX - destructuring bind or optional arg */
    }

    if (SYMBOL_P(cdr(args))) {
      /* We have a rest arg here. */
      rn = cons(c, cdr(args), rn);
      arc_gcode1(c, ctx, imvrarg, (*nargs)++);
      break;
    } else if (NIL_P(cdr(args))) {
      /* done */
      break;
    }
    /* next arg */
    args = cdr(args);
  }
  /* the names in rn are reversed.  Reverse it before returning */
  ahd = cur = rn;
  nahd = CNIL;
  while (cur != CNIL) {
    ahd = cdr(ahd);
    scdr(cur, nahd);
    nahd = cur;
    cur = ahd;
  }
  return(nahd);
}

/* generate code to set up the new environment given the arguments. 
   After producing the code to generate the new environment, which
   generally consists of an env instruction to create an environment
   of the appropriate size and mvargs/mvoargs/mvrargs to move data from
   the stack into the appropriate environment slot as well as including
   instructions to perform any destructuring binds, return the new
   environment which includes all the names specified properly ordered.
   so that a call to find-var with the new environment can find the
   names. */
static value compile_args(arc *c, value args, value ctx, value env)
{
  value names;
  int envinstaddr, nargs;

  /* just return the current environment if no args */
  if (args == CNIL)
    return(env);

  if (SYMBOL_P(args)) {
    /* If args is a single name, make an environment with a single
       name and a list containing the name of the sole argument. */
    arc_gcode1(c, ctx, ienv, 1);
    arc_gcode1(c, ctx, imvrarg, 0);
    return(add_env_frame(c, cons(c, args, CNIL), env));
  }

  if (!CONS_P(args)) {
    c->signal_error(c, "invalid fn arg %p", args);
    return(env);
  }
  envinstaddr = FIX2INT(CCTX_VCPTR(ctx));
  /* this instruction will get patched once the true number of named
     arguments has been identified. */
  arc_gcode1(c, ctx, ienv, 0);
  names = arglist(c, args, ctx, env, &nargs);
  /* patch the true number of arguments into the instruction */
  VINDEX(CCTX_VCODE(ctx), envinstaddr+1) = nargs;
  return(add_env_frame(c, names, env));
}

static value compile_fn(arc *c, value expr, value ctx, value env,
			value cont)
{
  value args, body, nctx, nenv, newcode;

  args = car(expr);
  body = cdr(expr);
  nctx = arc_mkcctx(c, INT2FIX(1), 0);
  nenv = compile_args(c, args, nctx, env);
  /* the body of a fn works as an implicit do/progn */
  for (; body; body = cdr(body))
    arc_compile(c, car(body), nctx, nenv, CNIL);
  compile_continuation(c, nctx, CTRUE);
  /* convert the new context into a code object and generate an
     instruction in the present context to load it as a literal,
     then create a closure using the code object and the current
     environment. */
  newcode = arc_cctx2code(c, nctx);
  arc_gcode1(c, ctx, ildl, find_literal(c, ctx, newcode));
  arc_gcode(c, ctx, icls);
  return(compile_continuation(c, nctx, cont));
}

static value compile_quote(arc *c, value expr, value ctx, value env,
			   value cont)
{
  return(CNIL);
}

static value compile_quasiquote(arc *c, value expr, value ctx, value env,
				value cont)
{
  return(CNIL);
}

static value compile_assign(arc *c, value expr, value ctx, value env,
			    value cont)
{
  return(CNIL);
}

static value (*spform(arc *c, value ident))(arc *, value, value, value, value)
{
  if (ARC_BUILTIN(c, S_IF) == ident)
    return(compile_if);
  if (ARC_BUILTIN(c, S_FN) == ident)
    return(compile_fn);
  if (ARC_BUILTIN(c, S_QUOTE) == ident)
    return(compile_quote);
  if (ARC_BUILTIN(c, S_QQUOTE) == ident)
    return(compile_quasiquote);
  if (ARC_BUILTIN(c, S_ASSIGN) == ident)
    return(compile_assign);
  return(NULL);
}

static value (*inline_func(arc *c, value ident))(arc *, value,
						 value, value, value)
{
  return(NULL);
}

static value compile_apply(arc *c, value expr, value ctx, value env,
			   value cont)
{
  value fname, args, ahd, nahd, cur;
  int contaddr, nargs;

  fname = car(expr);
  args = cdr(expr);

  /* to perform a function application, we first try to make a continuation.
     The address of the continuation will be computed later. */
  arc_gcode1(c, ctx, icont, 0);
  contaddr = FIX2INT(CCTX_VCPTR(ctx)) - 1;
  /* Compile the arguments in reverse order.  This will destructively
     reverse the list of arguments! */
  ahd = cur = args;
  nahd = CNIL;
  while (cur != CNIL) {
    ahd = cdr(ahd);
    scdr(cur, nahd);
    nahd = cur;
    cur = ahd;
  }

  /* Traverse the reversed arguments, compiling each */
  for (nargs = 0; nahd; nahd = cdr(nahd), nargs++) {
    arc_compile(c, car(nahd), ctx, env, CNIL);
    arc_gcode(c, ctx, ipush);
  }
  /* Compile the function name, loading it to the value reg. */
  arc_compile(c, fname, ctx, env, CNIL);
  arc_gcode1(c, ctx, iapply, nargs);
  /* fix the continuation address */
  VINDEX(CCTX_VCODE(ctx), contaddr) = FIX2INT(CCTX_VCPTR(ctx)) - contaddr + 1;
  return(compile_continuation(c, ctx, cont));  
}

static value compile_list(arc *c, value expr, value ctx, value env,
			  value cont)
{
  value (*fun)(arc *, value, value, value, value) = NULL;

  if ((fun = spform(c, car(expr))) != NULL)
    return(fun(c, cdr(expr), ctx, env, cont));
  if ((fun = inline_func(c, car(expr))) != NULL)
    return(fun(c, expr, ctx, env, cont));

  return(compile_apply(c, expr, ctx, env, cont));
}
