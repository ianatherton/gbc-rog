# gbc-rog

Roguelike for Game Boy Color, built with [GBDK-2020](https://github.com/gbdk-2020/gbdk-2020).

## Setup

1. **GBDK is already in the repo** (extracted under `gbdk/`). If you need to re-download:
   ```bash
   curl -sL "https://github.com/gbdk-2020/gbdk-2020/releases/download/4.5.0/gbdk-linux64.tar.gz" -o gbdk-linux64.tar.gz
   tar -xzf gbdk-linux64.tar.gz
   ```

2. **Optional:** set `GBDK_HOME` if you use a different path:
   ```bash
   export GBDK_HOME=/path/to/gbdk
   ```
   Default is `./gbdk` (project root).

## Build

```bash
make
```

Output: `build/gbc/gbc-rog.gbc`

- `make gbc` – build GBC ROM only  
- `make clean` – remove build artifacts  

## Run

Use any Game Boy (Color) emulator, e.g. [SameBoy](https://sameboy.github.io/), [BGB](https://bgb.bircd.org/), or [mGBA](https://mgba.io/):

```bash
sameboy build/gbc/gbc-rog.gbc
# or
mgba build/gbc/gbc-rog.gbc
```

## Docs

- [GBDK docs](https://gbdk.org/docs/api/)
- [GBDK manual](gbdk/gbdk_manual.pdf) (in repo)
