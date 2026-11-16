#include <stdio.h>

int func(int j)
{
    int k;

    k = j / 2;
    while (k < j)
    {
        printf("%d\n", k * 2);
        k = k + 1;
    }
}

int main(void)
{
    int i;

    i = 0;
    while (i < 5)
    {
        func(i * 2);
        i = i + 1;
    }
    return (0);
}

