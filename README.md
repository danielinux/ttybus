TTYBUS project. Copyright (c) 2010 Daniele Lacamera <root@danielinux.net>.


# OVERVIEW
The **TTYBUS** project is a toolkit that provides tty device virtualization, multiplexing, spoofing and sharing among unix processes.
It allows users to connect their real and virtual terminals (including real serial devices) to a shared, *non-reliable* bus, and
then to virtually any input/output stream.


# LICENSE
**TTYBUS** comes without any warranty and it is released under the terms of the GNU/GPL v.2


# IDEA
**TTYBUS** steals the architecture concept from VDE (Virtual Distributed Ethernet) design.
The working scheme is quite simple: Users can connect their ttys together in a virtual bus, then all data sent and received
from any of the devices connected to the virtual bus is echoed to all other devices.

This includes real and virtual tty devices, with the possibility to create fake devices using posix pseudo-terminals.


# COMPONENTS

## `tty_bus`
Creates a new tty_bus running on the system, at a given bus path specified with the `-s` option. The command creates the bus
and exposes a unix socket at the given path. Once the path has been created, any device can be plugged in using the other
toolkit's commands.

## `tty_plug`
Connects `STDIN/STDOUT` of the current terminal to the tty_bus specified with the `-s` option.
Eventually the `-i` option can be specified to add an init string to be passed to process stdout before it's connected
to the tty_bus.

## `tty_fake`
Creates a new pseudo-terminal devices connected to the tty_bus specified with the `-s` option. If the given path for the fake
device already exists, `tty_fake` can be forced to replace it with the `-o` option.

## `tty_attach`
Open a real (existing) tty and connects it to the tty_bus specified with the -s option.
Eventually the `-i` option can be specified to add an init string to be passed to the real tty device before it's connected
to the tty_bus.

## `dpipe`
Taken from the VDE project, allows two unix processes to communicate each-other by attaching each process' `STDOUT` stream to
the other one's `STDIN`.

Please refer to each command's help for usage notes, using the `-h` option .


# EXAMPLES

## Use case 1
Multiplexing serial input only or output only device attached to `/dev/ttyS0`, for use with multiple applications.

1. Create a new tty_bus called `/tmp/ttyS0mux`:

	`tty_bus -s /tmp/ttyS0mux`

1. Connect the real device to the bus using `tty_attach`:

	`tty_attach -s /tmp/ttyS0mux /dev/ttyS0`

1. Create two fake `/dev/ttyS0` devices, attached to the bus:

	`tty_fake -s /tmp/ttyS0mux /dev/ttyS0fake0`

	`tty_fake -s /tmp/ttyS0mux /dev/ttyS0fake1`

1. Start your application and force it to use the new serial device for input or output

	`/bin/foo /dev/ttyS0fake0 &`

	`/bin/bar /dev/ttyS0fake1 &`

Both application will read (or write) from the same serial device.

**CAUTION:** all data written on each of the two fake devices will be echoed on the other one too.


## Use case 2
Create fake NMEA devices from your running gpsd daemon.

1. Create the bus:

	`tty_bus -s /tmp/gpsmux`

1. Use `dpipe`, `tty_plug` and `netcat` to connect tcp socket to gpsd daemon. Send 'r' as init string to get NMEA flow from the socket:

	`dpipe tty_plug -s /tmp/gpsmux -i "r"  = nc -c ip.address.of.gpsd 2947`

1. Create a fake gps device

	`tty_fake -s /tmp/gpsmux /dev/gps0`

1. Connect your legacy serial, non gpsd-compatible, nmea application to the fake device `/dev/gps0`

	`/usr/local/games/stupid_nmea_app /dev/gps0`


## Use case 3:
Remote serial device via SSH tunnel. Host *mars* has a serial device connected on `/dev/ttyUSB0`, which must be  accessed and used from host *venus*.

1. Create two `tty_bus`, one per machine.

	`mars:$ tty_bus -s /tmp/exported_ttybus`

	`venus:$ tty_bus -s /tmp/remote_ttybus`

1. On *mars*, attach the device to the local bus:

	`mars:$ tty_attach -s /tmp/exported_ttybus /dev/ttyUSB0`

1. On *venus*, prepare the fake tty device, on the same path, temporarly overriding the local `/dev/ttyUSB0` device:

	`venus:$ tty_fake -s /tmp/remote_ttybus -o /dev/ttyUSB0`

1. Connect the two buses on the two hosts, using remote ssh command and `tty_plug`:

	`venus:$ dpipe tty_plug -s /tmp/remote_ttybus = ssh mars tty_plug -s /tmp/exported_ttybus`

From now on, the `/dev/ttyUSB0` on *venus* is actually the device connected on *mars*. If it existed before being overridden,
the original `/dev/ttyUSB0` is restored once the `tty_fake` process on venus gets killed.