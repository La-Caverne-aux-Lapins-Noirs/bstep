#include <stdio.h>

int main(void)
{
    int total = 0;

    for (int r = 1; r <= 2; r++)
    {
        for (int c = 1; c <= 3; c++)
        {
            int charge = r * c;
            if (charge % 2 == 0)
                total += charge;
            else
                fprintf(stderr, "%d%d ", r, c);
        }
        // ? r, total
        printf("%d\n", total);
    }
    return 0;
}
