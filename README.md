# Papaya

A music player for the 3DS (mainly the New 3DS since it's completely untested on the o3ds) that aims to be really good. Or something.

This is a fork of ctrmus by deltabeard. here is the [original repo](https://github.com/deltabeard/ctrmus).

<img width="816" height="967" alt="image" src="https://github.com/user-attachments/assets/b5328434-6d5b-4504-9cf8-68f812fdadfa" />

>[!WARNING]
> **this is very vibecoded** but its at least somewhat tested so it shouldnt be that bad (?) if you have a problem with it being vibecoded then just dont use it ig ?

## Features

there are a lot of features.

most notably: (as of 1.0)

- you can play mp3, flac, opus, vorbis, wav, m4a, aac, and sid (???) not sure what a sid is but whatever
- "advanced file browser" (???) it works

note that the top directory is what you're currently in and the bottom directory is what the current playlist is playing from. there is no "real" playlist support in this (intentionally) so it just plays from whatever dir that is
<img width="655" height="492" alt="image" src="https://github.com/user-attachments/assets/cdc3c4e7-ec92-40f5-83f6-c9aa48ea63a5" />

- volume, brightness, and battery details in the main ui, along with the time and date
- details of whatever you're playing (bitrate, format, etc)
- metadata support
- repeat and shuffle
- queuing!
- seeking!
- file search so you can find songs way quicker if you have a big library

<img width="639" height="480" alt="image" src="https://github.com/user-attachments/assets/74bd40f2-f19c-4f4a-8ed9-23797e583f89" />

- queue editor that lets you move, remove, play tracks. kinda like spotify. so its advanced

<img width="642" height="481" alt="image" src="https://github.com/user-attachments/assets/b7d08d22-1cd0-45d6-b351-0e930b6ed0ad" />

- themes!
- an in-app theme editor that kinda sucks but whatever it works so you can make custom themes

<img width="642" height="484" alt="image" src="https://github.com/user-attachments/assets/f17e82b3-3569-4ec9-a931-df9f42b40384" />

- a "spinning disc" mode. it does exactly what it says.

<img width="315" height="342" alt="image" src="https://github.com/user-attachments/assets/bf4ba75b-dd07-4663-b1a3-44bbc9515767" />

- an entire effects menu with...
  - playback speed (it's not decoupled from pitch so that changes too but its fine)
  - distortion
  - reverb (you can change the mix and how long it lasts)
 
<img width="646" height="484" alt="image" src="https://github.com/user-attachments/assets/941f5e8d-13ae-450f-bfd7-d64a00b7aa64" />

you may be asking, "won't this hurt performance?" yes. yes it will.

the solution is very simple, just don't enable that many effects (or any at all). you could also just turn on fast mode if you're on a new 3ds but it fucks battery life so you choose.

## Installation

there should be a release, use the .cia instead of the .3dsx if you want it on your home menu instead of in the homebrew launcher

i wanna put it on universal updater, might figure that out later.

## Controls

**select** = open the menu. or settings. whatever you want to call it. but very very very useful

### music player
- x (or l+r or l+up because ???): pause/resume
- r: next song
- l: previous song
- zl/zr: seek back/forwards. you can change the amount in the settings
- start: toggle shuffle
- r+up / r+down: brightness

### file browser
- a: play
- b: go up
- y: add to queue
- up/down: move. obviously
- left/right: page up/down, very useful in big libraries
- you can press the buttons with the touchscreen and also touch the scrollbar

other controls can probably be found in the app and i cant be bothered to write them down
