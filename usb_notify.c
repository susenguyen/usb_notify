/*
 * Completely absurd!
 * When you disable the device, the /dev device file is removed,
 * hence causing an IN_DELETE event which re-enables the device, and so on...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>	/* open() */
#include <signal.h>
#include <errno.h>
#include <sys/inotify.h>

#define PCI_NAMES_LEN 64
#define DEVICE_EVENT "/dev"
#define DEVICE_NAME_LEN 32
#define LINK_LEN 256
#define CLASS "/sys/class/block/"
#define SYS_BLOCK_LEN 32	/* /sys/class/block/sd? */
#define SYS_DEVICES_LEN 256	/* /sys/devices/pci.../0000:.../driver/unbind */

#define EVENT_BUF_LEN sizeof(struct inotify_event) + DEVICE_NAME_LEN

#define handle_error(errno) do { 		\
	printf("Error: %s\n", strerror(errno)); \
	exit(EXIT_FAILURE); 			\
} while(0)

#define handle_error_sub(errno) do { 		\
	printf("Error: %s\n", strerror(errno)); \
	return -1;				\
} while(0)

#define syntax_error(arg) do {			\
	printf("Syntax: %s sdX\n", arg);	\
	exit(EXIT_FAILURE);			\
} while(0)

static char pci_bus[PCI_NAMES_LEN];
static char pci_address[PCI_NAMES_LEN];

int check_pci_bus(const char dev[], const char sys_devices_pci[])
{
	char sys_class_block[SYS_BLOCK_LEN],
		buf[LINK_LEN];

	/* Build the /sys/class/block/sd? string */
	strcpy(sys_class_block, CLASS);
	stpcpy(sys_class_block + strlen(CLASS), dev);
	printf("inspecting: %s\n", sys_class_block);
	printf("checking for link to: %s\n", sys_devices_pci);

	/* Find the link to /sys/device for /sys/class/block/sd? */
	if (readlink(sys_class_block, buf, LINK_LEN) == -1)
		return 1;	/* Not an error, just file not found */

	/* Does the link to /sys/device/... contain pci_address? */
	if (strstr(buf, sys_devices_pci))
		return 0;

	return -1;
}

int update_sys_file(const char path[])
{
	int fd, count;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		handle_error_sub(errno);

	count = write(fd, pci_address, strlen(pci_address));
	if (count < strlen(pci_address)) {
		printf("Wrote %d bytes instead of %d\n", count, strlen(pci_address));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

static void sig_handler(int sig)
{
	/* Re-enables the device and exits for SIGINT */

	printf("echo \"%s\" > %s\n", pci_address, \
			"/sys/bus/pci/drivers/xhci_hcd/bind");
	update_sys_file("/sys/bus/pci/drivers/xhci_hcd/bind");

	printf("Caught SIGINT!\nExiting...\n");
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int ifd, wd;
	int count;
	char buf[EVENT_BUF_LEN], device_name[DEVICE_NAME_LEN],
		short_pci_path[SYS_DEVICES_LEN],
		sys_devices_pci_prefix[SYS_DEVICES_LEN],
		sys_devices_pci[SYS_DEVICES_LEN];
	struct inotify_event *event;

	if (argc != 3) {
		printf("Error!!!\nSyntax: %s pci_bus pci_address\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if ((strlen(argv[1]) >= PCI_NAMES_LEN) || (strlen(argv[2]) >= PCI_NAMES_LEN)) {
		printf("Error!!!\n%s or %s cannot exceed %d bytes\n",
							argv[1], argv[2], PCI_NAMES_LEN);
		exit(EXIT_FAILURE);
	}

	/* Build the pci_bus[] and pci_address from argv[]s */
	strcpy(pci_bus, argv[1]);
	strcpy(pci_address, argv[2]);

	/* From now on, static char pci_bus[] and pci_address[] are initialized */
	printf("Monitoring PCI bus %s on port %s\n", pci_bus, pci_address);

	/* Build the /sys/devices/pci.../0000:xxx string */
	strcpy(sys_devices_pci_prefix, "/sys/devices/");
	strcpy(sys_devices_pci_prefix + strlen(sys_devices_pci_prefix), pci_bus);
	strcpy(sys_devices_pci_prefix + strlen(sys_devices_pci_prefix), "/");
	strcpy(sys_devices_pci_prefix + strlen(sys_devices_pci_prefix), pci_address);

	/* Build the "short pci path" pci.../0000:xxxx string */
	strcpy(short_pci_path, pci_bus);
	strcpy(short_pci_path + strlen(pci_bus), "/");
	strcpy(short_pci_path + strlen(pci_bus) + 1, pci_address);

	ifd = inotify_init();
	if (ifd == -1)
		handle_error(errno);

	wd = inotify_add_watch(ifd, DEVICE_EVENT, IN_CREATE | IN_ONLYDIR | IN_DELETE);
	if (wd == -1)
		handle_error(errno);

	printf("Added watch %d on %s\n", wd, DEVICE_EVENT);

	/* Loop until /sys/class/block/[event->name] links to the USB PCI address */
	do {
		count = read(ifd, buf, EVENT_BUF_LEN);

		if (count == 0) {
			printf("Fatal error reading the notifier\n");
			exit(EXIT_FAILURE);
		}

		if (count == -1)
			handle_error(errno);

		event = (struct inotify_event *) buf;
		printf("File %s was added with wd=%d\n", event->name, event->wd);

		if (event->mask & IN_CREATE) {
			if (check_pci_bus(event->name, short_pci_path) == 0) {

				/* Initialize device_name[] */
				memset(device_name, 0, DEVICE_NAME_LEN);

				/* Storing the device name for the IN_DELETE event */
				strncpy(device_name, event->name, event->len);

				printf("--> Found /sys/class/block/%s linking to %s\n", device_name,
											short_pci_path);

				strcpy(sys_devices_pci, sys_devices_pci_prefix);
				strcpy(sys_devices_pci + strlen(sys_devices_pci_prefix), "/driver/unbind");
				printf("echo \"%s\" > %s\n", pci_address, sys_devices_pci);

				/* Intentionally not exit(EXIT_FAILURE) */
				update_sys_file(sys_devices_pci);
			}
		} else if (event->mask & IN_DELETE) {
			printf("Delete event %s with length %d\n", event->name, event->len);

			/* The device PCI bus is now disabled, so it is removed from /dev */
			if (!strncmp(event->name, device_name, event->len)) {
				printf("---> Deleted /dev/%s\n", event->name, event->len);

				printf("---> Registering customer signal handlers\n");

				/* Registering the signal handler to re-enable the PCI device */
				if (signal(SIGINT, sig_handler) == SIG_ERR)
					handle_error(errno);
			}
		}
	} while(1);

	/* Will never reach this piece of code */
	inotify_rm_watch(ifd, wd);
	exit(EXIT_SUCCESS);
}
