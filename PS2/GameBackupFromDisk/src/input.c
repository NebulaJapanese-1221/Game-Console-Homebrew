#include "input.h"

static struct padButtonStatus pad_data __attribute__((aligned(64)));
static unsigned short last_buttons = 0;

void input_init(void)
{
    padInit(0);
    padPortOpen(0, 0, &pad_data);
}

int input_update(void)
{
    padRead(0, 0, &pad_data);
    unsigned short buttons = pad_data.btns;
    unsigned short pressed = buttons & ~last_buttons;
    last_buttons = buttons;
    return pressed;
}

void input_shutdown(void)
{
    padPortClose(0, 0);
    padEnd();
}
