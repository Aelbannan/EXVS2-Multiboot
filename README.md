# EXVS2 Multitool

Does two things:
1. Allows you to run multiple copies of the client on one PC (Can play w/ controllers using nucleus coop)
2. Translates text on the fly (can be used to translate to english)

```
; EXVS2 multiboot (bcrypt.dll) configuration
; Copy to net.ini in the game directory and edit as needed.

[General]
; UDP port the game binds for inter-instance broadcast (required for socket killer).
SharedPort=50000
; This machine's instance index (0, 1, 2, ...).
InstanceNumber=0
; Card server UDP port (excluded from socket-killer trigger).
CardServerPort=50001
; Close the broadcast socket after the first sendto to another instance to allow multiple clients (0=off, 1=on). Default: off.
SocketKillerEnable=0

[LocalizedText]
; Load English text overrides from JSON (0=off, 1=on). Default: on.
OverrideEnable=1
; Override file in the game directory. Message keys and Japanese literal names share one file.
OverrideFile=localized_text_overrides_en.json

```
