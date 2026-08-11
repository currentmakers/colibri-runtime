#include "colibri/boot.h"

int main()
{
    int error = boot_initialize();

    if (error)
    {
        boot_error(error);
    } else
    {
        boot_ok();
    }
    return 0;
}
