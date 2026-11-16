#include <stdio.h>

int main(void)
{
    int seuil = 2;

    for (int i = 0; i < 2; i++)
        if (i >= seuil)
            fputs("haut\n", stderr);
    puts("Termine");
    return 0;
}
