#include <stdio.h>

int main(void)
{
    int mesures[] = {3, -1, 5};
    int gardees = 0;

    for (int i = 0; i < 3; i++)
    {
        if (mesures[i] < 0)
            fprintf(stderr, "rejet %d\n", i);
        else
        {
            printf("%d ", mesures[i]);
            gardees++;
        }
    }
    // ? gardees
    printf("\nOK %d\n", gardees);
    return 0;
}
