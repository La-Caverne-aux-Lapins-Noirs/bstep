#include <stdio.h>

int main(void)
{
    int interieur = 0;

    for (int y = 0; y < 3; y++)
    {
        for (int x = 0; x < 5; x++)
        {
            if (y == 0 || y == 2 || x == 0 || x == 4)
                putchar('#');
            else
            {
                putchar('.');
                interieur++;
            }
        }
        putchar('\n');
    }
    // ? interieur
    return 0;
}
