/* 
  Copyright (C) 2013 Rafael R. Sevilla

  This file is part of Arcueid

  Arcueid is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/
#include <check.h>
#include "../src/arcueid.h"
#include "../src/vmengine.h"
#include "../src/arith.h"
#include "../config.h"

#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#elif defined __GNUC__
#ifndef alloca
# define alloca __builtin_alloca
#endif
#elif defined _AIX
# define alloca __alloca
#elif defined _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# include <stddef.h>
void *alloca (size_t);
#endif

arc cc;
arc *c;

#define CPUSH_(val) CPUSH(thr, val)

#define XCALL(fname, ...) do {			\
    TVALR(thr) = arc_mkaff(c, fname, CNIL);	\
    TARGC(thr) = NARGS(__VA_ARGS__);		\
    FOR_EACH(CPUSH_, __VA_ARGS__);		\
    __arc_thr_trampoline(c, thr);		\
  } while (0)

/*================================= Additions involving fixnums */

START_TEST(test_add_fixnum)
{
  int i;
  value v = INT2FIX(0);
  value maxfixnum, one, negone, sum;

  for (i=1; i<=100; i++)
    v = __arc_add2(c, v, INT2FIX(i));
  fail_unless(TYPE(v) == T_FIXNUM);
  fail_unless(FIX2INT(v) == 5050);  

  maxfixnum = INT2FIX(FIXNUM_MAX);
  one = INT2FIX(1);
  negone = INT2FIX(-1);
  sum = __arc_add2(c, maxfixnum, one);
#ifdef HAVE_GMP_H
  fail_unless(TYPE(sum) == T_BIGNUM);
  fail_unless(mpz_get_si(REPBNUM(sum)) == FIXNUM_MAX + 1);

  sum = __arc_add2(c, negone, sum);
  fail_unless(TYPE(sum) == T_FIXNUM);
  fail_unless(FIX2INT(sum) == FIXNUM_MAX);
#else
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(REPFLO(sum) - (FIXNUM_MAX + 1) < 1e-6);
#endif
  /* Negative side */
  maxfixnum = INT2FIX(-FIXNUM_MAX);
  sum = __arc_add2(c, maxfixnum, negone);

#ifdef HAVE_GMP_H
  fail_unless(TYPE(sum) == T_BIGNUM);
  fail_unless(mpz_get_si(REPBNUM(sum)) == -FIXNUM_MAX - 1);
#else
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(REPFLO(sum) - (-FIXNUM_MAX - 1) < 1e-6);
#endif
}
END_TEST

#ifdef HAVE_GMP_H

START_TEST(test_add_fixnum2bignum)
{
  value bn, sum;
  char *str;

  bn = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(bn), "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", 10);
  sum = __arc_add2(c, bn, INT2FIX(1));
  fail_unless(TYPE(sum) == T_BIGNUM);
  str = alloca(mpz_sizeinbase(REPBNUM(bn), 10) + 2);
  mpz_get_str(str, 10, REPBNUM(sum));
  fail_unless(strcmp(str, "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001") == 0);
}
END_TEST

START_TEST(test_add_fixnum2rational)
{
  value v1, v2, sum;

  v1 = arc_mkrationall(c, 1, 2);
  v2 = INT2FIX(1);
  sum = __arc_add2(c, v1, v2);
  fail_unless(TYPE(sum) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(sum), 3, 2) == 0);

  sum = __arc_add2(c, v2, v1);
  fail_unless(TYPE(sum) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(sum), 3, 2) == 0);
}
END_TEST

#endif

START_TEST(test_add_fixnum2flonum)
{
  value val1, val2, sum;

  val1 = INT2FIX(1);
  val2 = arc_mkflonum(c, 3.14159);

  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(fabs(4.14159 - REPFLO(sum)) < 1e-6);

  val1 = INT2FIX(-1);
  sum = __arc_add2(c, sum, val1);
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(fabs(3.14159 - REPFLO(sum)) < 1e-6);
}
END_TEST

START_TEST(test_add_fixnum2complex)
{
  value val1, val2, sum;

  val1 = INT2FIX(1);
  val2 = arc_mkcomplex(c, 1.1 + I*2.2);

  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(2.1 - creal(REPCPX(sum))) < 1e-6);
  fail_unless(fabs(2.2 - cimag(REPCPX(sum))) < 1e-6);

  val1 = INT2FIX(-1);
  sum = __arc_add2(c, sum, val1);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(1.1 - creal(REPCPX(sum))) < 1e-6);
  fail_unless(fabs(2.2 - cimag(REPCPX(sum))) < 1e-6);
}
END_TEST

#ifdef HAVE_GMP_H

/*================================= Additions involving bignums */

START_TEST(test_add_bignum)
{
  value val1, val2, sum;
  mpz_t expected;

  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "100000000000000000000000000000", 10);
  val2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val2), "200000000000000000000000000000", 10);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "300000000000000000000000000000", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(sum)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_add_bignum2rational)
{
  value v1, v2, sum;
  mpq_t expected;

  v1 = arc_mkrationall(c, 1, 2);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "100000000000000000000000000000", 10);
  sum = __arc_add2(c, v1, v2);
  fail_unless(TYPE(sum) == T_RATIONAL);
  mpq_init(expected);
  mpq_set_str(expected, "200000000000000000000000000001/2", 10);
  fail_unless(mpq_cmp(expected, REPRAT(sum)) == 0);
  mpq_clear(expected);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "100000000000000000000000000000", 10);
  v2 = arc_mkrationall(c, 1, 2);
  sum = __arc_add2(c, v1, v2);
  fail_unless(TYPE(sum) == T_RATIONAL);
  mpq_init(expected);
  mpq_set_str(expected, "200000000000000000000000000001/2", 10);
  fail_unless(mpq_cmp(expected, REPRAT(sum)) == 0);
  mpq_clear(expected);
}
END_TEST

START_TEST(test_add_bignum2flonum)
{
  value v1, v2, sum;

  v1 = arc_mkflonum(c, 0.0);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "10000000000000000000000", 10);
  sum = __arc_add2(c, v1, v2);
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(fabs(REPFLO(sum) - 1e22) < 1e-6);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "10000000000000000000000", 10);
  v2 = arc_mkflonum(c, 0.0);
  sum = __arc_add2(c, v1, v2);
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(fabs(REPFLO(sum) - 1e22) < 1e-6);
}
END_TEST

START_TEST(test_add_bignum2complex)
{
  value v1, v2, sum;

  v1 = arc_mkcomplex(c, 0.0 + I*1.1);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "10000000000000000000000", 10);
  sum = __arc_add2(c, v1, v2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(sum)) - 1e22) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(sum)) - 1.1) < 1e-6);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "10000000000000000000000", 10);
  v2 = arc_mkcomplex(c, 0.0 + I*1.1);
  sum = __arc_add2(c, v1, v2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(sum)) - 1e22) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(sum)) - 1.1) < 1e-6);
}
END_TEST

START_TEST(test_add_rational)
{
  value val1, val2, sum;
  mpz_t expected;

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkrationall(c, 1, 4);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(sum), 3, 4) == 0);

  /* Conversions of rationals to integer types when appropriate */
  val1 = sum;
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_FIXNUM);
  fail_unless(FIX2INT(sum) == 1);

  val1 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(val1), "1606938044258990275541962092341162602522202993782792835301375/4", 10);
  val2 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(val2), "1/4", 10);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "401734511064747568885490523085290650630550748445698208825344", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(sum)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_add_rational2flonum)
{
  value val1, val2, sum;

  val1 = arc_mkflonum(c, 0.5);
  val2 = arc_mkrationall(c, 1, 2);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(fabs(1.0 - REPFLO(sum)) < 1e-6);

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkflonum(c, 0.5);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(fabs(1.0 - REPFLO(sum)) < 1e-6);
}
END_TEST

START_TEST(test_add_rational2complex)
{
  value val1, val2, sum;

  val1 = arc_mkcomplex(c, 0.5 + I*0.5);
  val2 = arc_mkrationall(c, 1, 2);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(1.0 - creal(REPCPX(sum))) < 1e-6);
  fail_unless(fabs(0.5 - cimag(REPCPX(sum))) < 1e-6);

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkcomplex(c, 0.5 + I*0.5);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(1.0 - creal(REPCPX(sum))) < 1e-6);
  fail_unless(fabs(0.5 - cimag(REPCPX(sum))) < 1e-6);
}
END_TEST

#endif

START_TEST(test_add_flonum)
{
  value val1, val2, sum;

  val1 = arc_mkflonum(c, 2.71828);
  val2 = arc_mkflonum(c, 3.14159);

  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_FLONUM);
  fail_unless(fabs(5.85987 - REPFLO(sum)) < 1e-6);
}
END_TEST

START_TEST(test_add_flonum2complex)
{
  value val1, val2, sum;

  val1 = arc_mkflonum(c, 0.5);
  val2 = arc_mkcomplex(c, 3 - I*4);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(3.5 - creal(REPCPX(sum))) < 1e-6);
  fail_unless(fabs(-4 - cimag(REPCPX(sum))) < 1e-6);

  val1 = arc_mkcomplex(c, 3 - I*4);
  val2 = arc_mkflonum(c, 0.5);
  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(3.5 - creal(REPCPX(sum))) < 1e-6);
  fail_unless(fabs(-4 - cimag(REPCPX(sum))) < 1e-6);
}
END_TEST

START_TEST(test_add_complex)
{
  value val1, val2, sum;

  val1 = arc_mkcomplex(c, 1 - I*2);
  val2 = arc_mkcomplex(c, 3 - I*4);

  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_COMPLEX);
  fail_unless(fabs(4 - creal(REPCPX(sum))) < 1e-6);
  fail_unless(fabs(-6 - cimag(REPCPX(sum))) < 1e-6);
}
END_TEST

START_TEST(test_add_cons)
{
  value val1, val2, sum;

  val1 = cons(c, INT2FIX(1), cons(c, INT2FIX(2), CNIL));
  val2 = cons(c, INT2FIX(3), cons(c, INT2FIX(4), CNIL));

  sum = __arc_add2(c, val1, CNIL);
  fail_unless(TYPE(sum) == T_CONS);
  fail_unless(car(sum) == INT2FIX(1));
  fail_unless(cadr(sum) == INT2FIX(2));
  fail_unless(NIL_P(cddr(sum)));

  sum = __arc_add2(c, CNIL, val2);
  fail_unless(TYPE(sum) == T_CONS);
  fail_unless(car(sum) == INT2FIX(3));
  fail_unless(cadr(sum) == INT2FIX(4));
  fail_unless(NIL_P(cddr(sum)));

  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_CONS);
  fail_unless(car(sum) == INT2FIX(1));
  fail_unless(cadr(sum) == INT2FIX(2));
  fail_unless(car(cddr(sum)) == INT2FIX(3));
  fail_unless(cadr(cddr(sum)) == INT2FIX(4));
  fail_unless(NIL_P(cddr(cddr(sum))));
}
END_TEST

START_TEST(test_add_string)
{
  value val1, val2, sum;

  val1 = arc_mkstringc(c, "foo");
  val2 = arc_mkstringc(c, "bar");

  sum = __arc_add2(c, val1, CNIL);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, val1) == 0);

  sum = __arc_add2(c, CNIL, val2);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, val2) == 0);

  sum = __arc_add2(c, val1, val2);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, arc_mkstringc(c, "foobar")) == 0);
}
END_TEST

START_TEST(test_add_string2char)
{
  value ch1, ch2, str, sum;

  ch1 = arc_mkchar(c, 0x9060);
  ch2 = arc_mkchar(c, 0x91ce);
  sum = __arc_add2(c, ch1, CNIL);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, arc_mkstringc(c, "遠")) == 0);

  sum = __arc_add2(c, CNIL, ch2);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, arc_mkstringc(c, "野")) == 0);

  sum = __arc_add2(c, ch1, ch2);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, arc_mkstringc(c, "遠野")) == 0);

  str = arc_mkstringc(c, "遠");
  sum = __arc_add2(c, str, ch2);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, arc_mkstringc(c, "遠野")) == 0);

  str = arc_mkstringc(c, "野");
  sum = __arc_add2(c, ch1, str);
  fail_unless(TYPE(sum) == T_STRING);
  fail_unless(arc_strcmp(c, sum, arc_mkstringc(c, "遠野")) == 0);
}
END_TEST

/*================================= End of Addition Tests */

/*================================= Subtractions involving fixnums */

START_TEST(test_sub_fixnum)
{
  int i;
  value v = INT2FIX(0);
  value maxfixnum, one, negone, diff;

  for (i=1; i<=100; i++)
    v = __arc_sub2(c, v, INT2FIX(i));
  fail_unless(TYPE(v) == T_FIXNUM);
  fail_unless(FIX2INT(v) == -5050);

  maxfixnum = INT2FIX(-FIXNUM_MAX);
  one = INT2FIX(1);
  negone = INT2FIX(-1);
  diff = __arc_sub2(c, maxfixnum, one);
#ifdef HAVE_GMP_H
  fail_unless(TYPE(diff) == T_BIGNUM);
  fail_unless(mpz_get_si(REPBNUM(diff)) == -FIXNUM_MAX - 1);
#else
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(REPFLO(diff) - ((double)(-FIXNUM_MAX - 1)) < 1e-6);
#endif

  diff = __arc_sub2(c, negone, diff);

#ifdef HAVE_GMP_H
  fail_unless(TYPE(diff) == T_FIXNUM);
  fail_unless(FIX2INT(diff) == FIXNUM_MAX);
#else
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(REPFLO(diff) - ((double)FIXNUM_MAX) < 1e-6);
#endif
}
END_TEST

#ifdef HAVE_GMP_H

START_TEST(test_sub_fixnum2bignum)
{
  value bn, sum;
  char *str;

  bn = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(bn), "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001", 10);
  sum = __arc_sub2(c, bn, INT2FIX(1));
  fail_unless(TYPE(sum) == T_BIGNUM);
  str = alloca(mpz_sizeinbase(REPBNUM(bn), 10) + 2);
  mpz_get_str(str, 10, REPBNUM(sum));
  fail_unless(strcmp(str, "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000") == 0);
}
END_TEST

START_TEST(test_sub_fixnum2rational)
{
  value v1, v2, diff;

  v1 = arc_mkrationall(c, 1, 2);
  v2 = INT2FIX(1);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(diff), -1, 2) == 0);

  v1 = INT2FIX(1);
  v2 = arc_mkrationall(c, 1, 2);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(diff), 1, 2) == 0);
}
END_TEST

#endif

START_TEST(test_sub_fixnum2flonum)
{
  value val1, val2, diff;

  val1 = INT2FIX(1);
  val2 = arc_mkflonum(c, 4.14159);

  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(fabs(-3.14159 - REPFLO(diff)) < 1e-6);

  val1 = INT2FIX(-1);
  diff = __arc_sub2(c, diff, val1);
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(fabs(-2.14159 - REPFLO(diff)) < 1e-6);
}
END_TEST

START_TEST(test_sub_fixnum2complex)
{
  value val1, val2, diff;

  val1 = INT2FIX(1);
  val2 = arc_mkcomplex(c, 1.1 + I*2.2);

  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(-0.1 - creal(REPCPX(diff))) < 1e-6);
  fail_unless(fabs(-2.2 - cimag(REPCPX(diff))) < 1e-6);

  val1 = INT2FIX(-1);
  diff = __arc_sub2(c, diff, val1);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(0.9 - creal(REPCPX(diff))) < 1e-6);
  fail_unless(fabs(-2.2 - cimag(REPCPX(diff))) < 1e-6);
}
END_TEST

#ifdef HAVE_GMP_H

START_TEST(test_sub_bignum)
{
  value val1, val2, diff;
  mpz_t expected;

  val1 = arc_mkbignuml(c, FIXNUM_MAX+1);
  val2 = arc_mkbignuml(c, FIXNUM_MAX+2);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_FIXNUM);
  fail_unless(FIX2INT(diff) == -1);

  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "300000000000000000000000000000", 10);
  val2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val2), "100000000000000000000000000000", 10);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "200000000000000000000000000000", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(diff)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_sub_bignum2rational)
{
  value v1, v2, diff;
  mpq_t expected;

  v1 = arc_mkrationall(c, 1, 2);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "100000000000000000000000000000", 10);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_RATIONAL);
  mpq_init(expected);
  mpq_set_str(expected, "-199999999999999999999999999999/2", 10);
  fail_unless(mpq_cmp(expected, REPRAT(diff)) == 0);
  mpq_clear(expected);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "100000000000000000000000000000", 10);
  v2 = arc_mkrationall(c, 1, 2);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_RATIONAL);
  mpq_init(expected);
  mpq_set_str(expected, "199999999999999999999999999999/2", 10);
  fail_unless(mpq_cmp(expected, REPRAT(diff)) == 0);
  mpq_clear(expected);
}
END_TEST

START_TEST(test_sub_bignum2flonum)
{
  value v1, v2, diff;

  v1 = arc_mkflonum(c, 0.0);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "10000000000000000000000", 10);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(fabs(REPFLO(diff) + 1e22) < 1e-6);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "10000000000000000000000", 10);
  v2 = arc_mkflonum(c, 0.0);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(fabs(REPFLO(diff) - 1e22) < 1e-6);
}
END_TEST

START_TEST(test_sub_bignum2complex)
{
  value v1, v2, diff;

  v1 = arc_mkcomplex(c, 0.0 + I*1.1);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "10000000000000000000000", 10);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(diff)) + 1e22) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(diff)) - 1.1) < 1e-6);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "10000000000000000000000", 10);
  v2 = arc_mkcomplex(c, 0.0 + I*1.1);
  diff = __arc_sub2(c, v1, v2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(diff)) - 1e22) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(diff)) + 1.1) < 1e-6);
}
END_TEST

/*================================= Subtractions involving rationals */
START_TEST(test_sub_rational)
{
  value val1, val2, diff;
  mpz_t expected;

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkrationall(c, 1, 4);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(diff), 1, 4) == 0);

  val1 = diff;
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_FIXNUM);
  fail_unless(FIX2INT(diff) == 0);

  val1 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(val1), "1606938044258990275541962092341162602522202993782792835301375/4", 10);
  val2 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(val2), "3/4", 10);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "401734511064747568885490523085290650630550748445698208825343", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(diff)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_sub_rational2flonum)
{
  value val1, val2, diff;

  val1 = arc_mkflonum(c, 0.5);
  val2 = arc_mkrationall(c, 1, 2);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(fabs(REPFLO(diff)) < 1e-6);

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkflonum(c, 0.5);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(fabs(REPFLO(diff)) < 1e-6);
}
END_TEST

START_TEST(test_sub_rational2complex)
{
  value val1, val2, diff;

  val1 = arc_mkcomplex(c, 0.5 + I*0.5);
  val2 = arc_mkrationall(c, 1, 2);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(diff))) < 1e-6);
  fail_unless(fabs(0.5 - cimag(REPCPX(diff))) < 1e-6);

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkcomplex(c, 0.5 + I*0.5);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(diff))) < 1e-6);
  fail_unless(fabs(-0.5 - cimag(REPCPX(diff))) < 1e-6);
}
END_TEST

#endif

/*================================= Subtractions involving flonums */

START_TEST(test_sub_flonum)
{
  value val1, val2, diff;

  val1 = arc_mkflonum(c, 2.71828);
  val2 = arc_mkflonum(c, 3.14159);

  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_FLONUM);
  fail_unless(fabs(-0.42331 - REPFLO(diff)) < 1e-6);
}
END_TEST

START_TEST(test_sub_flonum2complex)
{
  value val1, val2, diff;

  val1 = arc_mkflonum(c, 0.5);
  val2 = arc_mkcomplex(c, 3 - I*4);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(-2.5 - creal(REPCPX(diff))) < 1e-6);
  fail_unless(fabs(4 - cimag(REPCPX(diff))) < 1e-6);

  val1 = arc_mkcomplex(c, 3 - I*4);
  val2 = arc_mkflonum(c, 0.5);
  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(2.5 - creal(REPCPX(diff))) < 1e-6);
  fail_unless(fabs(-4 - cimag(REPCPX(diff))) < 1e-6);

}
END_TEST

START_TEST(test_sub_complex)
{
  value val1, val2, diff;

  val1 = arc_mkcomplex(c, 1 - I*2);
  val2 = arc_mkcomplex(c, 3 - I*4);

  diff = __arc_sub2(c, val1, val2);
  fail_unless(TYPE(diff) == T_COMPLEX);
  fail_unless(fabs(-2 - creal(REPCPX(diff))) < 1e-6);
  fail_unless(fabs(2 - cimag(REPCPX(diff))) < 1e-6);
}
END_TEST

/*================================= End of Subtraction Tests */

/*================================= Multiplications involving fixnums */

START_TEST(test_mul_fixnum)
{
  value prod;
#ifdef HAVE_GMP_H
  mpz_t expected;
#endif

  prod = __arc_mul2(c, INT2FIX(8), INT2FIX(21));
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == 168);

  prod = __arc_mul2(c, INT2FIX(-8), INT2FIX(-21));
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == 168);

  prod = __arc_mul2(c, INT2FIX(-8), INT2FIX(21));
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == -168);

  prod = __arc_mul2(c, INT2FIX(8), INT2FIX(-21));
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == -168);

  prod = __arc_mul2(c, INT2FIX(0xdeadbeef), INT2FIX(1));
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == 0xdeadbeef);

#ifdef HAVE_GMP_H
  prod = __arc_mul2(c, INT2FIX(2), INT2FIX(FIXNUM_MAX));
  fail_unless(TYPE(prod) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_si(expected, 2*FIXNUM_MAX);
  fail_unless(mpz_cmp(expected, REPBNUM(prod)) == 0);

  prod = __arc_mul2(c, INT2FIX(-2), INT2FIX(-FIXNUM_MAX));
  fail_unless(TYPE(prod) == T_BIGNUM);
  mpz_set_si(expected, 2*FIXNUM_MAX);
  fail_unless(mpz_cmp(expected, REPBNUM(prod)) == 0);

  prod = __arc_mul2(c, INT2FIX(-2), INT2FIX(FIXNUM_MAX));
  fail_unless(TYPE(prod) == T_BIGNUM);
  mpz_set_si(expected, -2*FIXNUM_MAX);
  fail_unless(mpz_cmp(expected, REPBNUM(prod)) == 0);

  prod = __arc_mul2(c, INT2FIX(2), INT2FIX(-FIXNUM_MAX));
  fail_unless(TYPE(prod) == T_BIGNUM);
  mpz_set_si(expected, -2*FIXNUM_MAX);
  fail_unless(mpz_cmp(expected, REPBNUM(prod)) == 0);
  mpz_clear(expected);
#else

  prod = __arc_mul2(c, INT2FIX(2), FIXNUM_MAX);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(REPFLO(prod) - 2*FIXNUM_MAX < 1e-6);

  prod = __arc_mul2(c, INT2FIX(-2), INT2FIX(-FIXNUM_MAX));
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(REPFLO(prod) - 2*FIXNUM_MAX < 1e-6);

  prod = __arc_mul2(c, INT2FIX(-2), INT2FIX(FIXNUM_MAX));
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(REPFLO(prod) - -2*FIXNUM_MAX < 1e-6);

  prod = __arc_mul2(c, INT2FIX(2), INT2FIX(-FIXNUM_MAX));
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(REPFLO(prod) - -2*FIXNUM_MAX < 1e-6);
#endif

}
END_TEST

#ifdef HAVE_GMP_H

START_TEST(test_mul_fixnum2bignum)
{
  value factorial;
  mpz_t expected;
  int i;

  factorial = INT2FIX(1);
  for (i=1; i<=100; i++)
    factorial = __arc_mul2(c, INT2FIX(i), factorial);

  mpz_init(expected);
  mpz_set_str(expected, "93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(factorial)) == 0);
  mpz_clear(expected);

  factorial = INT2FIX(1);
  for (i=1; i<=100; i++)
    factorial = __arc_mul2(c, factorial, INT2FIX(i));

  /* multiply from the other side */
  mpz_init(expected);
  mpz_set_str(expected, "93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(factorial)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_mul_fixnum2rational)
{
  value v1, v2, prod;

  v1 = arc_mkrationall(c, 1, 2);
  v2 = INT2FIX(3);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(prod), 3, 2) == 0);

  v1 = INT2FIX(3);
  v2 = arc_mkrationall(c, 1, 2);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(prod), 3, 2) == 0);

  v1 = INT2FIX(2);
  v2 = arc_mkrationall(c, 1, 2);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == 1);
}
END_TEST

#endif

START_TEST(test_mul_fixnum2flonum)
{
  value val1, val2, prod;

  val1 = INT2FIX(2);
  val2 = arc_mkflonum(c, 3.14159);

  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(fabs(6.28318 - REPFLO(prod)) < 1e-6);

  val1 = INT2FIX(3);
  prod = __arc_mul2(c, prod, val1);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(fabs(18.84954 - REPFLO(prod)) < 1e-6);
}
END_TEST

START_TEST(test_mul_fixnum2complex)
{
  value v1, v2, prod;

  v1 = arc_mkcomplex(c, 1.0 + I*2.0);
  v2 = INT2FIX(3);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(3.0 - creal(REPCPX(prod))) < 1e-6);
  fail_unless(fabs(6.0 - cimag(REPCPX(prod))) < 1e-6);

  v1 = INT2FIX(3);
  v1 = arc_mkcomplex(c, 1.0 + I*2.0);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(fabs(3.0 - creal(REPCPX(prod))) < 1e-6);
  fail_unless(fabs(6.0 - cimag(REPCPX(prod))) < 1e-6);
}
END_TEST

#ifdef HAVE_GMP_H

/*================================= Multiplications involving bignums */

START_TEST(test_mul_bignum)
{
  value val1, val2, sum;
  mpz_t expected;

  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "400000000000000000000000000000", 10);
  val2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val2), "300000000000000000000000000000", 10);
  sum = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(sum) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "120000000000000000000000000000000000000000000000000000000000", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(sum)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_mul_bignum2rational)
{
  value v1, v2, prod;
  mpq_t expected;
  mpz_t zexpected;

  v1 = arc_mkrationall(c, 1, 3);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "100000000000000000000000000000", 10);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_RATIONAL);
  mpq_init(expected);
  mpq_set_str(expected, "100000000000000000000000000000/3", 10);
  fail_unless(mpq_cmp(expected, REPRAT(prod)) == 0);
  mpq_clear(expected);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "100000000000000000000000000000", 10);
  v2 = arc_mkrationall(c, 1, 3);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_RATIONAL);
  mpq_init(expected);
  mpq_set_str(expected, "100000000000000000000000000000/3", 10);
  fail_unless(mpq_cmp(expected, REPRAT(prod)) == 0);
  mpq_clear(expected);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "100000000000000000000000000000", 10);
  v2 = arc_mkrationall(c, 1, 2);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_BIGNUM);
  mpz_init(zexpected);
  mpz_set_str(zexpected, "50000000000000000000000000000", 10);
  fail_unless(mpz_cmp(zexpected, REPBNUM(prod)) == 0);
  mpz_clear(zexpected);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "100000000000000000000000000000", 10);
  v2 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(v2), "1/100000000000000000000000000000", 10);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == 1);
}
END_TEST

START_TEST(test_mul_bignum2flonum)
{
  value v1, v2, prod;

  v1 = arc_mkflonum(c, 2.0);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "100000000000000000000000000000", 10);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(fabs(REPFLO(prod) - 2e29) < 1e-6);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "100000000000000000000000000000", 10);
  v2 = arc_mkflonum(c, 2.0);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(fabs(REPFLO(prod) - 2e29) < 1e-6);
}
END_TEST

START_TEST(test_mul_bignum2complex)
{
  value v1, v2, prod;

  v1 = arc_mkcomplex(c, 1.0 + I*1.0);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "10000000000000000000000", 10);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(prod)) - 1e22) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(prod)) - 1e22) < 1e-6);

  v1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v1), "10000000000000000000000", 10);
  v2 = arc_mkcomplex(c, 1.0 + I*1.0);
  prod = __arc_mul2(c, v1, v2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(prod)) - 1e22) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(prod)) - 1e22) < 1e-6);
}
END_TEST

/*================================= Multiplications involving rationals */

START_TEST(test_mul_rational)
{
  value val1, val2, prod;
  mpz_t expected;

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkrationall(c, 1, 4);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(prod), 1, 8) == 0);

  val1 = arc_mkrationall(c, 3, 4);
  val2 = arc_mkrationall(c, 4, 3);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == 1);

  val1 = arc_mkrationall(c, 3, 4);
  val2 = arc_mkrationall(c, 4, 3);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_FIXNUM);
  fail_unless(FIX2INT(prod) == 1);

  val1 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(val1), "115792089237316195423570985008687907853269984665640564039457584007913129639936/3", 10);
  val2 = arc_mkrationall(c, 3, 4);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "28948022309329048855892746252171976963317496166410141009864396001978282409984", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(prod)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_mul_rational2flonum)
{
  value val1, val2, prod;

  val1 = arc_mkflonum(c, 2.0);
  val2 = arc_mkrationall(c, 1, 2);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(fabs(1.0 - REPFLO(prod)) < 1e-6);

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkflonum(c, 2.0);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(fabs(1.0 - REPFLO(prod)) < 1e-6);
}
END_TEST

START_TEST(test_mul_rational2complex)
{
  value val1, val2, prod;

  val1 = arc_mkcomplex(c, 0.5 + I*0.25);
  val2 = arc_mkrationall(c, 1, 2);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(0.25 - creal(REPCPX(prod))) < 1e-6);
  fail_unless(fabs(0.125 - cimag(REPCPX(prod))) < 1e-6);

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkcomplex(c, 0.5+ I*0.25);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(0.25 - creal(REPCPX(prod))) < 1e-6);
  fail_unless(fabs(0.125 - cimag(REPCPX(prod))) < 1e-6);
}
END_TEST

#endif

/*================================= Multiplications involving flonums */

START_TEST(test_mul_flonum)
{
  value val1, val2, prod;

  val1 = arc_mkflonum(c, 1.20257);
  val2 = arc_mkflonum(c, 0.57721);

  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_FLONUM);
  fail_unless(fabs(0.694135 - REPFLO(prod)) < 1e-6);
}
END_TEST

START_TEST(test_mul_flonum2complex)
{
  value val1, val2, prod;

  val1 = arc_mkflonum(c, 0.5);
  val2 = arc_mkcomplex(c, 3 - I*4);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(1.5 - creal(REPCPX(prod))) < 1e-6);
  fail_unless(fabs(-2.0 - cimag(REPCPX(prod))) < 1e-6);

  val1 = arc_mkcomplex(c, 3 - I*4);
  val2 = arc_mkflonum(c, 0.5);
  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(1.5 - creal(REPCPX(prod))) < 1e-6);
  fail_unless(fabs(-2.0 - cimag(REPCPX(prod))) < 1e-6);
}
END_TEST

/*================================= Multiplications involving complexes */

START_TEST(test_mul_complex)
{
  value val1, val2, prod;

  val1 = arc_mkcomplex(c, 2.0 + I*1.0);
  val2 = arc_mkcomplex(c, 3.0 + I*2.0);

  prod = __arc_mul2(c, val1, val2);
  fail_unless(TYPE(prod) == T_COMPLEX);
  fail_unless(fabs(4.0 - creal(REPCPX(prod))) < 1e-6);
  fail_unless(fabs(7.0 - cimag(REPCPX(prod))) < 1e-6);
}
END_TEST

/*================================= End of Multiplication Tests */

/*================================= Divisions involving fixnums */

START_TEST(test_div_fixnum)
{
  value quot;
#ifdef HAVE_GMP_H
  mpq_t expected;
#endif

  quot = __arc_div2(c, INT2FIX(168), INT2FIX(21));
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == 8);

  quot = __arc_div2(c, INT2FIX(-168), INT2FIX(21));
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == -8);

  quot = __arc_div2(c, INT2FIX(168), INT2FIX(-21));
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == -8);

  quot = __arc_div2(c, INT2FIX(-168), INT2FIX(-21));
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == 8);

  quot = __arc_div2(c, INT2FIX(1), INT2FIX(2));
#ifdef HAVE_GMP_H
  fail_unless(TYPE(quot) == T_RATIONAL);
  mpq_init(expected);
  mpq_set_str(expected, "1/2", 10);
  fail_unless(mpq_cmp(expected, REPRAT(quot)) == 0);
  mpq_clear(expected);
#else
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(REPFLO(quot) - 0.5) < 1e-6);
#endif
}
END_TEST

#ifdef HAVE_GMP_H

START_TEST(test_div_fixnum2bignum)
{
  value val1, val2, quot;
  mpz_t expected;
  mpq_t qexpected;
  int i;

  /* Bignum / Fixnum = Bignum */
  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "40000000000000000000000000000000000000000000000", 10);
  val2 = INT2FIX(2);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "20000000000000000000000000000000000000000000000", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(quot)) == 0);
  mpz_clear(expected);

  /* Bignum / Fixnum = Fixnum (eventually) */
  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "40000000000000000000000000000000000000000000000", 10);
  val2 = INT2FIX(10);
  for (i=0; i<46; i++)
    val1 = __arc_div2(c, val1, val2);
  fail_unless(TYPE(val1) == T_FIXNUM);
  fail_unless(FIX2INT(val1) == 4);

  /* Bignum / Fixnum = Rational */
  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "40000000000000000000000000000000000000000000000",
	      10);
  val2 = INT2FIX(3);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_RATIONAL);
  mpq_init(qexpected);
  mpq_set_str(qexpected, "40000000000000000000000000000000000000000000000/3",
	      10);
  fail_unless(mpq_cmp(qexpected, REPRAT(quot)) == 0);

  /* Fixnum / "Bignum" = Fixnum (somewhat contrived) */
  val1 = INT2FIX(100);
  val2 = arc_mkbignuml(c, 10);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == 10);

  /* Fixnum / Bignum = Rational */
  val1 = INT2FIX(3);
  val2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val2), "40000000000000000000000000000000000000000000000",
	      10);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_RATIONAL);
  mpq_init(qexpected);
  mpq_set_str(qexpected, "3/40000000000000000000000000000000000000000000000",
	      10);
  fail_unless(mpq_cmp(qexpected, REPRAT(quot)) == 0);
}
END_TEST

START_TEST(test_div_fixnum2rational)
{
  value v1, v2, quot;

  v1 = arc_mkrationall(c, 1, 2);
  v2 = INT2FIX(3);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(quot), 1, 6) == 0);

  v1 = INT2FIX(3);
  v2 = arc_mkrationall(c, 5, 2);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(quot), 6, 5) == 0);

  v1 = INT2FIX(3);
  v2 = arc_mkrationall(c, 3, 2);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == 2);
}
END_TEST

#endif

START_TEST(test_div_fixnum2flonum)
{
  value val1, val2, quot;

  val1 = INT2FIX(2);
  val2 = arc_mkflonum(c, 3.14159);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(0.63662031 - REPFLO(quot)) < 1e-6);

  val1 = INT2FIX(2);
  quot = __arc_div2(c, quot, val1);
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(0.3183101 - REPFLO(quot)) < 1e-6);
}
END_TEST

START_TEST(test_div_fixnum2complex)
{
  value v1, v2, quot;

  v1 = arc_mkcomplex(c, 1.0 + I*2.0);
  v2 = INT2FIX(4);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(0.25 - creal(REPCPX(quot))) < 1e-6);
  fail_unless(fabs(0.50 - cimag(REPCPX(quot))) < 1e-6);

  v1 = INT2FIX(2);
  v2 = arc_mkcomplex(c, 1.0 + I*2.0);
  quot = __arc_div2(c, v1, v2);
  fail_unless(fabs(0.4 - creal(REPCPX(quot))) < 1e-6);
  fail_unless(fabs(-0.8 - cimag(REPCPX(quot))) < 1e-6);
}
END_TEST

#ifdef HAVE_GMP_H

/*================================= Divisions involving bignums */
START_TEST(test_div_bignum)
{
  value val1, val2, quot;
  mpz_t expected;

  /* Bignum result */
  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "40000000000000000000000000000000000000000000000", 10);
  val2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val2), "20000000000000000000000", 10);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "2000000000000000000000000", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(quot)) == 0);
  mpz_clear(expected);

  /* Fixnum result */
  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "40000000000000000000000000000000000000000000000", 10);
  val2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val2), "20000000000000000000000000000000000000000000000", 10);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == 2);

  /* Rational result */
  val1 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val1), "40000000000000000000000000000000000000000000000", 10);
  val2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(val2), "30000000000000000000000000000000000000000000000", 10);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(quot), 4, 3) == 0);

}
END_TEST

START_TEST(test_div_bignum2rational)
{
  value v1, v2, quot;
  value expected;

  /* rational result */
  v1 = arc_mkrationall(c, 1, 2);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "40000000000000000000000000000000000000000000000", 10);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_RATIONAL);
  expected = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(expected), "1/80000000000000000000000000000000000000000000000", 10);
  fail_unless(arc_is2(c, expected, quot) == CTRUE);

  /* bignum result */
  quot = __arc_div2(c, v2, v1);
  fail_unless(TYPE(quot) == T_BIGNUM);
  expected = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(expected), "80000000000000000000000000000000000000000000000", 10);
  fail_unless(arc_is2(c, expected, quot) == CTRUE);

  /* fixnum result */
  v1 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(v1), "1000000000000000000000000000000000000000000/2", 10);
  quot = __arc_div2(c, v2, v1);
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(quot == INT2FIX(80000));
}
END_TEST

START_TEST(test_div_bignum2flonum)
{
  value v1, v2, quot;

  /* rational result */
  v1 = arc_mkflonum(c, 0.5);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "1000000000000000000000000", 10);
  quot = __arc_div2(c, v2, v1);
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(REPFLO(quot) - 2e24) < 1e-6);

  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(REPFLO(quot) - 5e-25) < 1e-6);
}
END_TEST

START_TEST(test_div_bignum2complex)
{
  value v1, v2, quot;

  v1 = arc_mkcomplex(c, 2.0+I*5.0);
  v2 = arc_mkbignuml(c, 0);
  mpz_set_str(REPBNUM(v2), "1000000000000000000000000", 10);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(quot)) - 2e-24) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(quot)) - 5e-24) < 1e-6);

  quot = __arc_div2(c, v2, v1);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(quot))/1e22 - 6.896551724) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(quot))/1e23 - -1.724137931) < 1e-6);
}
END_TEST

/*================================= Divisions involving rationals */
START_TEST(test_div_rational)
{
  value val1, val2, quot;
  mpz_t expected;

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkrationall(c, 1, 3);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_RATIONAL);
  fail_unless(mpq_cmp_si(REPRAT(quot), 3, 2) == 0);

  val1 = arc_mkrationall(c, 1, 2);
  val2 = arc_mkrationall(c, 1, 4);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_FIXNUM);
  fail_unless(FIX2INT(quot) == 2);

  val1 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(val1), "115792089237316195423570985008687907853269984665640564039457584007913129639936/3", 10);
  val2 = arc_mkrationall(c, 4, 3);
  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_BIGNUM);
  mpz_init(expected);
  mpz_set_str(expected, "28948022309329048855892746252171976963317496166410141009864396001978282409984", 10);
  fail_unless(mpz_cmp(expected, REPBNUM(quot)) == 0);
  mpz_clear(expected);
}
END_TEST

START_TEST(test_div_rational2flonum)
{
  value v1, v2, quot;

  v1 = arc_mkflonum(c, 0.25);
  v2 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(v2), "5/2", 10);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(REPFLO(quot) - 0.1) < 1e-6);

  quot = __arc_div2(c, v2, v1);
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(REPFLO(quot) - 10.0) < 1e-6);
}
END_TEST

START_TEST(test_div_rational2complex)
{
  value v1, v2, quot;

  v1 = arc_mkcomplex(c, 2.0+I*5.0);
  v2 = arc_mkrationall(c, 0, 1);
  mpq_set_str(REPRAT(v2), "5/2", 10);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(quot)) - 0.8) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(quot)) - 2.0) < 1e-6);

  quot = __arc_div2(c, v2, v1);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(quot)) -  0.172413793) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(quot)) - -0.431034483) < 1e-6);
}
END_TEST

#endif

/*================================= Divisions involving flonums */
START_TEST(test_div_flonum)
{
  value val1, val2, quot;

  val1 = arc_mkflonum(c, 1.20257);
  val2 = arc_mkflonum(c, 0.57721);

  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_FLONUM);
  fail_unless(fabs(2.0834185 - REPFLO(quot)) < 1e-6);

}
END_TEST

START_TEST(test_div_flonum2complex)
{
  value v1, v2, quot;

  v1 = arc_mkcomplex(c, 2.0+I*5.0);
  v2 = arc_mkflonum(c, 2.5);
  quot = __arc_div2(c, v1, v2);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(quot)) - 0.8) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(quot)) - 2.0) < 1e-6);

  quot = __arc_div2(c, v2, v1);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(creal(REPCPX(quot)) -  0.172413793) < 1e-6);
  fail_unless(fabs(cimag(REPCPX(quot)) - -0.431034483) < 1e-6);
}
END_TEST

START_TEST(test_div_complex)
{
  value val1, val2, quot;

  val1 = arc_mkcomplex(c, 2.0 + I*1.0);
  val2 = arc_mkcomplex(c, 3.0 + I*2.0);

  quot = __arc_div2(c, val1, val2);
  fail_unless(TYPE(quot) == T_COMPLEX);
  fail_unless(fabs(0.61538462 - creal(REPCPX(quot))) < 1e-6);
  fail_unless(fabs(-0.07692308 - cimag(REPCPX(quot))) < 1e-6);

}
END_TEST

/*================================= End of Division Tests */

int main(void)
{
  int number_failed;
  Suite *s = suite_create("Arithmetic Operations");
  TCase *tc_arith = tcase_create("Arithmetic Operations");
  SRunner *sr;

  c = &cc;
  arc_init(c);

  /* Additions of fixnums */
  tcase_add_test(tc_arith, test_add_fixnum);
#ifdef HAVE_GMP_H
  tcase_add_test(tc_arith, test_add_fixnum2bignum);
  tcase_add_test(tc_arith, test_add_fixnum2rational);
#endif
  tcase_add_test(tc_arith, test_add_fixnum2flonum);
  tcase_add_test(tc_arith, test_add_fixnum2complex);


#ifdef HAVE_GMP_H
  /* Additions of bignums */
  tcase_add_test(tc_arith, test_add_bignum);

  tcase_add_test(tc_arith, test_add_bignum2rational);
  tcase_add_test(tc_arith, test_add_bignum2flonum);
  tcase_add_test(tc_arith, test_add_bignum2complex);

  /* Additions of rationals */
  tcase_add_test(tc_arith, test_add_rational);
  tcase_add_test(tc_arith, test_add_rational2flonum);
  tcase_add_test(tc_arith, test_add_rational2complex);

#endif

  /* Additions of flonums */
  tcase_add_test(tc_arith, test_add_flonum);
  tcase_add_test(tc_arith, test_add_flonum2complex);

  /* Additions of complexes */
  tcase_add_test(tc_arith, test_add_complex);

  /* Miscellaneous type additions -- it is possible to add conses
     (concatenating the lists), strings and characters (producing
     strings) */
  tcase_add_test(tc_arith, test_add_cons);
  tcase_add_test(tc_arith, test_add_string);
  tcase_add_test(tc_arith, test_add_string2char);

  /* Subtraction of fixnums */
  tcase_add_test(tc_arith, test_sub_fixnum);
#ifdef HAVE_GMP_H
  tcase_add_test(tc_arith, test_sub_fixnum2bignum);
  tcase_add_test(tc_arith, test_sub_fixnum2rational);
#endif

  tcase_add_test(tc_arith, test_sub_fixnum2flonum);
  tcase_add_test(tc_arith, test_sub_fixnum2complex);

#ifdef HAVE_GMP_H
  /* Subtraction of bignums */
  tcase_add_test(tc_arith, test_sub_bignum);

  tcase_add_test(tc_arith, test_sub_bignum2rational);
  tcase_add_test(tc_arith, test_sub_bignum2flonum);
  tcase_add_test(tc_arith, test_sub_bignum2complex);

  /* Subtraction of rationals */
  tcase_add_test(tc_arith, test_sub_rational);
  tcase_add_test(tc_arith, test_sub_rational2flonum);
  tcase_add_test(tc_arith, test_sub_rational2complex);
#endif

  /* Subtraction of flonums */
  tcase_add_test(tc_arith, test_sub_flonum);
  tcase_add_test(tc_arith, test_sub_flonum2complex);

  /* Subtraction of complexes */
  tcase_add_test(tc_arith, test_sub_complex);

  /* Multiplication of fixnums */
  tcase_add_test(tc_arith, test_mul_fixnum);
#ifdef HAVE_GMP_H
  tcase_add_test(tc_arith, test_mul_fixnum2bignum);
  tcase_add_test(tc_arith, test_mul_fixnum2rational);
#endif
  tcase_add_test(tc_arith, test_mul_fixnum2flonum);
  tcase_add_test(tc_arith, test_mul_fixnum2complex);

#ifdef HAVE_GMP_H
  /* Multiplication of bignums */
  tcase_add_test(tc_arith, test_mul_bignum);

  tcase_add_test(tc_arith, test_mul_bignum2rational);
  tcase_add_test(tc_arith, test_mul_bignum2flonum);
  tcase_add_test(tc_arith, test_mul_bignum2complex);

  /* Multiplication of rationals */
  tcase_add_test(tc_arith, test_mul_rational);
  tcase_add_test(tc_arith, test_mul_rational2flonum);
  tcase_add_test(tc_arith, test_mul_rational2complex);

#endif

  /* Multiplication of flonums */
  tcase_add_test(tc_arith, test_mul_flonum);
  tcase_add_test(tc_arith, test_mul_flonum2complex);

  /* Multiplication of complexes */
  tcase_add_test(tc_arith, test_mul_complex);

  /* Division of fixnums */
  tcase_add_test(tc_arith, test_div_fixnum);

#ifdef HAVE_GMP_H
  tcase_add_test(tc_arith, test_div_fixnum2bignum);
  tcase_add_test(tc_arith, test_div_fixnum2rational);
#endif

  tcase_add_test(tc_arith, test_div_fixnum2flonum);
  tcase_add_test(tc_arith, test_div_fixnum2complex);

#ifdef HAVE_GMP_H
  /* Division of bignums */
  tcase_add_test(tc_arith, test_div_bignum);
  tcase_add_test(tc_arith, test_div_bignum2rational);
  tcase_add_test(tc_arith, test_div_bignum2flonum);
  tcase_add_test(tc_arith, test_div_bignum2complex);

  /* Division of rationals */
  tcase_add_test(tc_arith, test_div_rational);
  tcase_add_test(tc_arith, test_div_rational2flonum);
  tcase_add_test(tc_arith, test_div_rational2complex);

#endif

  /* Division of flonums */
  tcase_add_test(tc_arith, test_div_flonum);
  tcase_add_test(tc_arith, test_div_flonum2complex);

  /* Division of complexes */
  tcase_add_test(tc_arith, test_div_complex);

  suite_add_tcase(s, tc_arith);
  sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return((number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}

