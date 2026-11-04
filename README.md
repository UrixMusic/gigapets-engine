Gigapets-Engine (name WIP) is an open-source engine reimplementation of the 2006 Hasbro Plug 'n' Play game "GigaPets Explorer", aiming to add moddability to the game.


## Exporting the ROM's music

- `gigapets rom.u7 --export-music <folder>` writes the ROM's songs as MIDI files (plus `songs.txt`), which you can edit and use as a mod's `music` record.
- `gigapets rom.u7 --export-instruments <folder>` writes a WAV of each of the ROM's 14 instruments and 12 drums, with `instruments.txt` listing their numbers and the General MIDI names they are closest to.
- In your DAW, name a channel after the instrument ("Trumpet") or write its number as `[iN]` ("Lead [i3]") to choose its ROM instrument.
