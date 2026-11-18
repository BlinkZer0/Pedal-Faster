# Pedal Faster: User Manual

## Getting started

**In a DAW:** run `install.bat`, rescan plugins and insert **Pedal Faster** on a guitar track (mono or stereo).

**Standalone:** start *Pedal Faster* from the Start Menu, open **Options → Audio/MIDI Settings**, choose your audio interface, enable its input, and pick a small buffer (64–128 samples) for low latency. The standalone app defaults to **Input: Left (Mono)**, so a guitar on input 1 feeds both channels. Change this in the **Master** panel.

## The window

```
┌ PEDAL FASTER  < [preset] >  ★  Save  …   [search]  [category]   Master  Quality  Tuner  BYPASS  IN/OUT  CPU ┐
│ Preset browser │ Signal chain: pre-amp pedals → AMPLIFIER + CABINET → post-amp pedals │ Parameter editor │
└ Input · Gate · Master EQ · Macros (Drive Tone Space Output) · TAP/BPM ·  Stereo/Mono · Level · Master · LIMITER ┘
```

### Top bar
- **< / >** and the preset name step through and open presets (presets are grouped by bank). A `*` after the name means the preset has been edited.
- **★** toggles the current preset as a favourite.
- **Save** overwrites the current *user* preset. On a factory preset it asks for a name and creates a user preset, because factory presets are read-only. The **…** menu has Save As, Duplicate, Rename, Delete, Import/Export of single presets and banks, Restore factory default, Reload saved version and Open preset folder.
- **Search** matches names, categories, descriptions and tags. **Category** filters by bank.
- **Master** shows the global settings in the right-hand panel. **Quality**: Eco (no oversampling, lowest CPU, 0 latency), Normal (2x) or High (4x).
- **Tuner** shows a chromatic tuner. Enable *Tuner Mute* in the host or Master panel to tune silently. **BYPASS** passes the dry signal through, latency-matched.

### Preset browser
Filter by All / Factory / User / Favourites, and sort by bank, name, category or gain. Each entry shows the name, category, description, tags and an estimated gain level from 0 to 10. Click (or press Enter) to load.

### Signal chain
- **Pedal cards:** choose the pedal type from the drop-down (grouped by category), switch it on or off with **ON**, and adjust the main knobs. **<** and **>** move the pedal within its row. **Pre/Post** moves it between before-amp and after-amp. **…** has Reset, Copy/Paste settings, Save/Load pedal (`.pfpedal`) and Remove.
- **Drag** a card by its top strip onto another card (to insert before it) or into the other row.
- **Amplifier** and **Cabinet** modules have their own colours. Click any module to edit all of its parameters in the right-hand panel.
- **Load IR…** on the cabinet loads a WAV, AIFF or FLAC impulse response (mono or stereo, truncated to 2 s). The IR is stored inside your DAW project, so projects still open correctly if the file is later moved.

### Parameter editor (right panel)
Shows every control of the selected pedal, amp, cabinet or the master section.

### Bottom bar
Input trim, noise gate on/off and threshold, master EQ (low/mid/high), the **four macros**, tap tempo with the current BPM, stereo/mono output, preset output level, master volume, and the limiter switch with its activity LED.

## Knobs
- Drag up/down (or left/right) to adjust. Hold **Shift** or **Ctrl** for fine adjustment.
- Use the **mouse wheel** to adjust.
- **Double-click** to reset to the default.
- **Click the value** to type a number.

## Macros
**Drive**, **Tone**, **Space** and **Output** sit at 50% (neutral) when a preset loads. Turning one moves several of the preset's settings together:

| Macro | Moves |
|---|---|
| Drive | Amp gain and the first drive pedal |
| Tone | Treble, presence and cabinet high cut |
| Space | The mix of every delay and reverb |
| Output | Preset output level |

## Tempo
Delays, tremolo and auto-wah can sync to tempo (their **Sync** setting). In a DAW they follow the host tempo. In the standalone app, or with *Follow host tempo* off, use **TAP** (or press **T**) or the Tempo control. Some electronic presets set their own tempo, for example Hardstyle at 150 and Drum & Bass at 174 BPM.

## MIDI
| Message | Action |
|---|---|
| CC 11 (expression pedal) | Wah position (every wah pedal in the rig) |
| CC 7 | Volume |
| CC 20 / 21 / 22 / 23 | Drive / Tone / Space / Output macros |
| CC 80 / CC 81 (≥ 64) | Next / previous preset |
| Program Change | Load preset number *n* (bank order) |

## Keyboard shortcuts
| Key | Action |
|---|---|
| ← / → (or Page Up/Down) | Previous / next preset |
| Ctrl+S | Save |
| Ctrl+F | Search |
| Esc | Clear the search |
| T | Tap tempo |
| U | Tuner |
| B | Global bypass |
| Ctrl+1 … Ctrl+8 | Edit slot 1 … 8 |

Tab moves between controls. All controls have accessible names for screen readers.

## Factory library (250 presets)
The 20 core banks of 10 presets each: Pristine Clean, Sparkle & Chime, Blues & Vintage, Classic Rock, Alternative & Indie, Punk & Garage, Hard Rock, Metal Rhythm, Metal Lead, Doom & Stoner, Funk & R&B, Jazz & Neo-Soul, Country & Rockabilly, Ambient & Shoegaze, Psychedelic & Experimental, Post-Rock & Cinematic, Pop & Contemporary, Bass & Extended Range, Acoustic & Direct, Utility & Studio.

Five electronic banks of 10 presets each:
- **Electronic Ambient:** drones, freeze-style pads, granular and shimmer textures
- **Hardstyle:** gated reverse-bass-style leads, screeches, hoovers, raw distortion, risers (150–155 BPM)
- **Drum & Bass:** reese basses, neuro grind, liquid rollers, jump-up wobbles, sub pressure (174 BPM)
- **Dubstep:** tempo-synced wobbles, growls, riddim chops, tearout, talking bass (140 BPM)
- **Synthwave:** chorused retro leads, VHS warble, outrun delays, retro pads (95–120 BPM)

All factory presets are calibrated to a similar loudness. Acoustic presets are DI and acoustic-electric processors. They don't model a miked acoustic guitar body.

## Where things are stored
| What | Where |
|---|---|
| User presets | `%APPDATA%\Pedal Faster\Presets\*.json` |
| Favourites | `%APPDATA%\Pedal Faster\favourites.json` |
| Window size | `%APPDATA%\Pedal Faster\settings.xml` |
| DAW sessions | Inside the project (parameters, routing, macros, embedded IR) |
