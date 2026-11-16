#include <stdio.h>

int main(void)
{
    int soldes[] = {3, -1, 5, -2};
    int credits = 0;
    int debits = 0;
    for (int i = 0; i < 4; i++)
    {
        if (soldes[i] < 0)
        {
            printf("%c|  |%d\n", 'A' + i, -soldes[i]);
            debits -= soldes[i];
        }
        else
        {
            printf("%c| %d|\n", 'A' + i, soldes[i]);
            credits += soldes[i];
        }
    }
    // ? credits, debits
    printf("NET %d\n", credits - debits);
    return 0;
}
