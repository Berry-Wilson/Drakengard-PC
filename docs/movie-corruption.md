# Movie corruption on PCSX2's GPU renderer

Status: worked around by drawing movies with PCSX2's software renderer (see "Current workaround").
The underlying problem in PCSX2's render-target cache is not fixed.

## Symptom

During movies (FMVs), every second picture shows the same stale, garbled image. Bands of the
picture are shifted sideways in a staircase, with black gaps. It happens in 4:3 and 16:9, and only on
the GPU renderers. `PS2X_PCSX2_RENDERER=software` shows every picture correctly. F12 screenshots can
look fine because they catch whichever picture is on screen.

## Reproducing

Replay `input_recordings/input_20261110_203136.txt` (it plays the Chapter 1 Verse 2 opening movie
about 80 to 105 s in) and save six consecutive frames:

```
PS2X_INPUT_REPLAY=input_recordings\input_20261110_203136.txt
PS2X_DUMP_FRAMES_DIR=<folder>
PS2X_DUMP_FRAMES_EVERY=1
PS2X_DUMP_FRAMES_AFTER=92
PS2X_DUMP_FRAMES_COUNT=6
```

Before the workaround: frames 5498 and 5499 were clean, 5500 and 5501 broken, 5502 and 5503 clean,
and the broken picture was identical every time. Set `PS2X_PCSX2_MOVIE_RENDERER=hardware` to turn the
workaround off and see it again.

## What the game sends

Captured with `PS2X_GIF_CAPTURE` (all PATH3):

- Each movie picture is one ~957 KB packet of 880 IMAGE transfers, each 16x16 CT32. BITBLTBUF has
  DBP 0 and DBW 12 (768 pixels wide).
- The game double-buffers in that one 768-wide area. Picture A goes to rows 40-391 and picture B to
  rows 520-871.
- A 256-byte packet then draws the letterbox bars with FRAME at FBP 0 (A) or FBP 0xB4 (B), FBW 12.
- DISPFB alternates between FBP 0 and FBP 0xB4, with FBW 12, DBX 40 and DBY 16. PMODE is 0x66.
- FBP 0xB4 is block 0x1680, which is row 480 at width 768. So picture B's uploads, made relative to
  block 0, land inside the 0x1680 buffer exactly as on real hardware.

## Root cause (in PCSX2's texture cache)

Found by temporarily adding `fprintf` logging to `GSTextureCache::LookupDisplayTarget` and
`GSTextureCache::InvalidateVideoMem` in the PCSX2 clone, listing every render target (TBP0, TBW,
size, end block, dirty count, valid rect) at each display lookup and at the first upload of a picture.
Patch idea, in each function after `bp`/`bw` are known:

```cpp
for (Target* t : m_dst[RenderTarget])
    std::fprintf(stderr, " [%x w%u %dx%d end %x dirty %zu]", t->m_TEX0.TBP0, t->m_TEX0.TBW,
                 t->m_unscaled_size.x, t->m_unscaled_size.y, t->m_end_block, t->m_dirty.size());
```

What it showed during the movie:

- The display alternates `LookupDisplayTarget` between bp 0 and bp 0x1680, both with TBW 12.
- The target at 0x1680 is TBW 12, 768x480, and stays correct. That is picture **B**, which plays fine.
- The target at 0 is **TBW 10** (640 wide), 768x480. It is left over from the 640-wide screen drawn
  just before the movie. Picture **A**'s 768-wide uploads are read back through that 640-wide
  layout, which produces the staircase.
- `InvalidateVideoMem` only takes the "dirty the target" path when the upload's width matches the
  target's (`bw == t->m_TEX0.TBW`). Otherwise it uses `DirtyRectByPage`.
- `LookupDisplayTarget` does have a rule for a target of the wrong width ("Prince of Persia": if the
  target's only dirty rect has the wanted width, adopt that width). It only applies while the target
  is still dirty, and at display time it never is: dirty count 0.

The [PCSX2 forums](https://forums.pcsx2.net/Thread-Drakengard-Video-Problem) report distorted movies
in Drakengard on the hardware renderer too. PCSX2's own remedy for this class of problem is the
`SoftwareRendererFMV` game fix, which `Counters.cpp` (`DoFMVSwitch`) triggers from IPU activity. Our
port decodes movies itself and never touches the IPU, so that detection cannot fire.

## Current workaround

While a movie plays, the runtime asks `ps2x-gs.dll` to draw with PCSX2's software renderer
(`ps2xgs_set_software_rendering`, which calls `GSSetSoftwareRendering` on the GS thread at the next
vsync), and switches back when the movie ends.

- **Detecting a movie:** like PCSX2's own detection, which watches the IPU going idle, it uses
  decoder activity. Each `sceMpegGetPicture` call notes a picture (`ps2xPcsx2NoteMoviePicture`).
  The backend switches to software at the next vsync, and back once 15 vsyncs (a quarter second)
  pass without one. The game never calls `sceMpegDelete`, so creation and deletion can't be used.
  A skipped movie also ends this way.
- **Cost:** movies are 640x480 video, so nothing is lost visually. PCSX2 copies its buffers when it
  changes renderer, which can cause a brief hitch at each switch.
- **Determinism:** after a movie the GPU renderer is a fresh instance, so replays differ from runs
  without the switch by a handful of pixels. They still follow the same path.
- **Turning it off:** set `PS2X_PCSX2_MOVIE_RENDERER=hardware`.

## Ideas for a proper fix

1. In `InvalidateVideoMem`, when an upload at a target's own base pointer has a different width and
   covers the target's full width, adopt the upload's width (as the Prince of Persia rule does at
   display time) instead of dirtying by page.
2. Let `LookupDisplayTarget` replace or reinterpret a target whose TBW differs from DISPFB's when
   uploads with DISPFB's width have landed in it since it was last drawn, not only while it is dirty.
3. Check how a draw with FRAME FBW 12 at bp 0 (the letterbox bars) handles the TBW 10 target in
   `LookupTarget` (around the `TEX0.TBW != t->m_TEX0.TBW` checks near lines 2504-2800 at PCSX2 commit
   1ccd6a8dc). That draw might be where the width should change.

Any change there touches code every PCSX2 game depends on. Check this game's other scenes
(`input_recordings/input_20261109_161215.txt` for the sky mission, the session above for menus and a
ground mission) for pixel changes before keeping it.
