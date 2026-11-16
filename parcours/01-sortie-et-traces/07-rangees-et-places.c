#include <stdio.h>

int main(void)
{
    for (int rangee = 0; rangee < 2; rangee++)
    {
        for (int place = 0; place < 2; place++)
        {
            // ? rangee, place
            printf("[%d%d]", rangee, place);
        }
        putchar('\n');
    }
    return 0;
}
