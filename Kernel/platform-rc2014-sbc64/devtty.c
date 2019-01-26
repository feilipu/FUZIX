#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdbool.h>
#include <tty.h>
#include <devtty.h>
#include <rc2014.h>

__sfr __at 0xf8 cpld_status;
__sfr __at 0xf9 cpld_data;

static char tbuf1[TTYSIZ];
static char tbuf2[TTYSIZ];
static char tbuf3[TTYSIZ];

static uint8_t sleeping;

struct s_queue ttyinq[NUM_DEV_TTY + 1] = {	/* ttyinq[0] is never used */
	{NULL, NULL, NULL, 0, 0, 0},
	{tbuf1, tbuf1, tbuf1, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf2, tbuf2, tbuf2, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf3, tbuf3, tbuf3, TTYSIZ, 0, TTYSIZ / 2},
};

static tcflag_t uart0_mask[4] = {
	_ISYS,
	_OSYS,
	_CSYS,
	_LSYS
};

static tcflag_t uart1_mask[4] = {
	_ISYS,
	_OSYS,
	CSIZE|CSTOPB|PARENB|PARODD|_CSYS,
	_LSYS
};

static tcflag_t uart2_mask[4] = {
	_ISYS,
	/* FIXME: break */
	_OSYS,
	/* FIXME CTS/RTS */
	CSIZE|CBAUD|CSTOPB|PARENB|PARODD|_CSYS,
	_LSYS,
};

tcflag_t *termios_mask[NUM_DEV_TTY + 1] = {
	NULL,
	uart0_mask,
	uart1_mask,
	uart2_mask
};

uint8_t sio_r[] = {
	0x03, 0xC1,
	0x04, 0xC4,
	0x05, 0xEA
};

static void sio2_setup(uint8_t minor, uint8_t flags)
{
	struct termios *t = &ttydata[minor].termios;
	uint8_t r;

	used(flags);

	/* Set bits per character */
	sio_r[1] = 0x01 | ((t->c_cflag & CSIZE) << 2);
	r = 0xC4;
	if (t->c_cflag & CSTOPB)
		r |= 0x08;
	if (t->c_cflag & PARENB)
		r |= 0x01;
	if (t->c_cflag & PARODD)
		r |= 0x02;
	sio_r[3] = r;
	sio_r[5] = 0x8A | ((t->c_cflag & CSIZE) << 1);
}

void tty_setup(uint8_t minor, uint8_t flags)
{
	if (minor == 1)
		return;
	sio2_setup(minor, flags);
	sio2_otir(SIO0_BASE + 2 * (minor - 1));
	/* We need to do CTS/RTS support and baud setting on channel 2
	   yet */
}

int tty_carrier(uint8_t minor)
{
        uint8_t c;
        if (minor == 1)
        	return 1;
	else if (minor == 2) {
		SIOA_C = 0;
		c = SIOA_C;
	} else {
		SIOB_C = 0;
		c = SIOB_C;
	}
	if (c & 0x8)
		return 1;
	return 0;
}

void tty_pollirq_sio(void)
{
	static uint8_t old_ca, old_cb;
	uint8_t ca, cb;
	uint8_t progress;

	/* Check for an interrupt */
	SIOA_C = 0;
	if (!(SIOA_C & 2))
		return;

	/* FIXME: need to process error/event interrupts as we can get
	   spurious characters or lines on an unused SIO floating */
	do {
		progress = 0;
		SIOA_C = 0;		// read register 0
		ca = SIOA_C;
		/* Input pending */
		if ((ca & 1) && !fullq(&ttyinq[1])) {
			progress = 1;
			tty_inproc(1, SIOA_D);
		}
		/* Break */
		if (ca & 2)
			SIOA_C = 2 << 5;
		/* Output pending */
		if ((ca & 4) && (sleeping & 2)) {
			tty_outproc(1);
			sleeping &= ~2;
			SIOA_C = 5 << 3;	// reg 0 CMD 5 - reset transmit interrupt pending
		}
		/* Carrier changed */
		if ((ca ^ old_ca) & 8) {
			if (ca & 8)
				tty_carrier_raise(1);
			else
				tty_carrier_drop(1);
		}
		SIOB_C = 0;		// read register 0
		cb = SIOB_C;
		if ((cb & 1) && !fullq(&ttyinq[2])) {
			tty_inproc(2, SIOB_D);
			progress = 1;
		}
		if ((cb & 4) && (sleeping & 4)) {
			tty_outproc(2);
			sleeping &= ~4;
			SIOB_C = 5 << 3;	// reg 0 CMD 5 - reset transmit interrupt pending
		}
		if ((cb ^ old_cb) & 8) {
			if (cb & 8)
				tty_carrier_raise(2);
			else
				tty_carrier_drop(2);
		}
	} while(progress);
}

void tty_poll_cpld(void)
{
	uint8_t ca;

	ca = cpld_status;
	if (ca & 1)
		tty_inproc(1, cpld_data);
}

void tty_putc(uint8_t minor, unsigned char c)
{
	if (minor == 1)
		cpld_bitbang(c);
	if (minor == 2)
		SIOA_D = c;
	else if (minor == 2)
		SIOB_D = c;
}

/* We will need this for SIO once we implement flow control signals */
void tty_sleeping(uint8_t minor)
{
	sleeping |= (1 << minor);
}

/* Be careful here. We need to peek at RR but we must be sure nobody else
   interrupts as we do this. Really we want to switch to irq driven tx ints
   on this platform I think. Need to time it and see

   An asm common level tty driver might be a better idea

   Need to review this we should be ok as the IRQ handler always leaves
   us pointing at RR0 */
ttyready_t tty_writeready(uint8_t minor)
{
	irqflags_t irq;
	uint8_t c;

	/* Bitbanged so trick the kernel into yielding when appropriate */
	if (minor == 1)
		return need_reschedule() ? TTY_READY_SOON: TTY_READY_NOW;
	irq = di();
	if (minor == 2) {
		SIOA_C = 0;	/* read register 0 */
		c = SIOA_C;
		irqrestore(irq);
		if (c & 0x04)	/* THRE? */
			return TTY_READY_NOW;
		return TTY_READY_SOON;
	} else if (minor == 3) {
		SIOB_C = 0;	/* read register 0 */
		c = SIOB_C;
		irqrestore(irq);
		if (c & 0x04)	/* THRE? */
			return TTY_READY_NOW;
		return TTY_READY_SOON;
	}
	irqrestore(irq);
	return TTY_READY_NOW;
}

void tty_data_consumed(uint8_t minor)
{
	used(minor);
}

/* kernel writes to system console -- never sleep! */
void kputchar(char c)
{
	/* Always use the bitbang port - no need for write waits therefore */
	if (c == '\n')
		tty_putc(TTYDEV - 512, '\r');
	tty_putc(TTYDEV - 512, c);
}