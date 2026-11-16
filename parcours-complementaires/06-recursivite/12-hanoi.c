#include <stdio.h>

void deplace(int taille, char depart, char arrivee, char relais)
{
    if (taille == 0)
        return;
    deplace(taille - 1, depart, relais, arrivee);
    printf("%c>%c\n", depart, arrivee);
    deplace(taille - 1, relais, arrivee, depart);
}

int main(void)
{
    deplace(2, 'A', 'C', 'B');
    return 0;
}
