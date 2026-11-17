#include <stdio.h>

char *strcpy(char *dest, char *src)
{
    int i;

    i = 0;
    while (dest[i] = src[i])
        i = i + 1;
    return (dest);
}

int main(void)
{
    char *dest;
    char buf[16];

    dest = strcpy(buf, "abc");
    puts(dest);
    return (0);
}