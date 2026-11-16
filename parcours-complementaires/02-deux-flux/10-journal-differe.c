#include <stdio.h>

int main(void)
{
    int n = 1;
    int duree = 0;

    while (duree < 4)
    {
        duree += n;
        n++;
    }
    // ? duree, n
    fprintf(stderr, "tests=%d\n", n - 1);
    printf("t=%d\n", duree);
    return 0;
}
