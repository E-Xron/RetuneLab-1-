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
- Hotkeys button: assign your own shortcuts (including Clear).
- Wheel: scroll pitch. Shift+wheel: scroll time. Ctrl/Cmd+wheel: zoom time.

Effects menu (every entry has its own On switch; ticked = on)
- Note Glide: portamento into a note from the one before it. Glide Time (0-500 ms) and Smoothness
  (straight line to S-curve). Applies to the selected notes, or all notes if none are selected.
- Drift & Vibrato: scale the slow pitch drift and the vibrato as sung (0% flat, 100% as sung, 200% exaggerated).
  Per note, same selection rule. The pitch curve inside each note shows the result.
- De-Esser: split-band sibilance reduction (Threshold, Frequency, Max Reduction).
- Compressor: soft-knee RMS compressor with program-dependent release and auto make-up
  (Threshold, Ratio, Attack, Release, Mix).
- Air: Mid Air / High Air exciter.
- Choir: turns the vocal into a choir. Bass an octave down, alto at pitch, soprano an octave up, each singer with
  its own timing, detune and stereo position. Size (3-24 singers), Bass/Alto/Soprano Share (how the singers are
  split between sections), Bass/Alto/Soprano Volume, Mix.
- Doubler: double-tracked vocal (Width, Detune, Mix).
- Echo: tempo-synced delay, 1/16 to 1/2 note (Time, Feedback, Tone, Mix).
- Reverb: Size, Damping, Width, Mix.
All effect controls are host-automatable.

Build: see the GitHub Actions workflow, or
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
