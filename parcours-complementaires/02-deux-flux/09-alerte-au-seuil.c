#include <stdio.h>

int main(void)
{
    int restant = 6;

    while (restant >= 2)
    {
        restant -= 2;
        if (restant == 2)
            fputs("RESERVE", stderr);
        putchar('a' + restant);
    }
    putchar('\n');
    return 0;
}
