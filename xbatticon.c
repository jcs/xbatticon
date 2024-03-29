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

#ifdef __linux__
#define _GNU_SOURCE
#endif

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

#ifdef __OpenBSD__
#include <machine/apmvar.h>
#endif

#ifdef __linux__
#include <time.h>
#include <dirent.h>
#include <errno.h>
#define timespecsub(a,b,d) ((d)->tv_sec = (a)->tv_sec - (b)->tv_sec)
#endif

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
#include "icons/icon_charging.xpm"

struct {
	Display *dpy;
	int screen;
	Window win;
	XWMHints hints;
	GC gc;
} xinfo = { 0 };

struct {
	int apmfd;
	int remaining;
	int ac;
} power = { 0, -1, 0 };

struct icon_map_entry {
	char **xpm;
	int value;
#define CHARGING_ICON_VALUE -1
	Pixmap pm;
	Pixmap pm_mask;
	XpmAttributes pm_attrs;
	Pixmap charging_pm;
	Pixmap charging_pm_mask;
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
	{ icon_charging_xpm, CHARGING_ICON_VALUE },
};

extern char *__progname;

void		killer(int sig);
void		usage(void);
void		read_power(void);
void		update_power(void);
void		update_icon(void);
void		build_charging_icon(struct icon_map_entry *);
int		battfd(void);

int		exit_msg[2];
int		power_check_secs = 10;
struct timespec	last_power_check;
struct icon_map_entry *charging_icon;

#define WINDOW_WIDTH	200
#define WINDOW_HEIGHT	100

#ifdef __OpenBSD__
#define APMDEV "/dev/apm"

int
battfd(void)
{
	int fd;

	fd = open(APMDEV, O_RDONLY);
	if (fd == -1)
		perror(APMDEV);
	return fd;
}

#elif defined(__linux__)
#define SYSFS_POWER_SUPPLIES "/sys/class/power_supply/"
#define SYSFS_BATTERY_TYPE "Battery"

int
battfd(void)
{
	struct dirent *supply;
	int capafd = -1;
	DIR *supplies;

	supplies = opendir(SYSFS_POWER_SUPPLIES);
	if (supplies == NULL) {
		perror(SYSFS_POWER_SUPPLIES);
		return -1;
	}

	while (1) {
		char type_str[sizeof(SYSFS_BATTERY_TYPE)-1];
		int supply_dir;
		int typefd;

		errno = 0;
		supply = readdir(supplies);
		if (errno) {
			perror(SYSFS_POWER_SUPPLIES);
			break;
		}

		if (supply == NULL)
			break;

		supply_dir = openat(dirfd(supplies), supply->d_name, O_RDONLY | O_DIRECTORY);
		if (supply_dir == -1)
			continue;

		typefd = openat(supply_dir, "type", O_RDONLY);
		if (typefd == -1) {
			close(supply_dir);
			continue;
		}

		if (read(typefd, type_str, sizeof(type_str)) != sizeof(type_str)) {
			close(supply_dir);
			close(typefd);
			continue;
		}

		close(typefd);
		if (memcmp(type_str, SYSFS_BATTERY_TYPE, sizeof(type_str))) {
			close(supply_dir);
			continue;
		}

		capafd = openat(supply_dir, "capacity", O_RDONLY);
		close(supply_dir);
		if (capafd != -1)
			break;
	}

	closedir(supplies);
	return capafd;
}

#else
#error "reading battery status is not supported on this platform"
#endif

int
main(int argc, char* argv[])
{
	XEvent event;
	XSizeHints *hints;
	XGCValues gcv;
	struct pollfd pfd[2];
	struct sigaction act;
	struct timespec now, delta;
	char *display = NULL;
	long sleep_secs;
	int ch, i;

	while ((ch = getopt(argc, argv, "d:i:")) != -1) {
		switch (ch) {
		case 'd':
			display = optarg;
			break;
		case 'i':
			power_check_secs = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((power.apmfd = battfd()) == -1)
		err(1, "failed to open the battery");

	if (!(xinfo.dpy = XOpenDisplay(display)))
		errx(1, "can't open display %s", XDisplayName(display));

#ifdef __OpenBSD_
	if (pledge("stdio") == -1)
		err(1, "pledge");
#endif

	/* setup exit handler pipe that we'll poll on */
	if (pipe2(exit_msg, O_CLOEXEC) != 0)
		err(1, "pipe2");
	act.sa_handler = killer;
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	xinfo.screen = DefaultScreen(xinfo.dpy);
	xinfo.win = XCreateSimpleWindow(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
	    BlackPixel(xinfo.dpy, xinfo.screen),
	    WhitePixel(xinfo.dpy, xinfo.screen));
	gcv.foreground = 1;
	gcv.background = 0;
	xinfo.gc = XCreateGC(xinfo.dpy, xinfo.win, GCForeground | GCBackground,
	    &gcv);

	/* load XPMs */
	for (i = 0; i < sizeof(icon_map) / sizeof(icon_map[0]); i++) {
		if (XpmCreatePixmapFromData(xinfo.dpy,
		    RootWindow(xinfo.dpy, xinfo.screen),
		    icon_map[i].xpm, &icon_map[i].pm,
		    &icon_map[i].pm_mask, &icon_map[i].pm_attrs) != 0)
			errx(1, "XpmCreatePixmapFromData failed");

		if (icon_map[i].value == CHARGING_ICON_VALUE)
			charging_icon = &icon_map[i];
	}

	/* pre-compute charging icons */
	for (i = 0; i < sizeof(icon_map) / sizeof(icon_map[0]); i++)
		build_charging_icon(&icon_map[i]);

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

	update_power();

	xinfo.hints.initial_state = IconicState;
	xinfo.hints.flags |= StateHint;
	XSetWMHints(xinfo.dpy, xinfo.win, &xinfo.hints);
	XMapWindow(xinfo.dpy, xinfo.win);

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
		if (icon_map[i].charging_pm)
			XFreePixmap(xinfo.dpy, icon_map[i].charging_pm);
		if (icon_map[i].charging_pm_mask)
			XFreePixmap(xinfo.dpy, icon_map[i].charging_pm_mask);
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
		"[-d display] [-i interval]");
	exit(1);
}

#ifdef __OpenBSD__
void
read_power(void)
{
	struct apm_power_info apm_info;

	if (ioctl(power.apmfd, APM_IOC_GETPOWER, &apm_info) == -1)
		err(1, "APM_IOC_GETPOWER");

	if (apm_info.battery_life == APM_BATT_LIFE_UNKNOWN)
		power.remaining = 0;
	else
		power.remaining = apm_info.battery_life;

	power.ac = (apm_info.ac_state == APM_AC_ON);
}
#endif

#ifdef __linux__
void
read_power(void)
{
	char bp[4] = { 0, };

	if (lseek(power.apmfd, 0, SEEK_SET) == -1) {
		perror ("SEEK_SET");
		power.remaining = 0;
		return;
	}

	if (read(power.apmfd, bp, 3) == -1) {
		perror ("read");
		power.remaining = 0;
		return;
	}

	power.remaining = atoi(bp);
}
#endif

void
update_power(void)
{
	int last_ac = power.ac;
	int last_remaining = power.remaining;

	clock_gettime(CLOCK_MONOTONIC, &last_power_check);

	read_power();

	if (power.remaining > 100)
		power.remaining = 100;

	/* well timmy, sometimes batteries die a little bit when we use them */
	if (power.ac && power.remaining >= 96)
		power.remaining = 100;

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
	int i, rc, xo = 0, yo = 0, icon = 0;

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
	for (i = (sizeof(icon_map) / sizeof(icon_map[0])) - 1; i >= 0; i--) {
		if (icon_map[i].value < 0)
			continue;

		if (power.remaining > icon_map[i].value) {
			icon = i;
			break;
		}
	}

	/* update the icon */
	if (power.ac) {
		xinfo.hints.icon_pixmap = icon_map[icon].charging_pm;
		xinfo.hints.icon_mask = icon_map[icon].charging_pm_mask;
	} else {
		xinfo.hints.icon_pixmap = icon_map[icon].pm;
		xinfo.hints.icon_mask = icon_map[icon].pm_mask;
	}
	xinfo.hints.flags = IconPixmapHint | IconMaskHint;
	XSetWMHints(xinfo.dpy, xinfo.win, &xinfo.hints);

	/* and draw it in the center of the window */
	XGetWindowAttributes(xinfo.dpy, xinfo.win, &xgwa);
	xo = (xgwa.width / 2) - (icon_map[icon].pm_attrs.width / 2);
	yo = (xgwa.height / 2) - (icon_map[icon].pm_attrs.height / 2);
	XSetClipMask(xinfo.dpy, xinfo.gc, (power.ac ?
	    icon_map[icon].charging_pm_mask : icon_map[icon].pm_mask));
	XSetClipOrigin(xinfo.dpy, xinfo.gc, xo, yo);
	XClearWindow(xinfo.dpy, xinfo.win);
	XSetFunction(xinfo.dpy, xinfo.gc, GXcopy);
	XCopyArea(xinfo.dpy,
	    (power.ac ? icon_map[icon].charging_pm : icon_map[icon].pm),
	    xinfo.win, xinfo.gc,
	    0, 0,
	    icon_map[icon].pm_attrs.width, icon_map[icon].pm_attrs.height,
	    xo, yo);
}

void
build_charging_icon(struct icon_map_entry *icon)
{
	XWindowAttributes xgwa;
	XGCValues gcv;
	GC gc;
	int xo = 10;

	XGetWindowAttributes(xinfo.dpy, xinfo.win, &xgwa);
	icon->charging_pm = XCreatePixmap(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    icon->pm_attrs.width, icon->pm_attrs.height,
	    xgwa.depth);
	icon->charging_pm_mask = XCreatePixmap(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    icon->pm_attrs.width, icon->pm_attrs.height,
	    1);

	gcv.foreground = 1;
	gcv.background = 0;
	gc = XCreateGC(xinfo.dpy, icon->charging_pm_mask,
	    GCForeground | GCBackground, &gcv);

	/* copy masks */
	XCopyPlane(xinfo.dpy, icon->pm_mask, icon->charging_pm_mask,
	    gc,
	    0, 0,
	    icon->pm_attrs.width, icon->pm_attrs.height,
	    xo, 0,
	    1);
	XSetFunction(xinfo.dpy, gc, GXor);
	XCopyPlane(xinfo.dpy, charging_icon->pm_mask, icon->charging_pm_mask,
	    gc,
	    0, 0,
	    charging_icon->pm_attrs.width,
	    charging_icon->pm_attrs.height,
	    0, 0,
	    1);

	/* copy icons */
	XSetFunction(xinfo.dpy, xinfo.gc, GXcopy);
	XCopyArea(xinfo.dpy, icon->pm, icon->charging_pm, xinfo.gc,
	    0, 0,
	    icon->pm_attrs.width,
	    icon->pm_attrs.height,
	    xo, 0);
	XSetClipMask(xinfo.dpy, xinfo.gc, charging_icon->pm_mask);
	XCopyArea(xinfo.dpy, charging_icon->pm, icon->charging_pm, xinfo.gc,
	    0, 0,
	    charging_icon->pm_attrs.width,
	    charging_icon->pm_attrs.height,
	    0, 0);
	XSetClipMask(xinfo.dpy, xinfo.gc, None);

	XFreeGC(xinfo.dpy, gc);
}
