# RetuneLab - clip-based note editor (VST3)

Workflow (like Melodyne's "Transfer"):
1. Put RetuneLab on an audio track with a monophonic vocal / lead clip.
2. Click Record, press play in Ableton over the clip, click Record again to stop (max ~60 s).
3. Notes appear. Edit them. Edits play back in sync with the transport (inside the recorded range).

Editing
- Drag a note: up/down = pitch (snaps to semitone, Shift = fine), left/right = timing. Alt locks to one axis.
- Drag a note's left/right edge: stretch / shorten it (and move its start).
- Split tool (or right-click > Split here): cut a note at the cursor. Right-click > Merge joins with the next note.
- Click selects; Ctrl/Cmd-click adds to the selection; Ctrl/Cmd+A selects all; Delete mutes; Ctrl/Cmd+Z undoes.
- Snap to scale: moves selected notes (or all) to the nearest note of the chosen key/scale.
- Drag on empty space to draw a selection rectangle (hold Ctrl/Cmd or Shift to add to the selection).
- Hotkeys button: choose your own keyboard shortcuts. Air button: Mid Air / High Air controls for vocal sheen.
- Wheel: scroll pitch. Shift+wheel: scroll time. Ctrl/Cmd+wheel: zoom time.

Build
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

## Easiest way to get the .vst3 (no compiler needed)
1. Create a free GitHub account and a new repository, then upload the contents of this folder (including the hidden .github folder).
2. Open the repo's Actions tab, pick "Build VST3", and wait for the run to finish (~10 min).
3. Download RetuneLab-Windows or RetuneLab-Mac from the run's Artifacts section.

## Install
- Windows: copy the RetuneLab.vst3 FOLDER to C:\Program Files\Common Files\VST3
- Mac: extract the tar.gz, copy RetuneLab.vst3 to ~/Library/Audio/Plug-Ins/VST3, then run in Terminal:
      xattr -cr ~/Library/Audio/Plug-Ins/VST3/RetuneLab.vst3
Then in Ableton: Preferences > Plug-Ins > enable VST3 system folders > Rescan.
