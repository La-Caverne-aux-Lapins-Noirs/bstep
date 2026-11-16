#include <stdio.h>

int main(void)
{
    int scores[] = {8, 12, 3};

    for (int j = 0; j < 3; j++)
    {
        printf("%c|", 'A' + j);
        if (scores[j] < 10)
            putchar(' ');
        printf("%d|\n", scores[j]);
    }
    return 0;
}
