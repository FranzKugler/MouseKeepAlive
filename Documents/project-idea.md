# Mouse Keep Alive Project

The Mouse Keep Alive Projet is a solution to keep a windows installation "alive" while away or busy with other tasks by "wiggeling" an artificial bluetooth mouse once in a while (e.g. once every 5min - must be configurable) to mimic an active user in front of the computer

## Main Function

I want to emulate a secondary bluetooth mouse that simulates a wiggle at a configurable time to simulate an active user to prevent windows going into a lock mode. An ESP32-S3 (to be more precise an XIAO ESP32-S3 Plus with 16MByte of FLASH and 8MByte of PSRAM) should be used for that purpose. If possible the device should emulate more than one mouse (I have at least 2 windows machines with that issue). The "wiggeling" should be configurable in terms of frequency (e.g. once every 5min etc. as well as "amplitude" so that windows recognize a human interaction)

Because we use a USB for the project, the ESP32 should use OTA flashing and logging. 

## Implementation Phases

1. Implement all ESP32 functions. In this phase, we want to debug the infrastructure, such as OTA flashing and debugging.
2. Implement the HID Mouse. As a test, the workbench should emulate the Windows Mouse "receiver", delete it, and write it again.

## Tools

Use ESP-IDF and Apple native tools for the project.
Use the ESP32-workbench (https://github.com/SensorsIot/Universal-ESP32-Workbench)  with its skills for flashing, testing, and Windows Mouse simulation (receiver9.

Create a new GitHub Repository on https://github.com/FranzKugler/MouseKeepAlive on github