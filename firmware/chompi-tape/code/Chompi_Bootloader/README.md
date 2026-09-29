# CHOMPI Bootloader (v6.2)

The bootloader that ships on CHOMPI units and loads the application firmware from
QSPI flash. Derived from the open-source Daisy bootloader by Electrosmith (MIT).


This project is organized into dependencies (cube_dfu, DaisySP, and libDaisy), the bootloader application (bootloader), and various target applications. When building the bootloader, make sure libDaisy is build _without_ VOLATILE defined. When building target applications, make sure VOLATILE _is_ defined (i.e. build VOLATILE=1, or by using the VSCode task build_libdaisy_volatile).

To upload to the bootloader, use the program-volatile target. It should produce the following command:

~~~ bash
dfu-util -a 0 -s 0x90040000:leave -D path/to/program.bin -d ,0483:df11
~~~

