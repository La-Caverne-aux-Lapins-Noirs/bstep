#include <stdio.h>

int main(void)
{
    int quota = 3;
    int places = 5;

    putchar('[');
    for (int j = 0; j < places; j++)
    {
        if (j < quota)
            putchar('#');
        else
            putchar('-');
    }
    printf("] %d/%d\n", quota, places);
    return 0;
}
