#include <stdio.h>

int main(void)
{
    int tour = 0;
    int signaux = 0;

    while (tour < 3)
    {
        putchar('.');
        signaux = signaux + 2;
        // ? tour, signaux
        tour = tour + 1;
    }
    printf(" %d\n", signaux);
    return 0;
}
