# Twrp Recovery Tree for Cubot P50 (Marlon)
![Cubot P50 (Marlon)](https://fdn2.gsmarena.com/vv/pics/cubot/cubot-p50-1.jpg)

# Android 12+ Branch (WIP)

|Basic               |Spec Sheet|
|--                  |--                                                            |
|CPU                 |Octa-core (4x2.0 GHz Cortex-A53 & 4x1.5 GHz Cortex-A53)      |
|Chipset             |Mediatek Helio P22 MT6762/MT6765 (12 nm)                                     |
|GPU                 |PowerVR GE8320                                             |
|Memory              |6GB RAM                                                     |
|Android Version     |11 (AOSP)                                               |
|Storage             |128GB                                                      |

Blocking checks
- [X] Correct screen/recovery size
- [X] Working Touch, screen
- [X] Backup to internal/microSD
- [X] Restore from internal/microSD
- [X] reboot to system
- [X] ADB
Medium checks
- [X] update.zip sideload
- [X] UI colors (red/blue inversions)
- [X] Screen goes off and on
- [X] F2FS/EXT4 Support, exFAT/NTFS where supported
- [X] all important partitions listed in mount/backup lists
- [X] backup/restore to/from external (USB-OTG) storage (mouse and keyboard works)
- [X] backup/restore to/from adb (https://gerrit.omnirom.org/#/c/15943/)
- [ ] decrypt /data
- [X] Correct date

Minor checks
- [X] MTP export
- [X] reboot to bootloader
- [X] reboot to recovery
- [X] poweroff
- [X] battery level
- [X] temperature
- [X] encrypted backups
- [X] input devices via USB (USB-OTG) - keyboard, mouse and disks
- [X] USB mass storage export
- [X] set brightness
- [X] vibrate
- [X] screenshot
- [X] partition SD card
