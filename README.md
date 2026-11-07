# gmud3ds

A native C11 rewrite of the WQX handheld RPG *Bai Jin Ying Xiong Tan Shuo* (白金英雄坛说) for the Nintendo 3DS, built on libctru and citro2d. It is not an emulator and does not read original saves; gameplay rules were checked against a public disassembly of the original game. The single-player game is implemented end to end, including character creation, quests, combat, mini-games and the six-tablet ending.

## Disclaimer

Unofficial, non-commercial fan work, not affiliated with the original rights holders or Nintendo. Nothing here grants rights to the game's assets: `data/` and `romfs/` are generated locally from assets the user is legally entitled to use, and must never be committed. The game's name, artwork, story and text remain with their owners.

`icon.png` and `icon_small.png` paste original hero sprite pixels, recoloured 1:1 with the port's classic-green palette. They will be removed on request.

## Build

Requires devkitPro `3ds-dev`, Python 3.10+ and GNU Make. With `DEVKITPRO` / `DEVKITARM` set, point `ref/gmud` at a local disassembly of the original game (with its extracted data) and run:

```sh
python tools/extract.py --src ref/gmud/src --out data
python tools/font_build.py --hex ref/unifont/unifont_all-16.0.01.hex.gz --out data/font.bin
python tools/sfx_build.py     # optional; the game runs silently without it
make -j4
```

`make` stops with an error repeating the extractor and font commands if required data is missing. The result is `gmud3ds.3dsx`.

For a CIA, run `bash tools/cia/build_cia.sh` with makerom and bannertool under `.tools/`; output is `build/cia/gmud3ds-nocrypt.cia`.

## Notes

- Tested on New 3DS LL and Azahar; the original 3DS and the opt-in `TOP_WIDE=1` output are untested.
- Hardware audio needs `sdmc:/3ds/dspfirm.cdc`.
- Saves live in `sdmc:/3ds/gmud/`: three manual slots, one read-only auto-save, plus `settings.cfg` for the theme.
- The original link-cable arena and password system are not implemented.

## Credits

Thanks to the people who preserved and documented the original game's assembly; no disassembly sources are included here.

Built with devkitPro, libctru, citro2d and GNU Unifont; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## License

MIT; see [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
