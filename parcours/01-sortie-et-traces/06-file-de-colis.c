#include <stdio.h>

int main(void)
{
    int stock = 0;
    int sorties = 0;

    for (int i = 0; i < 4; i++)
        stock = stock + 2;

    while (stock >= 3)
    {
        printf("p%d ", stock);
        stock = stock - 3;
        sorties++;
    }
    // ? stock, sorties
    printf("\nIl reste %d.\n", stock);
    return 0;
}
