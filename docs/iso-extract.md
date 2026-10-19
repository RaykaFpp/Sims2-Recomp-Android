# ISO Extraction Record

## Image

`Sims 2, The (USA).xiso.iso`, 1,986,920,448 bytes, located in `D:/sims2` (git-ignored; never commit it).

## Tool

`tools/xiso_extract.py` — a stdlib-only XDVDFS reader with subcommands `info`, `list`, and `extract`.

## Reproduce

```
python tools/xiso_extract.py info    "Sims 2, The (USA).xiso.iso"
python tools/xiso_extract.py list    "Sims 2, The (USA).xiso.iso"
python tools/xiso_extract.py extract "Sims 2, The (USA).xiso.iso" assets
```

## How the reader works

XDVDFS stores files in 2048-byte sectors. The volume descriptor lives at sector 32 of the game partition: it begins and ends with the 20-byte magic `MICROSOFT*XBOX*MEDIA` (the trailing copy is at offset `0x7EC` of that sector), and at offset 20 from the magic it holds the root directory's sector, the root directory table size, and a FILETIME timestamp, as `<IIQ`.

Because a disc image may be a bare game partition or a full dump with video partitions in front of it, the reader probes known game-partition bases first, in this order:

| Probe base | Layout it covers |
| --- | --- |
| `0x00000000` | trimmed image / bare game partition |
| `0x0000FD90000` | XGD layouts |
| `0x00018300000` | XGD layouts |
| `0x00002080000` | XGD1 video+game |
| `0x000001FB20000` | XGD2/XGD3 |

A base matches only if the magic is present at both the start and the end of sector 32 at that base. If no candidate matches, the reader scans the start of the image (up to `0x1A000000` bytes) for a sector-aligned magic and derives the base from it.

### Directory entries

Each directory is a table of entries forming an AVL tree keyed by name. One entry is:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | left link, in 4-byte units inside the table (`0` / `0xFFFF` = none) |
| 2 | 2 | right link, in 4-byte units inside the table |
| 4 | 4 | start sector of the file or subdirectory |
| 8 | 4 | size in bytes |
| 12 | 1 | attributes (`0x10` = directory) |
| 13 | 1 | name length |
| 14 | name length | name (latin-1) |

So an entry is 14 bytes plus its name. The reader walks the tree from offset 0, follows both links, recurses into entries with the directory attribute, and yields files depth-first. File data is read at `base + sector * 2048` in 1 MiB chunks. Extraction skips a file that already exists with the right size (unless forced).

## Volume geometry (this disc)

- Partition base: `0` — the image is a bare game partition, not a full dual-layer dump.
- Root directory sector: 264.
- Files: 55, totalling 1,986,340,175 bytes.

The `extract` run wrote those 55 files into `D:/sims2/assets`.

## Contents

Top level (in `assets/`):

| Path | Size (bytes) | Notes |
| --- | --- | --- |
| `default.xbe` | 3,399,680 | Executable, magic `XBEH`, SHA-1 `16f9e41ce26c315a758f25d43ba1fa9d3809d1ad` — see `docs/xbe-analysis.md` |
| `default.NGH` | 1,370,112 | |
| `sims2.bin` | 2,960 | |
| `DATA/` | — | 26 files, see below |
| `movies/` | — | 26 `.xmv` files, see below |

`DATA/` (26 files): `.arc` archives plus `index.ind` / `index1.ind` and the `audiostr.xsb` / `audiostr.xwb` XACT sound bank / wave bank. Notable entries:

| Path | Size (bytes) | Notes |
| --- | --- | --- |
| `DATA/samples.arc` | 534,554,060 | Largest archive |
| `DATA/textures.arc` | 256,900,590 | |
| `DATA/movies.arc` | 191,522,816 | |
| `DATA/datasets.arc` | 183,339,882 | |
| `DATA/audiostr.xwb` | 162,260,992 | XACT wave bank |
| `DATA/models.arc` | 114,297,923 | |
| `DATA/animatio.arc` | 24,114,632 | |
| `DATA/quickdat.arc` | 14,497,786 | |
| `DATA/index.ind` | 451,204 | |
| `DATA/audiostr.xsb` | 2,524 | XACT sound bank |

The other `DATA/` archives are `ambients.arc`, `audiostr.arc`, `binaries.arc`, `characte.arc`, `edithtre.arc`, `effectsa.arc`, `effectse.arc`, `effectss.arc`, `emitters.arc`, `flashes.arc`, `fonts.arc`, `levels.arc`, `shaders.arc`, `soundeve.arc` and `soundtra.arc`.

`movies/` (26 files): `.xmv` videos —

- the EA logo in twelve localised variants: `ea_games.xmv` plus `_pal`, `_da`, `_du`, `_fi`, `_fr`, `_ge`, `_it`, `_no`, `_po`, `_sp`, `_sw`;
- the Sims 2 console intro, attract and ending: `s2c_intro.xmv`, `s2c_intro_pal.xmv`, `s2c_attract.xmv`, `s2c_attract_pal.xmv`, `s2c_ending.xmv`, `s2c_ending_pal.xmv`;
- the Urbz intro, attract and ending (this engine family's previous title): `urbz_intro.xmv`, `urbz_intro_pal.xmv`, `urbz_intro_jp.xmv`, `urbz_attract.xmv`, `urbz_attract_pal.xmv`, `urbz_ending.xmv`, `urbz_ending_pal.xmv`, `urbz_ending_jp.xmv`. Largest is `urbz_attract.xmv` at 65,163,264 bytes.

## Notes

The extracted tree in `assets/` and the ISO itself are the user's own game data. They are never committed and live only in `assets/`. The SDK can also read the ISO directly.
