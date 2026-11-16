#include <stdio.h>

int main(void)
{
    for (int y = 0; y < 7; y++)
    {
        for (int x = 0; x < 7; x++)
        {
            int exterieur = y == 0 || y == 6 || x == 6 || (x == 0 && y >= 2);
            int interieur = (y == 2 && x <= 4) || (x == 4 && y >= 2 && y <= 4)
                || (y == 4 && x >= 2 && x <= 4);
            if (exterieur || interieur)
                putchar('#');
            else
                putchar('.');
        }
        putchar('\n');
    }
    return 0;
}
