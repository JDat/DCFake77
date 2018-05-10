/* DCFake77 */
/* (c) 2016 Renzo Davoli. GPL v2+ */
/* inspired from minimal_clock.c (public domain sw) */

#include "dcfake77.h"

void gpioSetMode(unsigned gpio, unsigned mode)
{
	int reg, shift;

	reg   =  gpio/10;
	shift = (gpio%10) * 3;

	gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);
}

int gpioGetMode(unsigned gpio)
{
	int reg, shift;

	reg   =  gpio/10;
	shift = (gpio%10) * 3;

	return (*(gpioReg + reg) >> shift) & 7;
}

/* Values for pull-ups/downs off, pull-down and pull-up. */

#define PI_PUD_OFF  0
#define PI_PUD_DOWN 1
#define PI_PUD_UP   2

void gpioSetPullUpDown(unsigned gpio, unsigned pud)
{
	*(gpioReg + GPPUD) = pud;

	usleep(20);

	*(gpioReg + GPPUDCLK0 + PI_BANK) = PI_BIT;

	usleep(20);

	*(gpioReg + GPPUD) = 0;

	*(gpioReg + GPPUDCLK0 + PI_BANK) = 0;
}

int gpioRead(unsigned gpio)
{
	if ((*(gpioReg + GPLEV0 + PI_BANK) & PI_BIT) != 0) return 1;
	else                                         return 0;
}

void gpioWrite(unsigned gpio, unsigned level)
{
	if (level == 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
	else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

void gpioTrigger(unsigned gpio, unsigned pulseLen, unsigned level)
{
	if (level == 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
	else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;

	usleep(pulseLen);

	if (level != 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
	else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

/* Bit (1<<x) will be set if gpio x is high. */

uint32_t gpioReadBank1(void) { return (*(gpioReg + GPLEV0)); }
uint32_t gpioReadBank2(void) { return (*(gpioReg + GPLEV1)); }

/* To clear gpio x bit or in (1<<x). */

void gpioClearBank1(uint32_t bits) { *(gpioReg + GPCLR0) = bits; }
void gpioClearBank2(uint32_t bits) { *(gpioReg + GPCLR1) = bits; }

/* To set gpio x bit or in (1<<x). */

void gpioSetBank1(uint32_t bits) { *(gpioReg + GPSET0) = bits; }
void gpioSetBank2(uint32_t bits) { *(gpioReg + GPSET1) = bits; }

unsigned gpioHardwareRevision(void)
{
	static unsigned rev = 0;

	FILE * filp;
	char buf[512];
	char term;
	int chars=4; /* number of chars in revision string */

	if (rev) return rev;

	piModel = 0;

	filp = fopen ("/proc/cpuinfo", "r");

	if (filp != NULL)
	{
		while (fgets(buf, sizeof(buf), filp) != NULL)
		{
			if (piModel == 0)
			{
				if (!strncasecmp("model name", buf, 10))
				{
					if (strstr (buf, "ARMv6") != NULL)
					{
						piModel = 1;
						chars = 4;
						piPeriphBase = 0x20000000;
						piBusAddr = 0x40000000;
					}
					else if (strstr (buf, "ARMv7") != NULL)
					{
						piModel = 2;
						chars = 6;
						piPeriphBase = 0x3F000000;
						piBusAddr = 0xC0000000;
					}
				}
			}

			if (!strncasecmp("revision", buf, 8))
			{
				if (sscanf(buf+strlen(buf)-(chars+1),
							"%x%c", &rev, &term) == 2)
				{
					if (term != '\n') rev = 0;
				}
			}
		}

		fclose(filp);
	}
	return rev;
}

/* Returns the number of microseconds after system boot. Wraps around
   after 1 hour 11 minutes 35 seconds.
 */

uint32_t gpioTick(void) { return systReg[SYST_CLO]; }

/* Map in registers. */

static uint32_t * initMapMem(int fd, uint32_t addr, uint32_t len)
{
	return (uint32_t *) mmap(0, len,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_SHARED|MAP_LOCKED,
			fd, addr);
}

int gpioInitialise(void)
{
	int fd;

	gpioHardwareRevision(); /* sets piModel, needed for peripherals address */

	fd = open("/dev/mem", O_RDWR | O_SYNC) ;

	if (fd < 0)
	{
		fprintf(stderr,
				"This program needs root privileges.  Try using sudo\n");
		return -1;
	}

	gpioReg  = initMapMem(fd, GPIO_BASE, GPIO_LEN);
	systReg  = initMapMem(fd, SYST_BASE, SYST_LEN);
	clkReg   = initMapMem(fd, CLK_BASE,  CLK_LEN);

	close(fd);

	if ((gpioReg == MAP_FAILED) ||
			(systReg == MAP_FAILED) ||
			(clkReg == MAP_FAILED))
	{
		fprintf(stderr,
				"Bad, mmap failed\n");
		return -1;
	}
	return 0;
}

int initClock(int clock, int source, int divI, int divF, int MASH)
{
	int ctl[] = {CLK_GP0_CTL, CLK_GP2_CTL};
	int div[] = {CLK_GP0_DIV, CLK_GP2_DIV};
	int src[CLK_SRCS] =
	{CLK_CTL_SRC_PLLD,
		CLK_CTL_SRC_OSC,
		CLK_CTL_SRC_HDMI,
		CLK_CTL_SRC_PLLC};

	int clkCtl, clkDiv, clkSrc;
	uint32_t setting;

	if ((clock  < 0) || (clock  > 1))    return -1;
	if ((source < 0) || (source > 3 ))   return -2;
	if ((divI   < 2) || (divI   > 4095)) return -3;
	if ((divF   < 0) || (divF   > 4095)) return -4;
	if ((MASH   < 0) || (MASH   > 3))    return -5;

	clkCtl = ctl[clock];
	clkDiv = div[clock];
	clkSrc = src[source];

	clkReg[clkCtl] = CLK_PASSWD | CLK_CTL_KILL;

	/* wait for clock to stop */

	while (clkReg[clkCtl] & CLK_CTL_BUSY)
	{
		usleep(10);
	}

	clkReg[clkDiv] =
		(CLK_PASSWD | CLK_DIV_DIVI(divI) | CLK_DIV_DIVF(divF));

	usleep(10);

	clkReg[clkCtl] =
		(CLK_PASSWD | CLK_CTL_MASH(MASH) | CLK_CTL_SRC(clkSrc));

	usleep(10);

	//clkReg[clkCtl] |= (CLK_PASSWD | CLK_CTL_ENAB);
	return 0;
}

int termClock(int clock)
{
	int ctl[] = {CLK_GP0_CTL, CLK_GP2_CTL};

	int clkCtl;

	if ((clock  < 0) || (clock  > 1))    return -1;

	clkCtl = ctl[clock];

	clkReg[clkCtl] = CLK_PASSWD | CLK_CTL_KILL;

	/* wait for clock to stop */

	while (clkReg[clkCtl] & CLK_CTL_BUSY)
	{
		usleep(10);
	}
}

int clkHigh(int clock) {
	int ctl[] = {CLK_GP0_CTL, CLK_GP2_CTL};
	int clkCtl;
	if ((clock  < 0) || (clock  > 1))    return -1;
	clkCtl = ctl[clock];
	clkReg[clkCtl] |= (CLK_PASSWD | CLK_CTL_ENAB);
}

int clkLow(int clock) {
	int ctl[] = {CLK_GP0_CTL, CLK_GP2_CTL};
	int clkCtl;
	if ((clock  < 0) || (clock  > 1))    return -1;
	clkCtl = ctl[clock];
	clkReg[clkCtl] = CLK_PASSWD | (clkReg[clkCtl] & ~CLK_CTL_ENAB);
}
