# usb_notify
Detects a USB storage device when it's plugged into a PC

# Objective
Protects a PC from USB mass-storage devices by:
- detecting when a mass-storage device is plugged into a USB port
- disabling the corresponding PCI port

Then, an in-memory virtual machine can be started, with PCI passthrough (IOMMU & VFIO) to inspect the mass-storage device without affecting the host PC.

It will discard non block devices; i-e: USB network devices, keyboards or mice will not be detected.

# Actions

## Start the Executable

It will detect any mass-storage device inserted into the specified PCI port and disable the corresponding PCI port.

## Enable the Device

Sending a SIGINT to usb_notify will re-enable the PCI port, and allow the device to be read from the PC.

# Syntax

usb_notify PCI_PORT PCI_ADDRESS

e-g: usb_notify "pci0000:00" "0000:00:14.0"
