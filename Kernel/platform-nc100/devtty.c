#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdbool.h>
#include <devtty.h>
#include <device.h>
#include <vt.h>
#include <tty.h>

#undef  DEBUG			/* UNdefine to delete debug code sequences */

__sfr __at 0xC0 uarta;
__sfr __at 0xC1 uartb;

__sfr __at 0x90 irqmap;

__sfr __at 0xB0 kmap0;
__sfr __at 0xB1 kmap1;
__sfr __at 0xB2 kmap2;
__sfr __at 0xB3 kmap3;
__sfr __at 0xB4 kmap4;
__sfr __at 0xB5 kmap5;
__sfr __at 0xB6 kmap6;
__sfr __at 0xB7 kmap7;
__sfr __at 0xB8 kmap8;
__sfr __at 0xB9 kmap9;

char tbuf1[TTYSIZ];
char tbuf2[TTYSIZ];

struct s_queue ttyinq[NUM_DEV_TTY + 1] = {	/* ttyinq[0] is never used */
	{NULL, NULL, NULL, 0, 0, 0},
	{tbuf1, tbuf1, tbuf1, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf2, tbuf2, tbuf2, TTYSIZ, 0, TTYSIZ / 2}
};

static void nap(void)
{
}

/* tty1 is the screen tty2 is the serial port */

int nc100_tty_open(uint8_t minor, uint16_t flag)
{
	int err;
	if (!minor)
		minor = udata.u_ptab->p_tty;

	err = tty_open(minor, flag);
	if (err)
		return err;
	if (minor == 2)
		mod_control(0, 0x10);	/* turn on the line driver */
	nap();
	mod_control(0x06, 0x01);	/* 9600 baud */
	return (0);
}


int nc100_tty_close(uint8_t minor)
{
	tty_close(minor);
	if (minor == 2)
		mod_control(0x10, 0);	/* turn off the line driver */
	return (0);
}

/* Output for the system console (kprintf etc) */
void kputchar(char c)
{
	if (c == '\n')
		tty_putc(1, '\r');
	tty_putc(1, c);
}

bool tty_writeready(uint8_t minor)
{
	uint8_t c;
	if (minor == 1)
		return 1;
	c = uartb;
	return c & 1;
}

void tty_putc(uint8_t minor, unsigned char c)
{
	minor;
	if (minor == 1) {
		vtoutput(&c, 1);
		return;
	}
	uarta = c;
}

/* Called to set baud rate etc */
void tty_setup(uint8_t minor)
{
    minor;
}

/* For the moment */
int tty_carrier(uint8_t minor)
{
    minor;
    return 1;
}


#define SER_INIT	0x4F	/*  1 stop,no parity,8bit,16x */
#define SER_RXTX	0x37

void nc100_tty_init(void)
{
  /* Reset the 8251 */
  mod_control(0x00, 0x08);
  nap();
  mod_control(0x08, 0x00);
  nap();
  uartb = SER_INIT;
  nap();
  uartb = SER_RXTX;
  nap();
  uarta;
  uarta;
}

static uint8_t keymap[10];
static uint8_t keyin[10];
static uint8_t keybyte, keybit;
static uint8_t newkey;
static int keysdown = 0;
static uint8_t shiftmask[10] = {
	3, 3, 2, 0, 0, 0, 0, 0x10, 0, 0
};

static void keyproc(void)
{
	int i;
	uint8_t key;

	for (i = 0; i < 10; i++) {
		key = keyin[i] ^ keymap[i];
		if (key) {
			int n;
			int m = 128;
			for (n = 0; n < 8; n++) {
				if ((key & m) && (keymap[i] & m)) {
					if (!(shiftmask[i] & m))
						keysdown--;
				}
				if ((key & m) && !(keymap[i] & m)) {
					if (!(shiftmask[i] & m))
						keysdown++;
					keybyte = i;
					keybit = n;
					newkey = 1;
				}
				m >>= 1;
			}
		}
		keymap[i] = keyin[i];
	}
}

static uint8_t keyboard[10][8] = {
	{0, 0, 0, 10, '?' /*left */ , 0, 0, 0},
	{0, '5', 0, 0, ' ', 27, 0, 0},
	{0, 0, 0, 0, '\t', '1', 0, 0},
	{'d', 's', 0, 'e', 'w', 'q', '2', '3'},
	{'f', 'r', 0, 'a', 'x', 'z', 0, '4'},
	{'c', 'g', 'y', 't', 'v', 'b', 0, 0},
	{'n', 'h', '/', '#', '?' /*right */ , 127, '?' /*down */ , '6'},
	{'k', 'm', 'u', 0, '?' /*up */ , '\\', '7', '='},
	{',', 'j', 'i', '\'', '[', ']', '-', '8'},
	{'.', 'o', 'l', ';', 'p', 8, '9', '0'}
};

static uint8_t shiftkeyboard[10][8] = {
	{0, 0, 0, 10, '?' /*left */ , 0, 0, 0},
	{0, '%', 0, 0, ' ', 3, 0, 0},
	{0, 0, 0, 0, '\t', '!', 0, 0},
	{'D', 'S', 0, 'E', 'W', 'Q', '"', '?' /* pound */ },
	{'F', 'R', 0, 'A', 'X', 'Z', 0, '$'},
	{'C', 'G', 'Y', 'T', 'V', 'B', 0, 0},
	{'N', 'H', '?', '~', '?' /*right */ , 127, '?' /*down */ , '^'},
	{'K', 'M', 'U', 0, '?' /*up */ , '|', '&', '+'},
	{'<', 'J', 'I', '@', '{', '}', '_', '*'},
	{'>', 'O', 'L', ':', 'P', 8, '(', ')'}
};

static uint8_t capslock = 0;

static void keydecode(void)
{
	uint8_t c;

	if (keybyte == 2 && keybit == 7) {
		capslock = 1 - capslock;
		return;
	}

	if (keymap[0] & 3)	/* shift */
		c = shiftkeyboard[keybyte][keybit];
	else
		c = keyboard[keybyte][keybit];
	if (keymap[1] & 2) {	/* control */
		if (c > 31 && c < 96)
			c &= 31;
	}
	if (keymap[1] & 1) {	/* function: not yet used */
		;
	}
//    kprintf("char code %d\n", c);
	if (keymap[2] & 1) {	/* symbol */
		;
	}
	if (capslock && c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (keymap[7] & 0x10) {	/* menu: not yet used */
		;
	}
	tty_inproc(1, c);
}


void platform_interrupt(void)
{
	uint8_t a = irqmap;
	uint8_t c;
	if (!(a & 2))
		wakeup(&ttydata[2]);
	if (!(a & 1)) {
		/* work around sdcc bug */
		c = uarta;
		tty_inproc(2, c);
	}
	if (!(a & 8)) {
		keyin[0] = kmap0;
		keyin[1] = kmap1;
		keyin[2] = kmap2;
		keyin[3] = kmap3;
		keyin[4] = kmap4;
		keyin[5] = kmap5;
		keyin[6] = kmap6;
		keyin[7] = kmap7;
		keyin[8] = kmap8;
		keyin[9] = kmap9;	/* This resets the scan for 10mS on */

		newkey = 0;
		keyproc();
		if (keysdown < 3 && newkey)
			keydecode();
		timer_interrupt();
	}

	/* clear the mask */
	irqmap = a;
}

/* This is used by the vt asm code, but needs to live at the top of the kernel */
uint16_t cursorpos;
