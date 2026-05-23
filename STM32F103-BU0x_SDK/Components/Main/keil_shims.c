#include <math.h>
#include <stdio.h>
#include <stdarg.h>

int __2sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsprintf(buf, fmt, args);
    va_end(args);
    return n;
}

int __2snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}

double __ARM_scalbn(double x, int n)
{
    return scalbn(x, n);
}

float __ARM_scalbnf(float x, int n)
{
    return scalbnf(x, n);
}
