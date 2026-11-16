#include <stdio.h>

int main(void)
{
    for (int y = 1; y <= 4; y++)
    {
        for (int x = 1; x <= 4; x++)
        {
            if (x <= y)
                putchar('0' + y);
            else
                putchar('.');
        }
        putchar('\n');
    }
    return 0;
}
