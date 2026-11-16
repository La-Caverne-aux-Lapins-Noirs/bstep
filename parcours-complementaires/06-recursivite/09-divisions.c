#include <stdio.h>

int pgcd(int a, int b)
{
    printf("%d/%d ", a, b);
    if (b == 0)
        return a;
    int reste = a % b;
    // ? reste
    return pgcd(b, reste);
}

int main(void)
{
    int valeur = pgcd(12, 8);
    printf("=%d\n", valeur);
    return 0;
}
