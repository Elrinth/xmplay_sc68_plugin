# xmp-sc68 1.0.2

Native **32-bit** XMPlay input plugin for Atari ST and Amiga music.

Engine: official **sc68** (libsc68 + file68 + unice68) by Benjamin Gérard.
This is **not** a Winamp `in_sc68` wrapper.

## Install

Copy `xmp-sc68.dll` next to `xmplay.exe` (or into XMPlay's plugin folder)
and restart XMPlay. The DLL carries a Windows VERSIONINFO resource (FILEVERSION 1.0.2.0) so XMPlay can include it in update notifications. Classic XMPlay is **32-bit only** — this DLL is PE32
i386. A 64-bit build will not load.

XMPlay's *Supported file types* list shows **sc68**, **sndh**, and **snd**
(one description: `SC68 / SNDH`).

## Formats

| Extension | Notes |
|-----------|--------|
| `.sc68`   | sc68 disk (Atari ST / Amiga, including TFMX and JamCracker) |
| `.sndh`   | Atari ST SNDH (raw or Pack-Ice `ICE!`) |
| `.snd`    | Same as SNDH (common SNDH archive name) |

CheckFile only reads a short prefix (`SC68`, `ICE!`, or `SNDH`) so adding
songs to the playlist stays fast.

External-replay sc68 files (JamCrackerPro and other Amiga players baked
into the DLL via `replay.inc.h`) play without a `Replay/` folder on disk.

## Length

Track time comes from the SNDH `TIME` tag or sc68's timedb when present
(e.g. cream-1996.sndh). If neither is known, GetFileInfo / Open skip-render
until a loop, trailing silence, or a 10-minute cap — they do **not** fake
3:00. CheckFile never does this (still a 2 KB magic peek).

## Seeking

The playhead is seekable on **every** supported format, including SNDH
and ICE-packed files (the old Winamp `in_sc68` could not seek).

Seek is implemented with sc68's position API plus restart + skip-render,
with 68k snapshots every ~3 seconds so dragging the playhead stays
responsive. Seeking backward, forward, and to 0 all work.

## Multi-track files

A file with several songs becomes NSF-style **tracks**. Use
**Shift+Left** / **Shift+Right** in XMPlay to change track, same as NSF.

## Volume / gain

Old sc68 / in_sc68 volumes are inconsistent. This plugin:

- Does **not** auto-normalize (preserves the tune's character)
- Has a **gain** control in dB (default 0), persisted by XMPlay
- Optional **"boost quiet tunes"** (+6 dB if the start is very quiet)

Open the plugin config from XMPlay's plugin options.

## Credits

- sc68, file68, unice68 — Benjamin Gérard (benjihan)
- SNDH archive — Atari ST YM2149 community
- XMPlay plugin SDK — un4seen / Ian Luck

License: GPLv3 (sc68 is GPLv3).
