**RETUNELAB**
Free vocal tuner/processor for every major DAW

Available as VST3 (Windows + Mac), AU (Mac) and a standalone app

The main goal when creating this was to emulate Melodyne and offer a functional and well built vocal tuner for free with no
licensing BS. I'm very proud of the final result and I hope you utilize all of the plug-ins capabilities.

**FEATURES**

- Live vocal processor
- Turns vocal tracks into choppable midi notes
- Split Notes
- Change Pitch
- Set Key
- Black Out Notes
- Snap to key
- Snap to Semitone
- Bindable Hot Keys
- Vocal Air Processor
- Mergeable Note Processor
- Enhanced Workflow
- Improved Pitch Tracker
- More Key Binds
- Merge To Next Note Modified
- Other Bugs Fixed
- Note Glide (portamento with adjustable time and smoothness)
- Drift & Vibrato editing per note
- De-Esser and natural Compressor
- Effects menu with toggleable Choir, Doubler, Echo and Reverb (Air moved in here too)
- Effects view inside the plugin window: an overview of every effect and its settings, plus a page per effect with machined knobs, meters and a live display (Back / Close / Esc to leave)
- Modernized main UI (toolbar, piano keyboard strip, notes)
- Smoother joins between notes and less crackle on drifting notes
- Keyboard note toggles are per octave

**FORMATS / DAWS**

- VST3 (Windows + Mac): Ableton Live, FL Studio, Cubase/Nuendo, Studio One, Reaper, Bitwig, Cakewalk, and other VST3 hosts
- AU (Mac): Logic Pro, GarageBand (also Ableton, Reaper, Studio One on Mac)
- Standalone app (Windows + Mac): runs without a DAW. Recording loops the clip back for editing
- Pro Tools (AAX) is not included: it needs Avid's AAX SDK and a paid PACE signing account
- Mono or stereo tracks, and mono-in / stereo-out

The GitHub Actions build makes RetuneLab-Windows (VST3 + .exe) and RetuneLab-Mac (VST3 + AU + .app), each with an INSTALL.txt.

**WORKFLOW**

Put RetuneLab on an audio track with a monophonic vocal / lead clip.
Click Record, press play in your DAW over the clip, click Record again to stop (max ~60 s).
Notes appear and the key is detected automatically. Edit. Edits play back in sync with the transport (inside the recorded range). Untouched notes play the original audio unchanged.
Editing

Drag a note: up/down = pitch (snaps to semitone, Shift = fine), left/right = timing. Alt locks to one axis.
Drag a note's left/right edge: stretch / shorten it.
Drag on empty space to draw a selection rectangle (Ctrl/Cmd or Shift adds to the selection).
Split tool (or right-click > Split here); right-click > Merge joins with the next note.
Snap all: Scale / Snap all: Semitone buttons move EVERY note. Right-click / hotkeys snap just the selection (or everything if nothing is selected).
Keyboard strip on the left: click or drag to switch individual notes on/off (per octave, so turning off C4 leaves C3 and C5 on) (these are the notes "Snap to scale" uses). Changing the Key or Scale dropdown resets the active notes to exactly that scale in every octave.
Clear wipes the clip; Undo (button or hotkey) brings it back.
Hotkeys button: assign your own shortcuts (including Clear). Effects button: open the effects overview or go straight to one effect.
Wheel: scroll pitch. Shift+wheel: scroll time. Ctrl/Cmd+wheel: zoom time.
