#include <stdio.h>

int main(void)
{
    for (int y = 0; y < 5; y++)
    {
        for (int x = 0; x < 5; x++)
        {
            if (x == y && x + y == 4)
                putchar('O');
            else if (x == y || x + y == 4)
                putchar('X');
            else
                putchar('.');
        }
        putchar('\n');
    }
    return 0;
}
