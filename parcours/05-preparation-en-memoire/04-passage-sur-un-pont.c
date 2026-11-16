#include <stdio.h>

int main(void)
{
    char carte[] = "...............";

    for (int x = 0; x < 5; x++)
        carte[5 + x] = '-';
    for (int y = 0; y < 3; y++)
        carte[y * 5 + 2] = '|';
    for (int i = 0; i < 15; i++)
    {
        putchar(carte[i]);
        if (i % 5 == 4)
            putchar('\n');
    }
    return 0;
}
