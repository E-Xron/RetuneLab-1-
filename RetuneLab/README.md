# RetuneLab - clip-based note editor (VST3)

Workflow (like Melodyne's "Transfer"):
1. Put RetuneLab on an audio track with a monophonic vocal / lead clip.
2. Click Record, press play in Ableton over the clip, click Record again to stop (max ~60 s).
3. Notes appear and the key is detected automatically. Edit. Edits play back in sync with the transport
   (inside the recorded range). Untouched notes play the original audio unchanged.

Editing
- Drag a note: up/down = pitch (snaps to semitone, Shift = fine), left/right = timing. Alt locks to one axis.
- Drag a note's left/right edge: stretch / shorten it.
- Drag on empty space to draw a selection rectangle (Ctrl/Cmd or Shift adds to the selection).
- Split tool (or right-click > Split here); right-click > Merge joins with the next note.
- Snap all: Scale / Snap all: Semitone buttons move EVERY note. Right-click / hotkeys snap just the selection
  (or everything if nothing is selected).
- Keyboard strip on the left: click or drag to switch pitch classes on/off (these are the notes "Snap to scale"
  uses). Changing the Key or Scale dropdown resets the active notes to exactly that scale.
- Clear wipes the clip; Undo (button or hotkey) brings it back.
- Hotkeys button: assign your own shortcuts (including Clear). Air button: Mid Air / High Air.
- Wheel: scroll pitch. Shift+wheel: scroll time. Ctrl/Cmd+wheel: zoom time.

Build: see the GitHub Actions workflow, or
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
