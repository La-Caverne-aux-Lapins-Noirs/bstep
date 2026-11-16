#include <stdio.h>

int chemins(int n)
{
    if (n == 0)
    {
        putchar('|');
        return 1;
    }
    if (n < 0)
        return 0;
    int un = chemins(n - 1);
    int deux = chemins(n - 2);
    if (n == 3)
    {
        // ? un, deux
    }
    putchar('0' + n);
    return un + deux;
}

int main(void)
{
    int nombre = chemins(3);
    printf("\nNombre %d\n", nombre);
    return 0;
}
