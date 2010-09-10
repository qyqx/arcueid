/* 
  Copyright (C) 2010 Rafael R. Sevilla

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
#include <stdio.h>
#include <alloca.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include "arcueid.h"
#include "utf.h"
#include "../config.h"

enum file_types {
  FT_FILE,			/* normal file */
  FT_STRING,			/* string file */
  FT_SOCKET			/* socket */
};

struct arc_port {
  int type;			/* type of file */
  union {
    struct {
      value str;
      int idx;
    } strfile;
    struct {
      value name;
      FILE *fp;
      int open;
    } file;
    int sock;
  } u;
  int (*getb)(arc *, struct arc_port *);
  int (*peekb)(arc *, struct arc_port *);
  int (*putb)(arc *, struct arc_port *, int);
  int (*seek)(arc *, struct arc_port *, int64_t, int);
  int64_t (*tell)(arc *, struct arc_port *);
  char uc_buf[UTFmax];
};

#define PORT(v) ((struct arc_port *)REP(v)._custom.data)
#define PORTF(v) (PORT(v)->u.file)
#define PORTS(v) (PORT(v)->u.strfile)

static value file_pp(arc *c, value v)
{
  value nstr = arc_mkstringc(c, "#<port:");

  nstr = arc_strcat(c, nstr, PORTF(v).name);
  nstr = arc_strcat(c, nstr, arc_mkstringc(c, ">"));
  return(nstr);
}

static void file_marker(arc *c, value v, int level,
			void (*markfn)(arc *, value, int))
{
  /* does nothing */
}

static void file_sweeper(arc *c, value v)
{
  if (PORTF(v).open) {
    fclose(PORTF(v).fp);
    /* XXX: error handling here? */
    PORTF(v).open = 0;
  }
  /* release memory */
  c->free_block(c, (void *)v);
}

static int file_getb(arc *c, struct arc_port *p)
{
  return(fgetc(p->u.file.fp));
}

static int file_peekb(arc *c, struct arc_port *p)
{
  int ch;

  ch = fgetc(p->u.file.fp);
  ungetc(ch, p->u.file.fp);
  return(ch);
}

static int file_putb(arc *c, struct arc_port *p, int byte)
{
  return(fputc(byte, p->u.file.fp));
}

static int file_seek(arc *c, struct arc_port *p, int64_t offset, int whence)
{
#ifdef HAVE_FSEEKO
  return(fseeko(p->u.file.fp, offset, whence));
#else
  return(fseek(p->u.file.fp, (long)offset, whence));
#endif
}

static int64_t file_tell(arc *c, struct arc_port *p)
{
#ifdef HAVE_FSEEKO
  return((int64_t)ftello(p->u.file.fp));
#else
  return((int64_t)ftell(p->u.file.fp));
#endif
}

static value openfile(arc *c, value filename, const char *mode)
{
  void *cellptr;
  value fd;
  char *utf_filename;
  FILE *fp;
  int en;

  cellptr = c->get_block(c, sizeof(struct cell) + sizeof(struct arc_port));
  if (cellptr == NULL)
    c->signal_error(c, "openfile: cannot allocate memory");
  fd = (value)cellptr;
  BTYPE(fd) = T_PORT;
  REP(fd)._custom.pprint = file_pp;
  REP(fd)._custom.marker = file_marker;
  REP(fd)._custom.sweeper = file_sweeper;
  PORT(fd)->type = FT_FILE;
  /* XXX make this file name a fully qualified pathname */
  PORTF(fd).name = filename;
  utf_filename = alloca(FIX2INT(arc_strutflen(c, filename)) + 1);
  arc_str2cstr(c, filename, utf_filename);
  fp = fopen(utf_filename, mode);
  if (fp == NULL) {
    en = errno;
    c->signal_error(c, "openfile: cannot open input file \"%s\", (%s; errno=%d)", utf_filename, strerror(en), en);
  }
  PORTF(fd).fp = fp;
  PORTF(fd).open = 1;
  PORT(fd)->getb = file_getb;
  PORT(fd)->peekb = file_peekb;
  PORT(fd)->putb = file_putb;
  PORT(fd)->seek = file_seek;
  PORT(fd)->tell = file_tell;
  return(fd);
}

value arc_infile(arc *c, value filename)
{
  return(openfile(c, filename, "r"));
}

value arc_outfile(arc *c, value filename, value xmode)
{
  if (xmode == arc_intern_cstr(c, "append"))
    return(openfile(c, filename, "a"));
  return(openfile(c, filename, "w"));
}

static value fstr_pp(arc *c, value v)
{
  return(arc_mkstringc(c, "#<port:string>"));
}

static void fstr_marker(arc *c, value v, int level,
			void (*mark)(arc *, value, int))
{
  mark(c, PORTS(v).str, level+1);
}

static void fstr_sweeper(arc *c, value v)
{
  /* release memory */
  c->free_block(c, (void *)v);
}

/* NOTE: this will actually return a RUNE, not a byte! */
static int fstr_getb(arc *c, struct arc_port *p)
{
  int len;

  len = arc_strlen(c, p->u.strfile.str);
  if (p->u.strfile.idx >= len)
    return(EOF);
  return(arc_strindex(c, p->u.strfile.str, p->u.strfile.idx++));
}

/* NOTE: this will actually return a RUNE, not a byte! */
static int fstr_peekb(arc *c, struct arc_port *p)
{
  int len;

  len = arc_strlen(c, p->u.strfile.str);
  if (p->u.strfile.idx >= len)
    return(EOF);
  return(arc_strindex(c, p->u.strfile.str, p->u.strfile.idx));
}

/* NOTE: this will actually write a RUNE, not a byte! */
static int fstr_putb(arc *c, struct arc_port *p, int byte)
{
  int len;

  len = arc_strlen(c, p->u.strfile.str);
  if (p->u.strfile.idx >= len) {
    p->u.strfile.idx = len+1;
    p->u.strfile.str = arc_strcatc(c, p->u.strfile.str, (Rune)byte);
  } else {
    arc_strsetindex(c, p->u.strfile.str, p->u.strfile.idx, (Rune)byte);
  }
  return(byte);
}

static int fstr_seek(arc *c, struct arc_port *p, int64_t offset, int whence)
{
  int64_t len;

  len = (int64_t)arc_strlen(c, p->u.strfile.str);
  switch (whence) {
  case SEEK_SET:
    break;
  case SEEK_CUR:
    offset += p->u.strfile.idx;
    break;
  case SEEK_END:
    offset = len - offset;
    break;
  default:
    return(-1);
  }
  if (offset >= len || offset < 0)
    return(-1);
  p->u.strfile.idx = offset;
  return(0);
}

static int64_t fstr_tell(arc *c, struct arc_port *p)
{
  return(p->u.strfile.idx);
}

value arc_instring(arc *c, value str)
{
  void *cellptr;
  value fd;

  cellptr = c->get_block(c, sizeof(struct cell) + sizeof(struct arc_port));
  if (cellptr == NULL)
    c->signal_error(c, "openstring: cannot allocate memory");
  fd = (value)cellptr;
  BTYPE(fd) = T_PORT;
  REP(fd)._custom.pprint = fstr_pp;
  REP(fd)._custom.marker = fstr_marker;
  REP(fd)._custom.sweeper = fstr_sweeper;
  PORT(fd)->type = FT_STRING;
  PORTS(fd).idx = 0;
  PORTS(fd).str = str;
  PORT(fd)->getb = fstr_getb;
  PORT(fd)->peekb = fstr_peekb;
  PORT(fd)->putb = fstr_putb;
  PORT(fd)->seek = fstr_seek;
  PORT(fd)->tell = fstr_tell;
  return(fd);
}

value arc_outstring(arc *c, value str)
{
  return(arc_instring(c, arc_mkstringc(c, "")));
}

value arc_fstr_inside(arc *c, value fstr)
{
  return(PORTS(fstr).str);
}

value arc_readb(arc *c, value fd)
{
  int ch;

  ch = PORT(fd)->getb(c, PORT(fd));
  if (ch == EOF)
    return(CNIL);
  return(INT2FIX(ch));
}

value arc_writeb(arc *c, value byte, value fd)
{
  int ch = FIX2INT(byte);

  PORT(fd)->putb(c, PORT(fd), ch);
  if (ch == EOF)
    return(CNIL);
  return(INT2FIX(ch));
}

Rune arc_readc_rune(arc *c, value fd)
{
  int ch;
  char buf[UTFmax];
  int i;
  Rune r;

  if (PORT(fd)->type == FT_STRING)
    return(FIX2INT(arc_readb(c, fd)));

  for (i=0; i<UTFmax; i++) {
    ch = PORT(fd)->getb(c, PORT(fd));
    if (ch == EOF)
      return(CNIL);
    buf[i] = ch;
    if (fullrune(buf, i+1))
      return(chartorune(&r, buf));
  }
  return(Runeerror);
}

value arc_readc(arc *c, value fd)
{
  return(arc_mkchar(c, arc_readc_rune(c, fd)));
}

Rune arc_writec_rune(arc *c, Rune r, value fd)
{
  char buf[UTFmax];
  int nbytes, i;

  if (PORT(fd)->type == FT_STRING) {
    PORT(fd)->putb(c, PORT(fd), r);
    return(r);
  }

  nbytes = runetochar(buf, &r);
  for (i=0; i<nbytes; i++)
    PORT(fd)->putb(c, PORT(fd), buf[i]);
  return(r);
}

value arc_writec(arc *c, value r, value fd)
{
  arc_writec_rune(c, REP(r)._char, fd);
  return(r);
}
