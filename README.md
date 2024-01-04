
# SRB2 3DS
A port of Sonic Robo Blast 2 to the Nintendo New3DS/2DS consoles.

## Installing
* Download srb2_3ds from the latest release on Github and extract it.
* If you would like to use the .3dsx file, copy it to /3ds/srb2_3ds on your SD card.
	If you would like to use the .CIA file, you may copy it at any location on your SD card.
* Download SRB2's assets: [file1](https://github.com/mazmazz/SRB2/releases/download/SRB2_assets/SRB2-v2122-assets.7z) and [file2](https://github.com/mazmazz/SRB2/releases/download/SRB2_assets/SRB2-v2122-optional-assets.7z) and extract them to /3ds/srb2_3ds on your SD card.
	You may also copy the files to the root directory. (Note: make sure any existing config.cfg file is located in the same directory as the game files.)
* Make sure you have dumped your DSP firmware and dspfirm.cdc is present. If it isn't: install and run the "dspDump" homebrew first.

## Building
* Building requires 3ds-sdl and 3ds-sdl_mixer to be installed (use devkitPro's pacman).
* Build and install [this fork](https://github.com/derrekr/citro3d) of the citro3d 3DS graphics library.
* makerom is required for building cia.

## Known Issues/Limitations
* Does *NOT* run on o3DS devices. The Nintendo New3DS' HW allows higher CPU clock frequencies and adds another layer of cache.
	On o3DS it's extremely laggy and would probably require modifications to the level design in order to run it at a decent FPS.
* No support for MIDI music
* No netplay
* Lower screen isn't really used
* CEZ2 is extremely laggy

## Thanks
Thanks to fincs, WinterMute, Monster Iestyn, Sryder, AlamTaz, Steel Titanium for help with development.
Thanks to profi200, fincs and WinterMute for testing.
Credit for 3DS homebrew logo goes to [PabloMK7](http://gbatemp.net/members/pablomk7.345712/).
