# mode7-video: start here

A video codec (Windows C++ encoder) and 6502 player for BBC Micro MODE 7 (Teletext), e.g. Bad Apple on two discs. beebasm. Main work 2016; branches explore MODE 5T/MODE 4/MODE 8 and NuLA variants up to 2020.

| | |
|---|---|
| GitHub | https://github.com/kieranhj/mode7-video (public) |
| Local path | D:\beeb\mine\mode7-video (moved from OneDrive\BEEB\Repos\mode7-video on 2026-11-22) |
| Branch | master; 15 other branches (16-bit-pack, brute-force, colour-convert, encoding-mode5t(-small), full-mode4, irqv-decode, linear-mode8, mode8-nula, puppeh-colour-codec, ...), wip/2026-11-tidy |
| Last commit | 2020-09-14: Update git ignore |

## What's in here
- `mode7video/`, `mode7video.sln`: the encoder (Visual Studio C++), using `jpeg-6b/` and `lpng1624/`; `mode7video.exe` is the built tool.
- `6502/m7vplay.6502`: the player; assembles to `M7VPLAY` (+ `m7vplay.inf`, both tracked) in the repo root.
- Per-video folders (`badappl`, `bad2`, `bird`, `simon`, `star*`, `tot2`, `aha`, `fish`, ...): frames, encoded streams and disc images.
- Pipeline scripts: `rip_mode7_frames.bat` / `rip_gif_frames.bat` (ffmpeg/ImageMagick frames), `compress_mode7_stream.bat` (exomizer + split), `make_mode7_disks.bat` (bbcim), `run_mode7_disk.bat`.
- `mode7-video-notes.txt` (untracked): notes.

## Build and run
- `make`: runs `make.bat`, which assembles `6502\m7vplay.6502` with `..\..\Bin\beebasm.exe`, writing `M7VPLAY` in the root (no disc). `make help` lists targets.
- Full pipeline per video: rip frames, run `mode7video.exe`, `compress_mode7_stream <name>`, `make_mode7_disks <name>`; tools expected in `bin\`.
- `run_mode7_disk.bat <name>` opens `<name>\disks\<name>.dsd` in `..\..\bin\b-em\B-em.exe`.

## Local changes
- Uncommitted: `.gitignore`, `rip_mode7_frames.bat`, `run_mode7_disk.bat` modified; untracked `bad2/`, `bird/`, `simon/`, `rip_gif_frames.bat`, `mode7-video-notes.txt`, `out.txt`, `mode7video.ilk`, and two 16 MB source videos (`a-ha - Take On Me ... .mp4`, `touhou bad apple smile.mp4`).
- Snapshotted on `wip/2026-11-tidy` (pushed 2026-11-21; includes `simon/` discs and `out.txt`).

## Status and next steps
- Last real work: `encoding-mode5t-small` (2019-10-28) and `full-mode4` (2020-09-14, same tip as master). No plan beyond `mode7-video-notes.txt`.
- Build junk at top level (`.vs`, `Debug`, `ipch`, `*.pdb`, `*.ilk`, `mode7video.VC.db`) is mostly ignored; the mp4s are not.

## See also
- `README.md` (one line), `LICENSE`; D:\beeb\mine\mode7-sprites, mode7-75, mode7-smooth.
