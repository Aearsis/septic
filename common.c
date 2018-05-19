#include "common.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "log.h"

void emerg_exit(const char *path, int line, int code)
{
	log_err("fatal error at %s:%d: %s (%d)", path, line, strerror(code), code);
	exit(1);
}

static bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void alloc_err(size_t size)
{
	log_err("allocation error, size %zu, errno %d", size, errno);
	exit(1);
}

void *salloc(size_t size)
{
	void *res;

	res = malloc(size);
	if (!res)
		alloc_err(size);
	return res;
}

void *szalloc(size_t size)
{
	void *res;

	res = calloc(1, size);
	if (!res)
		alloc_err(size);
	return res;
}

void *srealloc(void *ptr, size_t size)
{
	void *res;

	res = realloc(ptr, size);
	if (!res)
		alloc_err(size);
	return res;
}

void sfree(void *ptr)
{
	free(ptr);
}

void rstrip(char *s)
{
	size_t pos;

	for (pos = strlen(s); pos > 0 && is_space(s[pos - 1]); pos--)
		;
	s[pos] = '\0';
}

size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t i = 0;

	if (size == 0)
		return strlen(src);
	while (i < size - 1 && *src) {
		*dest++ = *src++;
		i++;
	}
	*dest = '\0';
	while (*src) {
		src++;
		i++;
	}
	return i;
}

char *sstrdup(const char *s)
{
	char * res;

	res = strdup(s);
	if (!res)
		alloc_err(strlen(s));
	return res;
}

char *ssprintf(const char *format, ...)
{
	va_list ap;
	char *res;

	va_start(ap, format);
	if (vasprintf(&res, format, ap) < 0 || !res)
		alloc_err(-1);
	va_end(ap);
	return res;
}

bool to_int(const char *src, int *dst)
{
	long res;

	if (!to_long(src, &res))
		return false;
	if (res < INT_MIN || res > INT_MAX)
		return false;
	*dst = res;
	return true;
}

bool to_long(const char *src, long *dst)
{
	char *end;

	*dst = strtol(src, &end, 10);
	return !*end;
}

void randomize(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	srandom(ts.tv_nsec ^ ts.tv_sec);
}
