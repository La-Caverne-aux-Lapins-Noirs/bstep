#include <stdio.h>

void trajet(int etage)
{
    if (etage == 0)
    {
        putchar('|');
        return;
    }
    putchar('0' + etage);
    trajet(etage - 1);
    // ? etage
    putchar('a' + etage - 1);
}

int main(void)
{
    trajet(3);
    putchar('\n');
    return 0;
}
