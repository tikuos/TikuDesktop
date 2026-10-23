/* Latin Extended-A uppercase pairs change parity within the block. */
#include <tiku_office_flow.c>
int main(void)
{
    const unsigned lower[] = {0x13a, 0x142, 0x144, 0x17a, 0x17c, 0x17e};
    int fails = 0;
    for (unsigned i = 0; i < sizeof lower / sizeof lower[0]; i++) {
        if (upper(lower[i]) != lower[i] - 1 || upper(lower[i] - 1) != lower[i] - 1) {
            printf("FAIL D-088 U+%04X\n", lower[i]); fails++;
        }
    }
    return fails != 0;
}
