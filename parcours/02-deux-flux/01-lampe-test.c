#include <stdio.h>

int main(void)
{
    puts("PRET");
    fputc('T', stderr);
    fputs("=OK\n", stderr);
    putchar('>');
    return 0;
}
