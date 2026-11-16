#include <stdio.h>

int main(void)
{
    int salle = 1;

    while (salle <= 3)
    {
        printf("%d", salle);
        salle++;
    }
    // ? salle
    printf(":%d\n", salle);
    return 0;
}
