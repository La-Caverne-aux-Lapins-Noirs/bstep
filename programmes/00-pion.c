#include <stdio.h>

int main(void)
{
    int x = 1;

    if (x < 3)
        x = x + 1;

    while (x < 5)
        x = x + 1;

    printf("Case %d\n", x);
    return 0;
}
