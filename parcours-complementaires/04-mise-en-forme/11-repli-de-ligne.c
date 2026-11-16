#include <stdio.h>

int main(void)
{
    int tailles[] = {2, 3, 1, 2};
    char signes[] = "x.y#";
    int colonne = 0;

    for (int r = 0; r < 4; r++)
    {
        for (int j = 0; j < tailles[r]; j++)
        {
            putchar(signes[r]);
            colonne++;
            if (colonne == 4)
            {
                putchar('\n');
                colonne = 0;
            }
        }
    }
    // ? colonne
    if (colonne != 0)
        putchar('\n');
    return 0;
}
