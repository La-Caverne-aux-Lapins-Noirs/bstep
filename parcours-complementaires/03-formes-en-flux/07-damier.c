#include <stdio.h>

int main(void)
{
    int claires = 0;

    for (int y = 0; y < 3; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            if ((x + y) % 2 == 0)
            {
                putchar('.');
                claires++;
            }
            else
                putchar('#');
        }
        putchar('\n');
    }
    // ? claires
    return 0;
}
