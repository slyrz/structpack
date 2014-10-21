/**
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ssize_t pack (int fd, const char *fmt, ...);
ssize_t unpack (int fd, const char *fmt, ...);
ssize_t vpack (int fd, const char *fmt, va_list ap);
ssize_t vunpack (int fd, const char *fmt, va_list ap);

ssize_t
pack (int fd, const char *fmt, ...)
{
  ssize_t ret;
  va_list ap;
  va_start (ap, fmt);
  ret = vpack (fd, fmt, ap);
  va_end (ap);
  return ret;
}

ssize_t
unpack (int fd, const char *fmt, ...)
{
  ssize_t ret;
  va_list ap;
  va_start (ap, fmt);
  ret = vunpack (fd, fmt, ap);
  va_end (ap);
  return ret;
}

struct arg {
  size_t size;
  uintmax_t val;
  void *ptr;
  void **pptr;
};

struct state {
  ssize_t ret;
  int err;
};

#define pop_arg(arg,ap,type) \
  do { \
    type v = va_arg (ap, type); \
    *(arg) = (struct arg){ .val = v, .size = sizeof (type) }; \
  } while (0)

#define pop_ptr(arg,ap,type) \
  do { \
    type * p = va_arg (ap, type *); \
    *(arg) = (struct arg){ .val = *p, .ptr = p, .size = sizeof (type) }; \
  } while (0)

#define pop_arr(arg,ap,type) \
  do { \
    type ** p = va_arg (ap, type **); \
    *(arg) = (struct arg){ .ptr = *p, .pptr = (void**) p, .size = sizeof (type) }; \
  } while (0)

static void
arg_val (struct arg *arg, va_list * ap, const char type)
{
  switch (type) {
    case 'c': /* type promotion */
    case 'C': /* type promotion */
    case 'h': /* type promotion */
    case 'H': /* type promotion */
    case 'i': pop_arg (arg, *ap, int); break;
    case 'I': pop_arg (arg, *ap, unsigned int); break;
    case 'l': pop_arg (arg, *ap, long int); break;
    case 'L': pop_arg (arg, *ap, unsigned long int); break;
    case 'q': pop_arg (arg, *ap, long long int); break;
    case 'Q': pop_arg (arg, *ap, unsigned long long int); break;
    case 'z': pop_arg (arg, *ap, size_t); break;
    case 'Z': pop_arg (arg, *ap, ssize_t); break;
  }
}

static void
arg_ptr (struct arg *arg, va_list * ap, const char type)
{
  switch (type) {
    case 'c': pop_ptr (arg, *ap, char); break;
    case 'C': pop_ptr (arg, *ap, unsigned char); break;
    case 'h': pop_ptr (arg, *ap, short int); break;
    case 'H': pop_ptr (arg, *ap, unsigned short int); break;
    case 'i': pop_ptr (arg, *ap, int); break;
    case 'I': pop_ptr (arg, *ap, unsigned int); break;
    case 'l': pop_ptr (arg, *ap, long int); break;
    case 'L': pop_ptr (arg, *ap, unsigned long int); break;
    case 'q': pop_ptr (arg, *ap, long long int); break;
    case 'Q': pop_ptr (arg, *ap, unsigned long long int); break;
    case 'z': pop_ptr (arg, *ap, size_t); break;
    case 'Z': pop_ptr (arg, *ap, ssize_t); break;
  }
}

static void
arg_arr (struct arg *arg, va_list * ap, const char type)
{
  switch (type) {
    case 'c': pop_arr (arg, *ap, char); break;
    case 'C': pop_arr (arg, *ap, unsigned char); break;
    case 'h': pop_arr (arg, *ap, short int); break;
    case 'H': pop_arr (arg, *ap, unsigned short int); break;
    case 'i': pop_arr (arg, *ap, int); break;
    case 'I': pop_arr (arg, *ap, unsigned int); break;
    case 'l': pop_arr (arg, *ap, long int); break;
    case 'L': pop_arr (arg, *ap, unsigned long int); break;
    case 'q': pop_arr (arg, *ap, long long int); break;
    case 'Q': pop_arr (arg, *ap, unsigned long long int); break;
    case 'z': pop_arr (arg, *ap, size_t); break;
    case 'Z': pop_arr (arg, *ap, ssize_t); break;
    case 's': pop_arr (arg, *ap, char); break;
  }
}

static int
sane_size (size_t bytes)
{
  return bytes <= (1024 * 1024 * 16);
}

static void
rec_alloc (struct state *state, struct arg *arg, size_t nmemb, size_t size)
{
  if ((state->err) || ((nmemb == 0) && (size == 0)))
    return;

  *arg->pptr = arg->ptr = sane_size(nmemb * size) ? calloc (nmemb, size) : NULL;
  if (arg->ptr == NULL)
    state->err = 1;
}

typedef ssize_t (*iofunc) (int, void *, size_t);

static void
rec_io (struct state *state, iofunc func, int fd, void *ptr, size_t size)
{
  ssize_t ret;

  if ((state->err) || (ptr == NULL) || (size == 0))
    return;

  for (;;) {
    ret = func (fd, ptr, size);
    if ((ret == -1) && (errno == EINTR))
      continue;
    break;
  }

  if ((size_t) ret == size) {
    state->ret = state->ret + ret;
    state->err = 0;
  }
  else {
    state->ret = ret;
    state->err = 1;
  }
}

static void
rec_read (struct state *state, int fd, void *ptr, size_t size)
{
  rec_io (state, (iofunc) read, fd, ptr, size);
}

static void
rec_write (struct state *state, int fd, void *ptr, size_t size)
{
  rec_io (state, (iofunc) write, fd, ptr, size);
}

ssize_t
vpack (int fd, const char *fmt, va_list ap)
{
  struct state state = { 0, 0 };
  struct arg arg0;
  struct arg arg1;

  va_list app;
  va_copy (app, ap);

  while (*fmt) {
    memset (&arg0, 0, sizeof (struct arg));
    memset (&arg1, 0, sizeof (struct arg));
    switch (*fmt) {
      case 'c':
      case 'C':
      case 'h':
      case 'H':
      case 'i':
      case 'I':
      case 'l':
      case 'L':
      case 'q':
      case 'Q':
      case 'f':
      case 'd':
      case 'z':
      case 'Z':
        arg_ptr (&arg0, &app, fmt[0]);
        rec_write (&state, fd, arg0.ptr, arg0.size);
        break;
      case 's':
        arg_arr (&arg0, &app, fmt[0]);
        arg0.size = strlen (arg0.ptr) + 1;
        rec_write (&state, fd, &arg0.size, sizeof (size_t));
        rec_write (&state, fd, arg0.ptr, arg0.size);
        break;
      case '=':
        arg_val (&arg0, &app, fmt[1]);
        arg_arr (&arg1, &app, fmt[2]);
        rec_write (&state, fd, &arg0.val, sizeof (size_t));
        rec_write (&state, fd, arg1.ptr, arg0.val * arg1.size);
        fmt += 2;
        break;
      case '*':
        arg_ptr (&arg0, &app, fmt[1]);
        arg_arr (&arg1, &app, fmt[2]);
        rec_write (&state, fd, &arg0.val, sizeof (size_t));
        rec_write (&state, fd, arg1.ptr, arg0.val * arg1.size);
        fmt += 2;
        break;
      default:
        fprintf (stderr, "unkown format %c", *fmt);
        return -1;
    }
    if (state.err)
      break;
    fmt++;
  }
  if ((state.err) && (state.ret >= 0))
    state.ret = -1;
  return state.ret;
}

ssize_t
vunpack (int fd, const char *fmt, va_list ap)
{
  struct state state = { 0, 0 };
  struct arg arg0;
  struct arg arg1;

  va_list app;
  va_copy (app, ap);

  while (*fmt) {
    size_t s = 0;
    memset (&arg0, 0, sizeof (struct arg));
    memset (&arg1, 0, sizeof (struct arg));
    switch (*fmt) {
      case 'c':
      case 'C':
      case 'h':
      case 'H':
      case 'i':
      case 'I':
      case 'l':
      case 'L':
      case 'q':
      case 'Q':
      case 'f':
      case 'd':
      case 'z':
      case 'Z':
        arg_ptr (&arg0, &app, fmt[0]);
        rec_read (&state, fd, arg0.ptr, arg0.size);
        break;
      case 's':
        arg_arr (&arg0, &app, fmt[0]);
        rec_read (&state, fd, &s, sizeof (size_t));
        rec_alloc (&state, &arg0, s, sizeof (char));
        rec_read (&state, fd, arg0.ptr, s);
        break;
      case '=':
        arg_val (&arg0, &app, fmt[1]);
        arg_arr (&arg1, &app, fmt[2]);
        rec_read (&state, fd, &s, sizeof (size_t));
        rec_alloc (&state, &arg1, s, arg1.size);
        rec_read (&state, fd, arg1.ptr, s * arg1.size);
        fmt += 2;
        break;
      case '*':
        arg_ptr (&arg0, &app, fmt[1]);
        arg_arr (&arg1, &app, fmt[2]);
        rec_read (&state, fd, &s, sizeof (size_t));
        rec_alloc (&state, &arg1, s, arg1.size);
        rec_read (&state, fd, arg1.ptr, s * arg1.size);
        *(size_t *) arg0.ptr = s;
        fmt += 2;
        break;
      default:
        fprintf (stderr, "unkown format %c", *fmt);
        return -1;
    }
    if (state.err)
      break;
    fmt++;
  }
  if ((state.err) && (state.ret >= 0))
    state.ret = -1;
  return state.ret;
}

