#include <stdio.h>

int main(void)
{
    // Le poids 1 code la cellule de gauche. Les voisines hors ligne valent 0.
    int ligne = 4;

    for (int generation = 0; generation < 3; generation++)
    {
        int gauche = 0;
        int poids = 1;
        int suivante = 0;
        for (int colonne = 0; colonne < 5; colonne++)
        {
            int ici = (ligne / poids) % 2;
            int droite = (ligne / (2 * poids)) % 2;
            putchar(".#"[ici]);
            if (gauche != droite)
                suivante += poids;
            gauche = ici;
            poids *= 2;
        }
        putchar('\n');
        if (generation == 0)
        {
            // ? suivante
        }
        ligne = suivante;
    }
    return 0;
}
