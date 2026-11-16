#include <stdio.h>

int main(void)
{
    int badge = 4;
    int limite = 4;

    putchar('[');
    if (badge > limite)
        putchar('X');
    else
        putchar('O');
    putchar(']');
    putchar('\n');
    return 0;
}
