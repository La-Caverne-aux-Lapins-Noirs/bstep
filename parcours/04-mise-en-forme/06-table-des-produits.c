#include <stdio.h>

int main(void)
{
    puts("x | 2  3  4");
    for (int y = 2; y <= 3; y++)
    {
        printf("%d |", y);
        for (int x = 2; x <= 4; x++)
        {
            int produit = x * y;
            if (produit < 10)
                putchar(' ');
            printf("%d ", produit);
        }
        putchar('\n');
    }
    return 0;
}
