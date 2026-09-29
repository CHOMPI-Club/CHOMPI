# Bootloader Changelog

## V6.2.0
- Modify battery communications to more robust method.

## V6.1.0
- Daisy bootloader modified for chompi based on bootloader V0.6.0
- Talk to BMC in the bootloader in order to go to shipping mode in case of low battery
- Reduce timeout waiting for button press. Speeds up overall boot time
- Add led animation during bin flash from SD card
- Moved things happening in audio cb to low priority cb