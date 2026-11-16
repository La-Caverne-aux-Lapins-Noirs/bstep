#include <stdio.h>

int cumul(int n)
{
    if (n == 0)
        return 0;
    int reste = cumul(n - 1);
    if (n == 2)
    {
        // ? n, reste
    }
    printf("%d:%d\n", n, reste);
    return n + reste;
}

int main(void)
{
    int resultat = cumul(3);
    printf("Total %d\n", resultat);
    return 0;
}
