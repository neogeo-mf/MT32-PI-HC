<h1 align="center">
    <img width="75%" title="mt32-pi - Baremetal synthesizer system" src="images/mt32-pi-hc-logo-01.png">
</h1>

- A work-in-progress baremetal MIDI synthesizer for the Raspberry Pi 3 or above, based on [Munt], [FluidSynth] and [Circle].

- Turn your Raspberry Pi into a dedicated emulation of the [famous multi-timbre sound module][Roland MT-32] used by countless classic MS-DOS, PC-98 and Sharp X68000 games!

- Add your favorite [SoundFonts][SoundFont] to expand your synthesizer with [General MIDI], [Roland GS], or even [Yamaha XG] support for endless MIDI possibilities.

- Includes General MIDI and Roland GS support out of the box thanks to [GeneralUser GS] by S. Christian Collins.

- No operating system, no complex Linux audio configuration; just super-low latency audio.

- Easy to configure and ready to play from cold-boot in a matter of seconds.

---

## New features

### MIDI Channel Settings Menu
Press the encoder knob to access the MIDI channel settings menu where you can:
- **Channel**: Select MIDI channel (1-16)
- **Mute**: Toggle channel mute on/off
- **Volume**: Adjust channel volume (0-127)
- **Program**: Change program/patch number (0-127)
- **Route**: Route channel to different output channel
- **Next**: Navigate to Animation Settings menu
- **Exit**: Return to normal display

### Animation Settings Menu
From the MIDI Channel Settings menu, select "Next" to access animation settings:
- **Mode**: Choose visualization mode:
  - **BarGraph**: Classic MIDI activity bar graph (default)
  - **Animation**: Cute face animations that react to MIDI
  - **Asteroids**: MIDI-reactive asteroids game with AI ship - notes spawn asteroids
  - **MatrixRain**: Matrix-style falling characters that react to note velocity
  - **Oscilloscope**: Real-time waveform display with MIDI-triggered waves
- **Next**: Navigate to Preset Menu (save/load MIDI channel configurations)
- **Back**: Return to MIDI Channel Settings menu
- **Exit**: Return to normal display

All three new visualization modes (Asteroids, MatrixRain, Oscilloscope) are fully MIDI-reactive and respond dynamically to incoming MIDI notes.

### FX Settings Menu (SoundFont Mode Only)
Access advanced effects controls when using SoundFont mode. Navigate from the MIDI Channel Settings menu by selecting "Next" twice (through Animation Settings):

#### Per-Channel FX Controls:
- **R Button**: Opens Reverb Settings (global reverb parameters)
- **C Button**: Opens Chorus Settings (global chorus parameters)
- **Ch**: Select MIDI channel (1-16)
- **Rv**: Reverb send level for selected channel (0-127, CC 91)
- **Chr**: Chorus send level for selected channel (0-127, CC 93)
- **Pn**: Pan position for selected channel (0-127, 64=center, CC 10)
- **Ex**: Expression level for selected channel (0-127, CC 11)
- **< Back**: Return to MIDI Channel Settings menu
- **> Next**: Navigate to Animation Settings menu
- **X Exit**: Return to normal display

#### Reverb Settings (Press R):
Global reverb parameters that affect the entire reverb unit:
- **Rm**: Room Size (0-100) - Controls the perceived size of the reverb space
- **Dp**: Damping (0-100) - High frequency absorption, higher values = darker reverb
- **SF**: SoundFont Override (ON/OFF) - When ON, per-channel reverb send values completely replace the SoundFont's built-in reverb levels instead of modulating them. Use this if you need to fully silence reverb on specific channels.
- **Wd**: Width (0-100) - Stereo width of the reverb effect
- **Lv**: Level (0-100) - Overall reverb output level
- **< Back**: Return to FX Settings menu
- **X Exit**: Return to normal display

#### Chorus Settings (Press C):
Global chorus parameters that affect the entire chorus unit:
- **Dp**: Depth (0-210) - Intensity of the chorus modulation (maps to 0.0-21.0)
- **Sp**: Speed (1-50) - Modulation rate in Hz (maps to 0.1-5.0 Hz)
- **SF**: SoundFont Override (ON/OFF) - When ON, per-channel chorus send values completely replace the SoundFont's built-in chorus levels instead of modulating them. Use this if you need to fully silence chorus on specific channels.
- **Lv**: Level (0-100) - Overall chorus output level (maps to 0.0-10.0)
- **Vc**: Voices (0-99) - Number of chorus voices
- **< Back**: Return to FX Settings menu
- **X Exit**: Return to normal display

**Note**: All FX parameter changes take effect immediately in real-time without requiring a restart. These settings only apply when using SoundFont mode and have no effect on MT-32 mode.

**Understanding SF Override**: SoundFonts contain built-in reverb and chorus send levels for each instrument. By default, the per-channel Rv/Chr values in the FX Settings menu modulate (add to) these built-in levels - setting Rv to 0 doesn't silence reverb if the SoundFont has reverb baked in. Enabling SF Override changes this behavior so that your per-channel send values completely replace the SoundFont's values, giving you full control. This is useful when you want to completely remove reverb or chorus from specific channels.

### Persistent Settings
The device now remembers your preferences across reboots:
- **Last selected visualization mode** - Your chosen animation mode is saved automatically and restored on boot
- **Last used SoundFont** - The device remembers which SoundFont you were using and loads it automatically on next boot

These settings are stored in separate state files on the SD card and persist through power loss.

## Project status

<img title="mt32-pi running on the Raspberry Pi 3 A+" width="280rem" align="right" src="images/mt32-pi-wht-pet.png">
This repository is a clone of the original where I will be making my own changes and is not affiliated with the original project. This repository is for a specific hardware build of the device by HobbyChop and therefore this readme file will be stripped down to include only what is related to that specific device. 

The goal of this development is focused on features for musicians, and the focus will not be on mister integration.

- [I²S Hi-Fi DAC support][I²S Hi-Fi DACs].
  * This is the recommended audio output method for the best quality audio.

- MIDI input, output and thru via TRS A MIDI interfaces, or Serial.

- [Configuration file] for selecting hardware options and fine tuning.

- [LCD status screen support][LCD and OLED displays] (for MT-32 SysEx messages and status information).

- Simple [physical control surface][control surface] using buttons and rotary encoder.

- Network MIDI support via [RTP-MIDI] and [raw UDP socket].

- [Embedded FTP server][FTP server] for remote access to files.

## Upgrade (If upgrading your HobbyChop device)

1. Download the latest release .zip file from https://github.com/Chiptune-Anamnesis/MT32-PI-HC/releases

2. Extract the contents.

3. Copy kernel8.img to the root of your MT32-Pi-HC SD Card.

4. Done

## Installation (if setting up a device from scratch)

1. Download the latest release from: https://github.com/Chiptune-Anamnesis/MT32-PI-HC/releases

2. Extract contents to a blank [FAT32-formatted SD card][SD card preparation].

3. For MT-32 support, add your MT-32 or CM-32L ROM images to the `roms` directory - you have to provide these for copyright reasons.

4. Add your favorite SoundFonts to the `soundfonts` directory.

5. The `mt32-pi.cfg` file has already been configured for the hardware and the MIDI hat, but feel free to look through the options and make any tweaks.

6. Connect a [USB MIDI interface][USB MIDI interfaces] or [GPIO MIDI circuit][GPIO MIDI interface] to the Pi, and connect some speakers to the headphone jack.

7. Connect your PC's MIDI OUT to the Pi's MIDI IN and (optionally) vice versa.

Device available here: https://hobbychop.com
