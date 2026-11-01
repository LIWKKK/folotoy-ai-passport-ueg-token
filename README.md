<p align="right">
  <strong>English</strong> · <a href="README.zh_CN.md">简体中文</a>
</p>

# UEG Digital Life Card · Firmware & Source Release

[![chip](https://img.shields.io/badge/chip-ESP32__C3__8MB-red)]() [![idf](https://img.shields.io/badge/ESP--IDF-v5.5-blue)]() [![version](https://img.shields.io/badge/WE--TOKEN-v1.0.0-green)]() [![build](https://img.shields.io/badge/build_date-2026--09--13-lightgrey)]()

> Firmware for the [FoloToy AI Passport](https://ai-passport.folotoy.cn/) (ESP32-C3, 8 MB Flash) that turns the device into a pocket "digital life card": flip through pages to check AI-platform balances, today's spend, a monthly usage heatmap, plus the next F1 race countdown and championship standings.

Official play page: <https://ai-passport.folotoy.cn/plays/342/>

**Build gate**: `tools/validate.sh` all green (repository checks / host tests / firmware layout / merged image)

---

## What it is

Five pages, navigated with the UP/DOWN keys:

| Page | Feature |
|---|---|
| 1 | **Identity card** — UEG digital life card home |
| 2 | **Balance board** — remaining quota per AI platform, today's spend |
| 3 | **Usage heatmap** — last month, day by day |
| 4 | **F1 board** — next-race countdown, driver/constructor standings, F1 news (Beijing time) |
| 5 | **Connection setup** — Wi-Fi and platform API keys (press OK to open the config hotspot) |

> The full source tree lives on the `main` branch of this repository; the `ueg-token-src-*.tar.gz` attached to each Release is a packaged snapshot of the same tree.

## Repository layout

| Directory | Purpose |
|---|---|
| `main/` | Application code (five-page UI, networking, config hotspot) |
| `components/` `bootloader_components/` | Custom components and bootloader hooks |
| `assets/` `docs/` | Brand assets and documentation |
| `tools/` `tests/` | Validation scripts and tests |

## Flashing

Release assets (full image / app-only / source snapshot / SHA256 / one-shot script — see the [Releases page](https://github.com/LIWKKK/folotoy-ai-passport-ueg-token/releases)):

```bash
# Option 1: bundled script (verifies SHA256, calls esptool, picks full/app automatically)
./flash.sh /dev/ttyACM0            # full image (default)
./flash.sh /dev/ttyACM0 app        # app partition only

# Option 2: manual full image (from 0x0: bootloader + partition table + app)
esptool.py --chip esp32c3 --baud 921600 \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  write_flash 0x0 FoloToy-AI-Passport-full.bin

# Option 3: manual app-only (from 0x10000, keeps the current bootloader and partition table)
esptool.py --chip esp32c3 --baud 921600 \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  write_flash 0x10000 FoloToy-AI-Passport-app-only.bin
```

### ⚠️ Before you flash

- **A full image overwrites the NVS partition** (`0x9000`) → saved Wi-Fi credentials and platform keys are wiped; re-configure after boot. Use the app-only method to keep your configuration.
- **Protected regions are untouched**: the full image writes up to `0x2bbd60`, while the `cardid` partition sits at `0x356000` and `recovery` at `0x700000`, both outside the write range. `tools/verify_firmware.py` enforces this.

## First-boot setup

The device opens a config hotspot automatically when un-provisioned:

1. Join the hotspot **`UEG-TOKEN-XXXX`** (last 4 digits come from the device MAC)
2. Password: `ordinarylife`
3. Open `http://192.168.4.1` (a private window avoids stale cache; the save button is pinned to the top of the page)
4. Enter Wi-Fi credentials and platform API keys → save → the device reboots and refreshes online

After connecting, flip to page 4 for the F1 countdown, standings and news.

## Rebuild from source

```bash
tar xzf ueg-token-src-20260913.tar.gz
cd ueg-token

# ESP-IDF v5.5 required (managed_components are fetched automatically; versions are pinned by dependencies.lock)
idf.py build

# Run the full validation gate (repo checks + host tests + firmware layout)
./tools/validate.sh

# Produce the merged image
idf.py merge-bin -o "$(pwd)/build/FoloToy-AI-Passport-full.bin"
```

> Note: `-o` of `idf.py merge-bin` resolves relative to the **build directory** — use an absolute path, or it raises `FileNotFoundError`.

## F1 data sources

| Data | Endpoint | Note |
|---|---|---|
| Next race | `api.jolpi.ca/ergast/f1/current/next.json` | 765 B; the full season schedule is 14 KB and requires scanning ~60% of it first |
| Drivers | `.../driverstandings.json?limit=5` | 2.4 KB with `limit` (10 KB without) |
| Constructors | `.../constructorstandings.json?limit=5` | 1.3 KB |
| News | `formula1.com/en/latest/all.xml` | The BBC RSS mirror was unreachable from the dev network; not used |

All times are displayed in Beijing time.

## License

Firmware and source are provided as-is for FoloToy AI Passport community play.
