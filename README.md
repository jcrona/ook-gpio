# ook-gpio - A simple GPIO-based OOK modulation driver


## Synopsis

This driver allows to send OOK (ON/OFF Keying) modulated signals to a GPIO. It's main purpose is to generate 433MHz OOK modulated frames by connecting a 1$ 433MHz transmitter to the GPIO, but it should work with any kind of transmitter.


## Build instruction

In order to build this driver as an OpenWrt package and/or add it to an OpenWrt firmware build:
- checkout an OpenWrt build tree (only tested on Barrier Breaker 14.07 so far)
- move this folder to __openwrt/package/kernel/ook-gpio__

Now, if you run `$ make menuconfig` from the __openwrt__ root folder, you should see an __ook-gpio__ entry in the __Kernel modules/Other modules__ sub-menu.
Just select it as built-in or package, and launch a build.


## Usage

Once registered, this driver adds two sysfs entries:
- __/sys/devices/platform/ook-gpio.0/timings__
- __/sys/devices/platform/ook-gpio.0/frame__

The first one is used to configure the OOK timings, while the second one will be used to send the actual frame.
The __timings__ sysfs expected format is __Tbase or THstart, TLstart, THend, TLend, THbit0, TLbit0, THbit1, TLbit1, fmt, count__ with:
- Tbase : the duration of a state (Low for 0, High for 1) in Raw mode (fmt = 2)
- TH/TLstart : High and Low duration of the starting marker
- TH/TLend : High and Low duration of the ending marker
- TH/TLbit0 : High and Low duration of the logical bit 0
- TH/TLbit1 : High and Low duration of the logical bit 1
- fmt : the bit format (order of the High/Low transition) which is 0 for High/Low, 1 for Low/High, and 2 for Raw (no transition)
- count : the number of times the frame must be sent

The __frame__ expected format is the number of bits in the frame followed by a ",", then the bytes of the frame, also separated by ",".


## License

This driver is distributed under the GPLv2 license. See the LICENSE file for more information.


## Miscellaneous

This driver has been successfully tested on a TP-Link TL-WR703N router running OpenWrt.
Check out my [Home-RF](https://github.com/jcrona/home-rf) and [rf-ctrl](https://github.com/jcrona/rf-ctrl) projects for a simple Web-UI and a command line tool that use this driver.

Fell free to visit my [blog](http://blog.rona.fr), and/or send me a mail !

__  
Copyright (C) 2016 Jean-Christophe Rona
