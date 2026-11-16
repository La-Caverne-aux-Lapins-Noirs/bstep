#include <stdio.h>

int main(void)
{
    int mesures[] = {7, 4, 9};
    int minimum = mesures[0];
    int maximum = mesures[0];

    for (int i = 1; i < 3; i++)
    {
        if (mesures[i] < minimum)
            minimum = mesures[i];
        if (mesures[i] > maximum)
            maximum = mesures[i];
    }
    // ? minimum, maximum
    puts("MIN|MAX|ECART");
    printf("  %d|  %d|%d\n", minimum, maximum, maximum - minimum);
    return 0;
}
