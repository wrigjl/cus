# cus
cu(1) for sockets

This program is a simple terminal interface for UNIX-domain sockets.
I use this regularly with VMware/Virtualbox VMs with serial ports
(primarily for serial console/kernel debugging).

To use: ```cc -O cus.c -o cus && ./cus [-l logfile] path-to-socket```
