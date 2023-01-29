/* vim:ts=8
 *
 * Copyright (c) 2023 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/xpm.h>

#include <machine/apmvar.h>
#define APMDEV "/dev/apm"

#include "icons/icon_000.xpm"
#include "icons/icon_001.xpm"
#include "icons/icon_002.xpm"
#include "icons/icon_003.xpm"
#include "icons/icon_004.xpm"
#include "icons/icon_005.xpm"
#include "icons/icon_006.xpm"
#include "icons/icon_010.xpm"
#include "icons/icon_015.xpm"
#include "icons/icon_020.xpm"
#include "icons/icon_025.xpm"
#include "icons/icon_030.xpm"
#include "icons/icon_035.xpm"
#include "icons/icon_040.xpm"
#include "icons/icon_045.xpm"
#include "icons/icon_050.xpm"
#include "icons/icon_055.xpm"
#include "icons/icon_060.xpm"
#include "icons/icon_065.xpm"
#include "icons/icon_070.xpm"
#include "icons/icon_075.xpm"
#include "icons/icon_080.xpm"
#include "icons/icon_085.xpm"
#include "icons/icon_090.xpm"
#include "icons/icon_095.xpm"
#include "icons/icon_100.xpm"

struct {
	Display *dpy;
	int screen;
	Window win;
	XWMHints hints;
	GC gc;
	XGCValues gcv;
} xinfo = { 0 };

struct {
	int apmfd;
	int remaining;
	int ac;
} power = { 0, -1, 0 };

struct icon_map_entry {
	char **xpm;
	int value;
	Pixmap pm;
	Pixmap pm_mask;
	XpmAttributes pm_attrs;
	Pixmap hidpi_pm;
	Pixmap hidpi_pm_mask;
	XpmAttributes hidpi_pm_attrs;
} icon_map[] = {
#define ICON_ENTRY(val) { icon_##val##_xpm, val }
	ICON_ENTRY(0),
	ICON_ENTRY(1),
	ICON_ENTRY(2),
	ICON_ENTRY(3),
	ICON_ENTRY(4),
	ICON_ENTRY(5),
	ICON_ENTRY(6),
	ICON_ENTRY(10),
	ICON_ENTRY(15),
	ICON_ENTRY(20),
	ICON_ENTRY(25),
	ICON_ENTRY(30),
	ICON_ENTRY(35),
	ICON_ENTRY(40),
	ICON_ENTRY(45),
	ICON_ENTRY(50),
	ICON_ENTRY(55),
	ICON_ENTRY(60),
	ICON_ENTRY(65),
	ICON_ENTRY(70),
	ICON_ENTRY(75),
	ICON_ENTRY(80),
	ICON_ENTRY(85),
	ICON_ENTRY(90),
	ICON_ENTRY(95),
	ICON_ENTRY(100),
};

const struct option longopts[] = {
	{ "display",	required_argument,	NULL,	'd' },
	{ "hidpi",	no_argument,		NULL,	'2' },
	{ NULL,		0,			NULL,	0 }
};

extern char *__progname;

void		killer(int sig);
void		usage(void);
void		update_power(void);
void		update_icon(void);
void		double_pixmap(Pixmap, int, int, Pixmap *, int);

int		exit_msg[2];
int		power_check_secs = 10;
int		hidpi_icon = 0;
struct timespec	last_power_check;

#define WINDOW_WIDTH	200
#define WINDOW_HEIGHT	100

int
main(int argc, char* argv[])
{
	XEvent event;
	XSizeHints *hints;
	struct pollfd pfd[2];
	struct sigaction act;
	struct timespec now, delta;
	char *display = NULL;
	long sleep_secs;
	int ch, i;

	while ((ch = getopt(argc, argv, "2d:")) != -1) {
		switch (ch) {
		case '2':
			hidpi_icon = 1;
			break;
		case 'd':
			display = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((power.apmfd = open(APMDEV, O_RDONLY)) == -1)
		err(1, "failed to open %s", APMDEV);

	/* setup exit handler pipe that we'll poll on */
	if (pipe2(exit_msg, O_CLOEXEC) != 0)
		err(1, "pipe2");
	act.sa_handler = killer;
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	if (!(xinfo.dpy = XOpenDisplay(display)))
		errx(1, "can't open display %s", XDisplayName(display));
	xinfo.screen = DefaultScreen(xinfo.dpy);
	xinfo.win = XCreateSimpleWindow(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
	    BlackPixel(xinfo.dpy, xinfo.screen),
	    WhitePixel(xinfo.dpy, xinfo.screen));
	xinfo.gcv.foreground = 1;
	xinfo.gcv.background = 0;
	xinfo.gc = XCreateGC(xinfo.dpy, xinfo.win, GCForeground | GCBackground,
	    &xinfo.gcv);

	/* load XPMs and scale for hidpi icon and window */
	for (i = 0; i < sizeof(icon_map) / sizeof(icon_map[0]); i++) {
		if (XpmCreatePixmapFromData(xinfo.dpy,
		    RootWindow(xinfo.dpy, xinfo.screen),
		    icon_map[i].xpm, &icon_map[i].pm,
		    &icon_map[i].pm_mask, &icon_map[i].pm_attrs) != 0)
			errx(1, "XpmCreatePixmapFromData failed");

		double_pixmap(icon_map[i].pm, icon_map[i].pm_attrs.width,
		    icon_map[i].pm_attrs.height, &icon_map[i].hidpi_pm, 0);
		double_pixmap(icon_map[i].pm_mask, icon_map[i].pm_attrs.width,
		    icon_map[i].pm_attrs.height, &icon_map[i].hidpi_pm_mask,
		    1);
		memcpy(&icon_map[i].hidpi_pm_attrs, &icon_map[i].pm_attrs,
		    sizeof(icon_map[i].hidpi_pm_attrs));
		icon_map[i].hidpi_pm_attrs.width *= 2;
		icon_map[i].hidpi_pm_attrs.height *= 2;
	}

	hints = XAllocSizeHints();
	if (!hints)
		err(1, "XAllocSizeHints");
	hints->flags = PMinSize | PMaxSize;
	hints->min_width = WINDOW_WIDTH;
	hints->min_height = WINDOW_HEIGHT;
	hints->max_width = WINDOW_WIDTH;
	hints->max_height = WINDOW_HEIGHT;
#if 0	/* disabled until progman displays minimize on non-dialog wins */
	XSetWMNormalHints(xinfo.dpy, xinfo.win, hints);
#endif

	XMapWindow(xinfo.dpy, xinfo.win);
	XIconifyWindow(xinfo.dpy, xinfo.win, xinfo.screen);

	update_power();

	memset(&pfd, 0, sizeof(pfd));
	pfd[0].fd = ConnectionNumber(xinfo.dpy);
	pfd[0].events = POLLIN;
	pfd[1].fd = exit_msg[0];
	pfd[1].events = POLLIN;

	/* we need to know when we're exposed */
	XSelectInput(xinfo.dpy, xinfo.win, ExposureMask);

	for (;;) {
		if (!XPending(xinfo.dpy)) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			timespecsub(&now, &last_power_check, &delta);

			if (delta.tv_sec > power_check_secs)
				sleep_secs = 0;
			else
				sleep_secs = (power_check_secs - delta.tv_sec);

			poll(pfd, 2, sleep_secs * 1000);
			if (pfd[1].revents)
				/* exit msg */
				break;

			if (!XPending(xinfo.dpy)) {
				update_power();
				continue;
			}
		}

		XNextEvent(xinfo.dpy, &event);

		switch (event.type) {
		case Expose:
			update_icon();
			break;
		}
	}

	for (i = 0; i < sizeof(icon_map) / sizeof(icon_map[0]); i++) {
		if (icon_map[i].pm)
			XFreePixmap(xinfo.dpy, icon_map[i].pm);
		if (icon_map[i].pm_mask)
			XFreePixmap(xinfo.dpy, icon_map[i].pm_mask);
		if (icon_map[i].hidpi_pm)
			XFreePixmap(xinfo.dpy, icon_map[i].hidpi_pm);
		if (icon_map[i].hidpi_pm_mask)
			XFreePixmap(xinfo.dpy, icon_map[i].hidpi_pm_mask);
	}

	XDestroyWindow(xinfo.dpy, xinfo.win);
	XFree(hints);
	XCloseDisplay(xinfo.dpy);

	return 0;
}

void
killer(int sig)
{
	if (write(exit_msg[1], &exit_msg, 1))
		return;

	warn("failed to exit cleanly");
	exit(1);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s %s\n", __progname,
		"[-d display] [-2]");
	exit(1);
}

void
update_power(void)
{
	struct apm_power_info apm_info;
	int last_ac = power.ac;
	int last_remaining = power.remaining;

	clock_gettime(CLOCK_MONOTONIC, &last_power_check);

	if (ioctl(power.apmfd, APM_IOC_GETPOWER, &apm_info) == -1)
		err(1, "APM_IOC_GETPOWER");

	if (apm_info.battery_life == APM_BATT_LIFE_UNKNOWN)
		power.remaining = 0;
	else if (apm_info.battery_life > 100)
		power.remaining = 100;
	else
		power.remaining = apm_info.battery_life;

	power.ac = (apm_info.ac_state == APM_AC_ON);

	if (power.ac != last_ac || power.remaining != last_remaining) {
#ifdef DEBUG
		printf("ac: %d, battery %d\n", power.ac, power.remaining);
#endif
		update_icon();
	}
}

void
update_icon(void)
{
	XTextProperty title_prop;
	XWindowAttributes xgwa;
	char title[50];
	char *titlep = (char *)&title;
	int i, rc, icon = 0;

	if (power.ac) {
		if (power.remaining >= 99)
			snprintf(title, sizeof(title), "Charged");
		else
			snprintf(title, sizeof(title), "Charging: %d%%",
			    power.remaining);
	} else
		snprintf(title, sizeof(title), "Battery: %d%%",
		    power.remaining);

	/* update icon and window titles */
	if (!(rc = XStringListToTextProperty(&titlep, 1, &title_prop)))
		errx(1, "XStringListToTextProperty");
	XSetWMIconName(xinfo.dpy, xinfo.win, &title_prop);
	XStoreName(xinfo.dpy, xinfo.win, title);

	/* find the icon that matches our battery percentage */
	for (i = sizeof(icon_map) / sizeof(icon_map[0]); i >= 0; i--) {
		if (power.remaining <= icon_map[i].value) {
			icon = i;
			break;
		}
	}

	/* update the icon */
	if (hidpi_icon) {
		xinfo.hints.icon_pixmap = icon_map[icon].hidpi_pm;
		xinfo.hints.icon_mask = icon_map[icon].hidpi_pm_mask;
	} else {
		xinfo.hints.icon_pixmap = icon_map[icon].pm;
		xinfo.hints.icon_mask = icon_map[icon].pm_mask;
	}
	xinfo.hints.flags = IconPixmapHint | IconMaskHint;
	XSetWMHints(xinfo.dpy, xinfo.win, &xinfo.hints);

	/* draw hidpi icon in center of window */
	XGetWindowAttributes(xinfo.dpy, xinfo.win, &xgwa);
	XSetFunction(xinfo.dpy, xinfo.gc, GXandInverted);
	XSetBackground(xinfo.dpy, xinfo.gc, 0UL);
	XSetForeground(xinfo.dpy, xinfo.gc, ~0UL);
	XCopyPlane(xinfo.dpy, icon_map[icon].hidpi_pm_mask, xinfo.win,
	    xinfo.gc, 0, 0,
	    icon_map[icon].hidpi_pm_attrs.width,
	    icon_map[icon].hidpi_pm_attrs.height,
	    (xgwa.width / 2) - (icon_map[icon].hidpi_pm_attrs.width / 2),
	    (xgwa.height / 2) - (icon_map[icon].hidpi_pm_attrs.height / 2),
	    1UL);
	XSetFunction(xinfo.dpy, xinfo.gc, GXor);
	XCopyArea(xinfo.dpy, icon_map[icon].hidpi_pm,
	    xinfo.win, xinfo.gc, 0, 0,
	    icon_map[icon].hidpi_pm_attrs.width,
	    icon_map[icon].hidpi_pm_attrs.height,
	    (xgwa.width / 2) - (icon_map[icon].hidpi_pm_attrs.width / 2),
	    (xgwa.height / 2) - (icon_map[icon].hidpi_pm_attrs.height / 2));
}

void
double_pixmap(Pixmap src, int srcwidth, int srcheight, Pixmap *ret, int mask)
{
	XImage *srcxi, *xi;
	XWindowAttributes xgwa;
	XGCValues gcv;
	GC gc;
	char *retbacking;
	unsigned long v;
	int y, x, xo, yo;
	int width = srcwidth * 2;
	int height = srcheight * 2;

	srcxi = XGetImage(xinfo.dpy, src, 0, 0, srcwidth, srcheight, ~0,
	    ZPixmap);

	retbacking = malloc(width * height * 4);
	xi = XCreateImage(xinfo.dpy, DefaultVisual(xinfo.dpy, xinfo.screen),
	    (mask ? 1 : 24), (mask ? XYBitmap : ZPixmap), 0,
	    (char *)retbacking, width, height, (mask ? 8 : 32), 0);
	if (!xi)
		errx(1, "XCreateImage failed");

	for (y = 0, yo = 0; y < srcheight; y++) {
		for (x = 0, xo = 0; x < srcwidth; x++) {
			v = XGetPixel(srcxi, x, y);
			XPutPixel(xi, xo, yo, v);
			XPutPixel(xi, xo, yo + 1, v);
			XPutPixel(xi, xo + 1, yo, v);
			XPutPixel(xi, xo + 1, yo + 1, v);
			xo += 2;
		}
		yo += 2;
	}

	XGetWindowAttributes(xinfo.dpy, xinfo.win, &xgwa);
	*ret = XCreatePixmap(xinfo.dpy, RootWindow(xinfo.dpy, xinfo.screen),
	    width, height, mask ? 1 : xgwa.depth);

	gcv.foreground = 1;
	gcv.background = 0;
	gc = XCreateGC(xinfo.dpy, *ret, GCForeground | GCBackground, &gcv);
	XPutImage(xinfo.dpy, *ret, gc, xi, 0, 0, 0, 0, width, height);

	XDestroyImage(xi); /* this will free retbacking */
	XDestroyImage(srcxi);
	XFreeGC(xinfo.dpy, gc);
}
