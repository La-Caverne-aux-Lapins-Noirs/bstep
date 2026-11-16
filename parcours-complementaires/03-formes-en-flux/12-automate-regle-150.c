#include <stdio.h>

int main(void)
{
    // Une cellule s'allume quand la somme de son état et de ses voisines est impaire.
    int ligne = 10;

    for (int generation = 0; generation < 4; generation++)
    {
        int poids = 1;
        int gauche = 0;
        int suivante = 0;
        for (int x = 0; x < 5; x++)
        {
            int ici = (ligne / poids) % 2;
            int droite = (ligne / (poids * 2)) % 2;
            putchar(".#"[ici]);
            suivante += poids * ((gauche + ici + droite) % 2);
            poids *= 2;
            gauche = ici;
        }
        putchar('\n');
        ligne = suivante;
    }
    // ? ligne
    return 0;
}
