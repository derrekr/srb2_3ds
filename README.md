
# SRB2 3DS
A port of Sonic Robo Blast 2 to the Nintendo New3DS/2DS consoles.

## Installing
* Download srb2_3ds from the latest release on Github and extract the files to /3ds/srb2_3ds on your SD card.
* Download SRB2's game files [here](https://downloads.devkitpro.org/srb2_data.zip) or from [srb2.org](http://www.srb2.org/) and extract them to /3ds/srb2_3ds as well.
	You may also copy the files to the root directory, but make sure config.cfg is located in the same directory as the game files.
* Make sure you have dumped your DSP firmware and dspfirm.cdc is present.

## Building
* Building requires 3ds-sdl and 3ds-sdl_mixer to be installed (use devkitPro's pacman).

## Known Issues/Limitations
* Does *NOT* run on o3DS devices. The Nintendo New3DS' HW allows higher CPU clock frequencies and adds another layer of cache.
	On o3DS it's extremely laggy and would probably require modifications to the level design in order to run it at a decent FPS.
* No stereoscopic 3D rendering
* No support for MIDI music
* No netplay
* Lower screen isn't really used
* CEZ2 is extremely laggy

## Thanks
Thanks to fincs, WinterMute, Monster Iestyn, Sryder, AlamTaz, Steel Titanium for help with development.
Thanks to profi200, fincs and WinterMute for testing.
