/*
 * User-mode nd_* hooks for bddisasm (same role as disasmtool.c in upstream; not linked as a whole).
 */
#include "nd_crt.h"

#include <stdio.h>
#include <string.h>

int nd_vsnprintf_s(
	char* buffer,
	size_t sizeOfBuffer,
	size_t count,
	const char* format,
	va_list argptr)
{
	return _vsnprintf_s(buffer, sizeOfBuffer, count, format, argptr);
}

void* nd_memset(void* s, int c, size_t n)
{
	return memset(s, c, n);
}
