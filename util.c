#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "util.h"

extern void
error(int exit_status, int errnum, const char *fmt, ...)
{
	va_list ap;

	fflush(stdout);
	fprintf(stderr, "%s: ", PROGNAME);
	va_start(ap, fmt);
	if (fmt)
		vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (errnum)
		fprintf(stderr, "%s%s", fmt ? ": " : "", strerror(errnum));
	fputc('\n', stderr);

	if (exit_status)
		exit(exit_status);
}

extern void *
emalloc(size_t size)
{
	void *ret;

	if ((ret = malloc(size)) == NULL)
		error(1, errno, "emalloc");
	return ret;
}

extern void *
ecalloc(size_t nmemb, size_t size)
{
	void *ret;

	if ((ret = calloc(nmemb, size)) == NULL)
		error(1, errno, "ecalloc");
	return ret;
}

extern void *
erealloc(void *ptr, size_t size)
{
	void *ret;

	if ((ret = realloc(ptr, size)) == NULL)
		error(1, errno, "erealloc");
	return ret;
}
