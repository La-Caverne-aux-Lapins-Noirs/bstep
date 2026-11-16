#include <stdio.h>

int main(void)
{
    int releves[] = {2, 0, 4};
    int total = 0;

    for (int ligne = 0; ligne < 3; ligne++)
    {
        printf("%c|", 'A' + ligne);
        for (int i = 0; i < releves[ligne]; i++)
            putchar('#');
        total += releves[ligne];
        putchar('\n');
    }
    // ? total
    printf("Total %d\n", total);
    return 0;
}
