/*
KpOS Kernel v0.0.1
*/


void kernel_main(unsigned int magic, unsigned int mbi_addr)
{
    volatile unsigned short *vga =
        (volatile unsigned short *)0xB8000;

    const char *message;

    unsigned int row = 0;
    unsigned int col = 0;
    unsigned int i = 0;

    (void)mbi_addr;

    if (magic == 0x36D76289)
    {
        message =
            "KpOS Kernel\n"
            "System initialized.\n"
            "Welcome to KpOS!";
    }
    else
    {
        message = "KpOS: Invalid boot magic.";
    }

    while (message[i] != '\0' && row < 25)
    {
        char c = message[i++];

        if (c == '\n')
        {
            row++;
            col = 0;
            continue;
        }

        vga[row * 80 + col] =
            (unsigned short)((0x0F << 8) | (unsigned char)c);

        col++;

        if (col == 80)
        {
            col = 0;
            row++;
        }
    }

    for (;;)
    {
        __asm__ volatile ("cli; hlt");
    }
}
