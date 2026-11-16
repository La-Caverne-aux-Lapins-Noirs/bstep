#include <stdio.h>

int main(void)
{
    int dossier = 3;

    printf("A%d", dossier);
    fprintf(stderr, "B%d", dossier + 1);
    putchar('Z');
    fputc('X', stderr);
    return 0;
}
