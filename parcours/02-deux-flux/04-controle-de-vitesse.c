#include <stdio.h>

int main(void)
{
    int vitesse = 0;

    for (int tour = 0; tour < 3; tour++)
    {
        vitesse = vitesse + 2;
        // ? tour, vitesse
        fprintf(stderr, "%d ", vitesse);
        if (vitesse >= 4)
            putchar('+');
        else
            putchar('.');
    }
    putchar('\n');
    return 0;
}
