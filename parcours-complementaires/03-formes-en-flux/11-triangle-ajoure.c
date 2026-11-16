#include <stdio.h>

int main(void)
{
    int marques = 0;

    for (int y = 0; y < 4; y++)
    {
        for (int x = -3; x <= 3; x++)
        {
            if (x == y || x == -y || (y == 3 && x % 2 == 0))
            {
                putchar('#');
                marques++;
            }
            else
                putchar('.');
        }
        putchar('\n');
    }
    // ? marques
    return 0;
}
