#!/bin/bash
# build_all.sh - regenerate every asset from maps/stage1.json and the art sources, then build the ROM
set -e
cd "$(dirname "$0")/.."
python3 tools/gen_art.py
python3 tools/gen_sprites.py
python3 tools/gen_hud.py
python3 tools/gen_level.py > /dev/null
python3 tools/gen_tables.py
python3 tools/gen_screens.py
# snd/music.it, boss.it, sfx.it are shipped; gen_audio.py only rebuilds them from the source mp3, so it is optional
if [ -f snd/dark_space.mp3 ]; then python3 tools/gen_audio.py > /dev/null; else echo "audio: snd/dark_space.mp3 not found, keeping the shipped snd/*.it"; fi
python3 tools/tint.py
python3 tools/stage_music.py
rm -f soundbank.asm soundbank.h
make
