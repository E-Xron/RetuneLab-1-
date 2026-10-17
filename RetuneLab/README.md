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
- Keyboard strip on the left: click or drag to switch individual notes on/off (per octave, so turning off C4 leaves C3 and C5 on) (these are the notes "Snap to scale"
  uses). Changing the Key or Scale dropdown resets the active notes to exactly that scale in every octave.
- Clear wipes the clip; Undo (button or hotkey) brings it back.
- Hotkeys button: assign your own shortcuts (including Clear).
- Wheel: scroll pitch. Shift+wheel: scroll time. Ctrl/Cmd+wheel: zoom time.

Effects (opens inside the plugin window, no pop-up)
- Effects button: "Effects overview", or pick one effect to go straight to its page. Ticked = on.
- Overview: the signal flow and a card per effect showing what it does and its current settings. Click a card
  (or a stage in the signal flow) to open it. Power buttons switch effects on and off.
- Effect page: big knobs, the effect's display/meters and a breakdown of what every control does.
  "< All effects" goes back to the overview, the arrows step through effects, Close (or Esc) returns to the notes.
- Moving a knob on a bypassed effect switches it on.
- Note Glide: portamento into a note from the one before it. Glide Time (0-500 ms) and Smoothness
  (straight line to S-curve). Applies to the selected notes, or all notes if none are selected.
- Drift & Vibrato: scale the slow pitch drift and the vibrato as sung (0% flat, 100% as sung, 200% exaggerated).
  Per note, same selection rule. The pitch curve inside each note shows the result.
- De-Esser: split-band sibilance reduction (Threshold, Frequency, Max Reduction).
- Compressor: soft-knee RMS compressor with program-dependent release and auto make-up
  (Threshold, Ratio, Attack, Release, Mix).
- Air: Mid Air / High Air exciter.
- Choir: turns the recorded clip into a choir. Basses an octave down, altos a third below in the current key
  (or on the melody with Alto harmony off), sopranos on the melody. Every singer is resynthesised with natural
  formants (bigger voices low, smaller voices high) and their own timing, tuning, vibrato and stereo position, then
  the ensemble is placed in a hall. Size (3-16 singers), Bass/Alto/Soprano Share, Bass/Alto/Soprano Volume, Mix, Hall.
  Re-renders when you change notes, Size, Shares, Alto harmony or the key.
- Doubler: double-tracked vocal (Width, Detune, Mix).
- Echo: tempo-synced delay, 1/16 to 1/2 note (Time, Feedback, Tone, Mix).
- Reverb: Size, Damping, Width, Mix.
All effect controls are host-automatable.

Build: see the GitHub Actions workflow, or
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
