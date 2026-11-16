#include <stdio.h>

int main(void)
{
    int niveau = 1;
    int tours = 0;

    while (niveau < 6)
    {
        printf("%d>", niveau);
        niveau = niveau * 2;
        printf("%d ", niveau);
        tours++;
    }
    // ? tours, niveau
    putchar('\n');
    return 0;
}
