#include <stdio.h>

int main(void)
{
    int pain;
    int fruit;
    int total;

    scanf("%d%d", &pain, &fruit);
    total = 3 * pain + 2 * fruit;
    // ? pain, fruit, total
    printf("pain |%d|\n", pain * 3);
    printf("fruit|%d|\n", fruit * 2);
    puts("-----------");
    printf("total|%d|\n", total);
    return 0;
}
