<div align="center">

# Ultimate Mortal Kombat 3 — PS Vita Port

**A native PlayStation Vita port of *Ultimate Mortal Kombat 3* for iOS, built from the decompilation of the iOS release (v1.2.59).**

Based on [MaryNCRT/Ultimate-Mortal-Kombat-3-iOS-Decompiled](https://github.com/MaryNCRT/Ultimate-Mortal-Kombat-3-iOS-Decompiled) · Rendered with [vitaGL](https://github.com/Rinnegatamante/vitaGL) by Rinnegatamante · [Vita build & settings](vita/README_VITA.md)

</div>

---

## PS Vita port

This repository is a fork of **[MaryNCRT's Ultimate Mortal Kombat 3 iOS decompilation and PC port](https://github.com/MaryNCRT/Ultimate-Mortal-Kombat-3-iOS-Decompiled)** (based on its v0.1.11 beta), with a PlayStation Vita platform layer added. The game code is the decompilation from that project; everything Vita-specific lives in [`vita/`](vita/) and in code marked `UMK3_VITA`.

> [!IMPORTANT]
> **No game data is distributed here.** You need your own legally obtained copy of *Ultimate Mortal Kombat 3* for iOS (**v1.2.59**, `.ipa`). The build extracts what it needs from it; the VPK you build contains data from your copy and must not be shared.

### Status

- Plays on a real PS Vita (vitaGL, SDL2 audio). Fight loads use an asset pack (`umk3.pak`) built from your `.ipa`.
- Known: Classic Sub-Zero's arm kombo has missing poses — the iOS release's own frame lists lack them (`RPCOMBO6`–`RPCOMBO10`); the port borrows the four that exist elsewhere and holds the last pose for the fifth, which no fighter has.
- Changes from 2026-11-14 (batched blood drops, lighter log, kombo backports from upstream) build cleanly and have been run on a Vita; the kombo options were tried (the default `kombo_slide=1` plays best). See [`docs/PROGRESS.md`](docs/PROGRESS.md).

### Building (Linux / WSL)

Requires [VitaSDK](https://vitasdk.org/) with vitaGL, SDL2 and SDL2_mixer (`vdpm`), Python 3 and, optionally, Pillow (for the icon).

```bash
python3 vita/apply_cmake_patch.py      # once; idempotent
./vita/build.sh path/to/UMK3_1.2.59.ipa
# -> build-vita/umk3.vpk, vita/data/ (game files) and vita/data/umk3.pak
```

Install the VPK with VitaShell, then copy the data to `ux0:data/umk3/` (`./vita/deploy_data.sh <VITA_IP> --all` over VitaShell's FTP). Settings are in `ux0:data/umk3/vita.cfg`; see [`vita/README_VITA.md`](vita/README_VITA.md).

**Controls:** Square = High Punch, Cross = Low Punch, Triangle = High Kick, Circle = Low Kick, R = Block, L (or the rear touch pad's top-left corner) = Run, Start = Pause, Select = Moves list.

### Credits

- **[MaryNCRT](https://github.com/MaryNCRT)** — the [clean-room decompilation of the iOS release and the PC port](https://github.com/MaryNCRT/Ultimate-Mortal-Kombat-3-iOS-Decompiled) this port is built on, including the kombo fixes backported here (KOMBO SLIDE, 0.2.0; kombos that share a start, 0.1.17).
- **[Rinnegatamante](https://github.com/Rinnegatamante)** — [vitaGL](https://github.com/Rinnegatamante/vitaGL), the OpenGL implementation the Vita renderer runs on.
- The **[VitaSDK](https://vitasdk.org/)** project and its packages (SDL2, SDL2_mixer and their codecs).
- Vita port: gmejia11, with Anthropic's Claude (Claude Code).
- From the original project, kept here: code from [Blender](https://www.blender.org) (`src/thirdparty/blender`, GPL); banner renders by [ermaccer](https://github.com/ermaccer) ([CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), `docs/img/banner.jpg`); UHD logo by [u/JuananoLaGarza](https://www.reddit.com/r/MortalKombat/comments/mvm4uo/ultimate_mortal_kombat_3_logo_redone_in_uhd/); key and button pictures from Julio Cacko's [Free Input Prompts](https://juliocacko.itch.io/free-input-prompts) (CC0).

The original project's documentation (how the game works, the engine, the file formats, the verification method) is in [`docs/`](docs/); for its PC builds and releases, see [its repository](https://github.com/MaryNCRT/Ultimate-Mortal-Kombat-3-iOS-Decompiled).

*Ultimate Mortal Kombat 3* was created by Midway Games and ported to iOS by EA Mobile. This project is unaffiliated with Warner Bros. Entertainment or Electronic Arts, and distributes no copyrighted code or assets. Licensed GPL-2.0-or-later, as the project it is based on ([LICENSE](LICENSE)).
