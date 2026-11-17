# SPACE RAYCAST FPS — прототип для SNES (Super FX + OpenSNES)

Doom-подобный sci-fi шутер от первого лица. Рендер стен/пола/потолка на GSU
(256x128, 4bpp, ~15 fps), спрайты/босс/HUD на PPU.

## Сборка
Положить папку в `examples/chips/space_fps/` дерева OpenSNES и выполнить `make`.
Результат: `space_fps.sfc` (512 КБ, Super FX). Ресурсы пересоздаются по порядку (нужны PIL, numpy, scipy):
`python3 tools/gen_art.py` (текстуры из textures_sheet.jpg, враги из *_front/back.jpg, бластер из scene_b.png — ваши рисунки),
`tools/gen_sprites.py` (шрифт, FX, босс), `tools/gen_tables.py`, `tools/gen_level.py`.

## Управление
D-pad — ход/поворот · L/R — стрейф · Y — бег · B — прыжок · A — огонь
X + Up/Down — взгляд вверх/вниз · SELECT — центр · START — рестарт

## Что есть
- Текстуры стен/панелей/пола/потолка нарезаны из вашего концепта и переведены в 5-битные классы текселей (яркость + направление грани из арта + эмиссивные акценты).
- Текстуры стен корабля, пол и потолок в перспективе, псевдо normal mapping
  (LUT текстур с направлением света для X/Y-граней), затемнение по дальности.
- Бластер слева внизу, ствол смотрит в перекрестье, качание при ходьбе/беге,
  опускание у стены, вспышка выстрела, смерть, урон (красная палитра).
- 3 врага из ваших листов (пингвин, скаут, наблюдатель): 15 кадров спереди + 4 кадра со спины (отступление), 7 уровней масштаба по дистанции (размер ~ 1/расстояние, как у стен), одновременно до 2 врагов, остальные из очереди волны.
  урон, смерть), 3 масштаба спрайтов, окклюзия по Z-буферу стен.
- Лабиринт 32x32, 5 триггеров секторов, телепорт-появление волн врагов.
- Босс-суперкомпьютер (BG2), стреляет плазмой; ярость при HP < 50%.

## Ограничения
Кадры анимации врагов получены процедурной деформацией одного рисунка (нет отдельных ракурсов), поэтому это черновая раскадровка; без звука; 3 дискретных масштаба спрайтов;
~15 fps. Проверено в эмуляторе luna, на железе не запускалось.

## v4
- Blaster redrawn: barrel points up-right into the crosshair.
- Enemies face the camera; circling = 3/4 + side view (flipped by direction), retreating = 3/4 back + back walk cycle. All 3 types (pengi, scout, watcher) spawn in rotation.

## v5
- 5 enemy types from the rotation sheets (penguin, one-wheeled robot, metal skeleton, biped, devourer): front, 20deg, profile, 135deg, rear. Strafe uses 20deg/profile flipped by screen direction; retreat uses 135deg then rear.

## v6
- player_god=1 in main.c (set 0 for normal damage); slower enemies; hit enemy flees 3 s then returns; ceiling raised (walls 2 units tall).

## v7
- Enemies: 10 smooth size levels with hysteresis, drawn as 16x16 pieces clipped per column by the wall z-buffer (no more showing through pillars), brighter palettes, x1.5 size.

## v8
- Sprite tiles and OAM are planned together each frame (no one-frame mismatch), HUD moved to the top band so enemies are clipped by the bottom blank, enemy/player minimum distance, tone-mapped enemy art.

## v9 — полный уровень по эскизу
- Карта 32x64 (`tools/gen_level.py`): корабль-«пентагон», ворота 1-11 (авто), K1D/K2D/K3D (синяя/жёлтая/красная карта), TRD (большие ворота босса, открываются плитой TR), зона EXIT.
- Предметы: кристаллы CR (9 на карте, для выхода нужно 8), бутылка BT (10 с суперсилы: ч/б экран, урон x2), 3 карты, оружие: W1 fuser, W2 quanton, W3 plasmagun, W4 railgun (SELECT — смена, X+SELECT — центр взгляда).
- Враги E1..E5 + BOSS-пингвин (30 HP, плазма/ракеты/лазер). Точки спавна просыпаются по расстоянию и линии видимости (до 2 врагов одновременно — лимит спрайтов).
- Выход: «NICE JOB!» на чёрном экране, START — уровень 2 (пока та же карта).
- Текстуры: два новых листа + ворота из door.png; ROM 2 МБ (64 банка).

## v9.1
- HUD: враги/предметы больше не заходят спрайтами в полосу HUD (лимит спрайтов на строку давал рябь).
- Оружие: убрана вторая рука; прячется у стены (зона 300 ед.), появляется при отходе/развороте.
- Стены: текстуры вдвое мельче (16x16 деталь, повтор 2x2); потолок — светлая панель с круглыми чёрными лампами.
- Прыжок: пол и потолок пересчитываются по высоте глаз (как стены), прыжок чуть ниже.

## v9.2
Dark Doom3-style lighting with blue wall lights, enemy aura, spawn ovals, death flash, medkit drops (every 4th), night vision (3 min), super-attack bottles, auto-aim brackets + life squares, enemy fire rules, regen, waves of 3 (2 on screen max), Wolf3D minimap, new controls (Y/B strafe, X jump, A short/long, L look, R shoot/melee), HUD ripple fix.

## v9.3
Управление: D-pad ходьба/поворот, Y шаг влево, A шаг вправо, X прыжок, B = следующее оружие/прибор, долгое A = переключить класс (оружие / приборы),
L+стрелки взгляд вверх/вниз (L отдельно = в центр), R = выстрел или вкл/выкл прибора, долгое R = ближний бой.
Оружие (урон в квадратах / заряд): fuser 1 / каждое нажатие, quanton 2 / 2 с, plasmagun 3 / 3 с, railgun 4 / 4 с, rocketgun 5 / 5 с, beamchain 6 / 6 с, kozematy 7 / 7 с.
Жизнь игрока 10 квадратов; у врагов квадраты вдвое мельче и вдвое больше (E1..E5 = 2,4,6,8,10, босс 20).
Приборы: nightvisor (зелёный, ярче, показывает скрытых скелетов, 3 минуты работы), optovisor (жёлтый, текст на стенах + стрелка к цели), redvisor (красный, виден круглый TR).
Головоломка: optovisor -> прочесть надпись у ворот 11 -> на двери босса загораются лампочки -> redvisor -> найти красную кнопку TR в левом коридоре зала -> ворота босса открываются.
Экраны: титул (tools/gen_screens.py), STAGE1..6 (пока тот же уровень), плавное появление/затухание через палитру.
Звук: SNESMOD, музыка snd/music.it (петля из вашего mp3 + бас, арпеджио, ударные), 24 эффекта snd/sfx.it (tools/gen_audio.py).

## v9.4 (see also v9.5 below)
- Music: tools/gen_audio.py builds snd/music.it - 8 breakbeat chops of the user's mp3 + NES-style chiptune (triangle bass, pulse arp/lead, pad, drums), 6 patterns, long order. Replace snd/music.it with your own tracker module (.it/.xm/.mod, up to 8 channels, short chip samples) to change it.
- Sound effects: 24 NES/Contra-style effects (jump, shot pop, hit, pickup...), generated by tools/gen_audio.py.
- Controls: hold A = item menu: LEFT/RIGHT = class W (weapons) / V (visors), UP/DOWN = pick inside the class, R = fire / toggle visor. Y = strafe left, B = strafe right. HUD shows the active class in [ ] brackets.
- Stuck? Hold X for 2 s: the screen darkens like closed eyes and you wake up at the level start.
- Title: 1:1 picture in BG mode 3 (256 colours). STAGE n screens: white sprite-font text on black.

## v9.5
- One visor at a time (turning one on turns the others off).
- Weapons drawn larger and closer to the camera (art scaled 1.3x, back end cropped by the canvas, bottom cut by the view edge).
- New music: Contra / Battle Toads / Double Dragon style (E minor, 152 bpm, galloping bass, pulse lead + echo, rock drums, fills) - tools/gen_audio.py music().
- Buttons: hold B = item menu (LEFT/RIGHT class W/V, UP/DOWN pick), A = strafe right, Y = strafe left.

## v9.6
- Weapon tiles are now copied during vblank (fixes the vanishing top of the gun when switching weapons).
- Bottom HUD strip (BG2 tiles, lines 156..168; tools/gen_hud.py): [WEAPON] / [VISOR] name, ammo icon + count (fuser infinity, W2-W5: 5, W6-W7: 2), RELOAD progress bar when empty, charge bar between shots; visor: ON/OFF and battery bar for the nightvisor. The empty weapon reloads by itself, you may switch meanwhile.
- Breakbeat music at 137 bpm (tempo of the mp3): syncopated drums, sub bass, stabs, lead, and beat-aligned 16th-note chops of the mp3 (bar at 112.5 s). tools/gen_audio.py music().

## v9.7
- Level music = your ambient_the_wing.xm (snd/ambient_the_wing.xm), converted by tools/xm2it.py: 24 XM channels -> 8 SNES voices (rhythmic "and fly" and "kb" first, pads fill gaps, long notes capped, ~12% of notes dropped), samples resampled to 4-8 kHz with shortened cross-faded loops. Run `python3 tools/gen_audio.py` to rebuild snd/music.it; the breakbeat track stays as snd/music_break.it.

## v9.8
- Music: your universalnetwork2.xm, converted 1:1 (6 channels, with arpeggio/slide/volume effects) by tools/xm2it_chip.py. ONE module, two sections sharing the samples: CALM = orders 0..7 (the 4/4 kick groove), ACTION = orders 8..13 (the dense part). The last row of each section jumps back to its own start (Bxx). The game calls snesmodPlay(0) / snesmodPlay(8) when enemies appear / have been gone for 7 s (MUS_ACTION_ORDER in main.c).
- Gunshots (FUSER, QUANTON, PLASMA, RAIL, ROCKET, BEAM, KOZE, ESHOT) are now bright pistol-style cracks: click + noise crack + low thump + tail (gun() in tools/gen_audio.py).
- Weapon art is drawn uncropped again and sits above the bottom HUD strip; waiting for the new weapon sprites (drop PNGs into tools/art_src/new/gun_*.png and run tools/gen_art.py).

## v9.9
- Three tracker songs, each its own SNESMOD module (snd/music.it, snd/action.it, snd/boss.it), converted by tools/mod2it.py (XM + S3M reader; keeps the 8 busiest channels, the 14 most used instruments, resamples samples to fit the SPC RAM, per-pattern row counts, jump back to the first order):
  State.xm -> background music (orders 7..18), Darker.xm -> fight with enemies (orders 2..21), WIRED96R.S3M -> boss fight (orders 6..17).
- main.c mus_set(): enemies around -> battle track, penguin boss -> boss track, 7 s without enemies -> background again. Switching reloads the module (short hitch) and then the 16 effects (sfx_init).
- SNESMOD has only 16 effect slots: the 24 sound ids now share 16 samples (pickups share PICK, switch/ready share BLIP, death uses EDIE).
- Weapon W2 (Quanton) uses the new blue gun sprite (tools/art_src/new/gun_quanton_new.png).

## v9.10
- Weapons: every gun is rotated so its barrel points from the lower-left corner at the cross-hair (AIM_DEG in tools/gen_art.py, sprite position ox=16, oy=94 in draw_weapon()).
- Music rebuilt with tools/mod2it.py (orders, point budget, title, max instruments, role, max channels):
  background = State.xm orders 28-37, melodic channels only (arpeggio chords + pads, no drums);
  battle = Darker.xm rhythm channels only (breakbeat drums + one bass), orders 2-5,7,8,12,13,15,16;
  boss = WIRED96R.S3M rhythm channels only, orders 6-13.
  Samples are cut to the length that is really played (no more 0.25 s truncation), loops are shortened.
- Gunshots: harder saturation in gun() (RMS about 2x), module volume 235 -> 160 so the music no longer covers the shots.

## v9.11
- Weapon colours were wrong because res/spr_defs.h (palettes) was not regenerated after tools/gen_art.py: always run `python3 tools/gen_art.py && python3 tools/gen_sprites.py`.
- Barrel angle is measured from the top edge of each gun (Quanton no longer looks into the corner); lasers / plasma streaks are removed from the sprites (opening in gun_cut()).
- Shot effect: muzzle star + a tracer of 8 bright balls from the muzzle to the cross-hair (draw_weapon(), OAM_CHG slots; muzzle positions in res/weapon_muz.h).

## v9.12
- Minimap / objective arrow: one arrow model (arrow_cell() in tools/gen_sprites.py) rotated for N, NE, E; the other directions are flips.
- Weapons: a magazine (W2-W5: 5 shots, W6-W7: 2) is fired with a short gap between shots (w_cd 18..52); the reload bar starts only when the magazine is empty.
- Battle music = your Ultimate_Mod_Collection_industrial_DUNE.XM (snd/dune.xm), orders 10-21, all 8 channels: `python3 tools/mod2it.py snd/dune.xm snd/action.it 10-21 55000 battle 14 all 8`.

## v9.13
- Weapons: new sprites in order beamchain, quanton, plasmagun, rocket launcher, railgun, fuser, kozematy.
- Shot effects: muzzle flash only; rocket blast at the crosshair; railgun white screen flash; beamchain thin beam muzzle to crosshair; kozematy two blue parallel lines edge to edge (BG2 row 7).
- Enemies: no crouch/jump, teleport-cylinder spawn effect, continuous fire, flee out of camera view when hit, run speed 4x walk, ~1 in 3 rushes for melee.
- Boss: every 2 life squares lost -> freeze 2 s, chase 4 s, return to spawn, resume shooting; slower fire, less damage.
- Extra E5 spawns before the TR; stele text "LOOK"; exit portal in the boss room when all crystals are found; level darker (DIM 0.62).

## v10
Game
- Enemy projection: the 64 px sprite is the biggest one, so the enemy now stops growing and sinking at that distance (`near_r`): feet are clamped, the hero is pushed out to that radius, melee range follows it.
- Enemy shots: ~70 % aimed exactly at the hero's position, ~30 % are misses that fly past (rotated 70..100/256) and play a ricochet sound (`S_RICO`) when they pass.
- 3R: the hero has 3 respawns; on START after death he returns to the start point with full health and the level keeps its state (crystals, doors, enemies). After the last one: GAME OVER and a new game. Shown top right as `3R`.
- Chaingun (weapon id 7, switch order: ... plasmagun, chaingun, railgun ...): hold R to fire, 10 shots/s, 100 rounds, every 5th bullet takes one life square (10 bullets = 2 squares), reload 4 s. Lies in the boss room next to the exit zone (item kind 17 at 14.5,1.5).
- Arena rooms (`ROOMS` in gen_level.py / "rooms" in the map file): kind 1 = crystal room: enemies and battle music wake up when the crystal is taken; kind 2 = 2 s pause after entering. While a fight runs every door around the room is locked (banner LOCKED); the doors open when all of the room's enemies are dead (ROOM CLEAR, music goes back to the calm track). Rooms with the cloaked E3 and the hall are not arenas (no soft-lock if the nightvisor battery is empty).
- "LOOK" wall text and the optovisor trigger are on the north wall of the start room. Reading it powers the boss gate and wakes up 3 class 6 enemies (new sprite, the tank robot of art_src/watcher_*.jpg): they are visible only with the optovisor and every 5..9 s they blink next to the hero (teleport rings) wherever he is, until killed.
- The cloaked E3 enemies of the K2 room are now drawn if and only if the nightvisor is on (no more flicker).
Map editor and pipeline
- `python3 tools/mapedit/server.py` then http://localhost:8766/ : 2D editor (tiles/walls with texture picture, doors, items, enemy spawns of all 7 classes + boss, start, exit zone, TR button, LOOK text, ghost spawns, arena rooms, floor/ceiling texture, wall texture upload, music choice/upload (.xm/.s3m), briefing picture upload, validate, download/load JSON). Stage 1..6 -> maps/stageN.json.
- The build reads maps/stage1.json: gen_level.py (tiles, objects), gen_art.py (textures), gen_screens.py (briefing picture after "STAGE X"; none = straight into the game), stage_music.py (converts uploaded music with mod2it). `tools/build_all.sh` runs everything and builds the ROM.
- The ROM embeds stage 1 only for now; stages 2..6 replay it (as before). Files maps/stage2..6.json are saved and valid, the game does not load them yet.

## v10.1
- Music: once the battle track has started it stays until 60 s have passed since the last enemy was around (`mus_hold` = 3600), then the calm track returns; a new enemy switches to battle again.
- God mode: UP UP DOWN DOWN L R toggles it (banner GOD MODE ON/OFF), the picture goes white for 3 s (1 s full, 2 s fade).
- Super bottle: 30 s immortal (and double damage), the world is drawn light for the whole time.
- Weapon/visor menu: LEFT/RIGHT (weapons <-> visors) now has to be held ~0.1 s and is ignored while UP/DOWN are held or were just pressed, so a diagonal slip of the thumb no longer flips the class.

## v10.2
- Sectors (Doom/Portal style): tools/gen_level.py flood-fills the free tiles into sectors cut at the doors (`sec_map`, `door_sec`, `NUM_SECS`). `sectors_update()` wakes the hero's sector plus the sectors linked by open doors; enemies, spawn points, items and doors of sleeping sectors are neither simulated nor drawn. Closed doors are already walls for the GSU rays, so the render never looks into a closed room.
- Door locks are recomputed only when a room changes state (before: every door, every frame). The game logic now runs while the GSU draws the frame instead of before it. The GSU render itself (about 0.9 M GSU clocks per frame) is still the frame-rate limit.
- Death: the hero falls to the floor (eye drops, view rolls up), a white strip (HDMA on the fixed colour) climbs from the bottom to the top, then he stands up where he died with full HP. Three resurrections on the spot (the `R` counter); when they are spent he is thrown to the level start, the counter is refilled, locked rooms are unlocked (state 4 = dormant), their living enemies vanish and the fight (2 s pause, locked doors, the remaining enemies) starts again when he enters the room. Killed enemies stay killed. No game over and no START needed.

## v10.3
- Kozematy: the two blue lines now shrink from full width to a point at the cross-hair in ~10 frames (own timer `koze_t`) and vanish.
- Every visor has a battery (3 min worn, charges at half speed while off), HUD gauge for all three; empty = switches off ("VISOR OFF").
- Having a visor is enough to see what it reveals: cloaked E3 (nightvisor) and class 6 (optovisor) are drawn without switching it on.
- Anti-stuck: forward/back held for 2 s with less than ~0.25 tile of movement -> the hero is moved toward the sector centre (`sec_c`, generated by gen_level.py), or onto it when blocked.
- Hold X (long jump): the hero wakes up in the centre of his current room/sector instead of the level start.
- Every arena room spawns at least 6 enemies in total (`sp_left` per spawn point = ceil(6/points)); only 2 fit on screen at a time (OAM budget), the rest arrive as waves.

## v10.4
- Kozematy lines: the BG2 row is written first thing in the blank and again for 4 frames (a write that missed the blank used to stick until the next scene reset).
- LOOK is always drawn on the start-room wall; with the optovisor on it turns into POWER ON after 0.5 s of looking at it (range and screen window widened, dwell counted in frames), which powers the gate/TR button for the redvisor.
- Picking up the nightvisor opens a card with the three visor icons and their descriptions; any button closes it (the game is frozen, dt = 0). The nightvisor and medkit icons were being overwritten by the beam glyph cells (62/63) in the sprite sheet: they now live in the unused ':' and '!' glyph cells (green goggles like the optovisor).
- Weapons: every press fires while the magazine has rounds (5-frame debounce instead of the 10..52 frame gap, no more blocking by the lowered weapon near walls); each weapon keeps its own reload.
- A melee blow makes the rusher step back for ~0.7 s and then shoot.
- Enemies: arena rooms get half of their spawn points' share, 2..6 per room, 2 on screen at a time; places outside arenas: 2 per point.
- Immortality bottle: the light world is much dimmer; visors switch off and cannot be used while immortal.
- Note: a 448-byte VRAM copy over several map rows wiped the HUD strip; the card now writes row by row.

## v10.5
- Anti-stuck 1 s; auto knock-back on wall collision; enemies are passable.
- Info card: black background, icons from top, gap, "PRESS ANY BUTTON" centred at bottom.
- Battle->ambient music hold 30 s; enemy count doubled.
- Koze weapon sprite replaced; weapon pickup icons redrawn (test tube, atom, rocket, magnet, lightning, bullet, two lines).
- 9 of 9 crystals required; the 9th drops at the boss position on boss death; "NEED ALL CRYSTALS" at exit.
- Immortality bottle: 10 s, no light tint, life bar and R hidden.
- Weapon switch clears Reload and refills the previous weapon.
- Speed: GSU now runs in cache mode (asynchronously, CPU keeps ROM), split floor/ceiling and walls jobs, row-pair plotting, WRAM map/tables, throttled world update, HUD caching, incremental weapon tile upload. ~11 -> ~13 fps in the rotation benchmark.
- Limits: OAM sprites cannot be scaled in hardware, so the 2x close-up of items/enemies is NOT done (items only no longer sink off-screen). Walls pass is the remaining bottleneck.

### v10.5b
- Kozematy sprite now rotated like the other weapons (barrel to the cross-hair).
- Enemy feet: uses the sprite's lowest row; no lifted feet near the player; camera-height formula matches the wall renderer.
- Items grow: the nearest item (< 640 units) is drawn as a 32x32 2x copy (spr_big.bin) in the block of the big plasma ball; plasma bolts have priority.
- Visor card: black backdrop, rows built outside the blank and copied 2 per frame (the blank is only ~6000 CPU cycles), rewritten in a loop.
- Pad is sampled by the NMI every frame (taps shorter than one game round were lost: weapon switch / HUD felt frozen).
- GSU programs load with MVN (4x faster than gsuCacheLoad): ~12.8 -> ~13.6 fps in the rotation benchmark.
- Weapon no longer vanishes while firing (tiles upload in the background); small recoil added.

## v10.6
- Rooms: 3 waves of 3 enemies (5 waves with a crystal, 6 in the LOOK room whose doors lock); one melee rusher per wave (touches, then charges 3 s), the last enemy of a wave shoots.
- Radiation: -1 power cell every 5 s, no regeneration. Bottle = radiation off for 30 s. Medkits (max 5, counter "L") fire automatically at the last cell and refill the bar; none left = respawn.
- Room cleared: "DOORS UNLOCKED" 2 s, music stops; battle music returns on new combat; ambient after 10 s of peace. Boss music = flight_attack.xm (orders 0-17, SPC RAM limit).
- Briefing card with 13 lines.
- Sprites vs walls: stored wall distances are shifted by the camera turn since they were rendered, and a 16 px piece needs 3 of 3 column tests.
- Boss, 6 stages: 0 rockets + charge (touch 2 cells) -> refill + medkit; 1 energy waves along the floor (jump = wave returns and hurts the boss, boss shielded otherwise); 2 invisible (nightvisor) + teleport race with 3-2-1 countdown (loser takes 4); refill + medkit, nightvisor removed; 3 boss wanders, optovisor reads "BOSS KEY" on the north wall, optovisor removed; 4 rockets, boss vulnerable; 5 5 s pause, redvisor shows the button, touch it = boss dies and drops the last crystal.
- Limits: MAXE = 2 enemy slots; enemy sprites cannot grow beyond 64 px; race circles are shown as the countdown digits 3-2-1-GO plus the ring on the floor.

## v10.7
- Briefing card: narrow 6 px text, dash before each line (tiles pre-composed by tools/gen_card.py -> res/card_tiles.bin).
- Music: calm and battle songs are merged into ONE module (tools/merge_it.py, sources snd/music_calm_src.it / music_battle_src.it); switching = snesmodPlay(order), no upload, no lag. SNESMOD has no per-channel/per-track volume, so a true parallel crossfade is not possible; the module was reduced to ~58000 sample points (module + sound effects must fit SPC RAM). Boss music is still its own module (loaded once when the boss wakes).
- 5 medkits at the start; radiation: one cell per 15 s, without the red flash.
- Perspective: sprites are capped at 64 px, so enemies keep their distance: closer than the size cap they back away at 2x the hero's speed (a cornered one pushes the hero back instead). A melee blow is a point-blank shot: 16 damage (a plasma bolt is 8).

## v10.9 - hybrid enemy renderer (OBJ far, Super FX close)
- Far enemies stay hardware sprites. When an enemy's apparent height reaches the 64 px sprite limit (s >= 227) the OBJ
  pieces are hidden and a third GSU job (`gsu_s.sfx`) draws the same frame scaled, into the wall picture, from the same point
  (feet on the floor, growing like the walls). It is depth-tested per 2 px column against this frame's ZBUF.
- Colours: on the way in (s 150..227) the enemy's OBJ palette is blended toward the BG1 palette entries the Super FX uses
  (`tools/gen_gspr.py` builds the colour map `res/gspr.h`), so the swap is seamless; the reverse on the way out.
- Source frames: 20 frames per type, packed 4bpp (2 KB each) in ROM; the current frame is copied ROM -> WRAM $7F -> Game Pak RAM
  $C000/$C800 only when the frame changes.
- `near_r` now allows s up to 340 (canvas about 96 px). Cost: about 8-20 % of a frame while an enemy is close.
- Not yet: hurt flash, cloaked/optovisor tint, items and the boss in Super FX mode. `-DGS_TEST` hooks a scripted test enemy.

## v10.10
- Near Super FX enemies hide far OBJ enemies standing behind them (no see-through).
- No more medkits from kills: once per wave a medkit falls from a white ceiling portal at a random spot, slides, bounces off walls/enemies and stops.
- First-run HUD tour: grey picture, controls frozen, white frame + big caption on life / crystals / 3R / 5L.
- Debug cheat: Left Left Right Right L R = clear enemies, all visors/weapons, 8 crystals, LOOK + TR active, boss gate open, teleport to the TR room (Up Up Down Down L R = god mode).

## v10.11
- LOOK on the wall is now four alien glyphs (res/alien_glyphs.txt -> frame 23 of res/spr_big.bin via tools/gen_alien.py, drawn through the shared 32x32 big block; hidden while a plasma ball / close item uses the block).
- Medkits: a new one falls only if none is still falling/lying and the counter is not full.
- Medkit portal and fall start at the real ceiling (4x eye height, z~500), not at 2x eye height.
- Skip cheat (Left Left Right Right L R) no longer forces the boss stage: the boss keeps its full wave/quest system (immortal bar refills until the quests are done).
- Debug build: make CFLAGS="-DDBG_TP -DDBG_TPX=.. -DDBG_TPY=.. -DDBG_TPA=.." starts at a given point.

## v10.12 — medkits, visor menu, TR eyes, boss level redesign
- Medkits: a drop after every wave of any fight (also in corridors) while the hero has fewer than 5 lives (L); each respawn (R) refills the hero to 5 L.
- Visor selector: switching to the visor pick hides the weapon, shows the visor icons and freezes the game; switching back returns the weapon and the game.
- TR plate: an eye icon whose pupil looks left and right.
- Boss level: round lamps on the walls, a glyph wall behind the boss, wave 1 shooting; then a white flash, a black screen "JUMP TO SURVIVE / IN THE DARKNESS", the picture opens from the centre as two halves, the room is dark (nightvisor needed) and the line-wave jump minigame follows; then a lit room with the boss at the glyph wall (optovisor, shooting minigame); at 2 lives left the boss shakes, its counter freezes and an eye marks it (redvisor); the last crystal makes it explode: flash, "ALL CRYSTALS FOUND / TIME TO ESCAPE", then the exit teleport.
- Not verified: sounds (ricochet, boss music), the full minigame play-through, the far lamps (only the ones within ~2400 units of the camera are drawn, 4 at most), plasma balls become small sprites in the boss room (the big block holds the lamps).

## v10.13 — boss level fixes
- Lamps removed. Dark stage: BG almost black, the weapon and the boss are not drawn at all (the nightvisor shows the boss and the line). The dark palette is written every frame (a one-time write could miss the VBlank).
- Stage 2: the boss stands at the glyph wall with its BACK sprite to the hero. With the optovisor on and the glyph wall in the centre of the view for 1 s the wall reads "BOSS KEY" and the banner "BOSS KEY DECRYPTED" starts the shooting minigame (bst 3).
- Boss music = your boss_fight.xm (snd/boss_fight.xm), orders 5-17, 8 busiest channels: `python3 tools/mod2it.py snd/boss_fight.xm snd/boss.it 5-17 55000 BOSS 14 all` (the previous WIRED version is snd/boss_wired_prev.it.bak). The module is uploaded to the SPC when the boss fight starts, so about a second of silence before it plays remains.

## v10.14
- The cut-scene bytes share memory with the HUD tour: they are only read once the tour is over (CSP), so "ALL CRYSTALS FOUND" no longer shows after the tutorial.
- Boss module made smaller (orders 5-10, 43105 points, soundbank 95 KB instead of 112 KB): the larger one most likely did not fit the SPC RAM and froze the upload when the boss music started. `python3 tools/mod2it.py snd/boss_fight.xm snd/boss.it 5-10 45000 BOSS 14 all`

## v10.15
- Глаз теперь только указатель «надень optovisor»: у кнопки TR и у кристалла босса. Сама кнопка и кристалл видны только через optovisor.
- Комната босса: в последней волне (осталось 2 жизни) в точке респауна босса появляется кристалл (виден только в optovisor, стрелка указывает на него); босс стоит спиной у стены с иероглифами.
- Взяв кристалл, игрок остаётся только с Redvisor; босс вращается вокруг оси, касание/выстрел в Redvisor добивает его → «ALL CRYSTALS FOUND».

## v10.16 — новый бой с боссом
0. Стрельба → 1. прыжки в темноте → чёрный экран «RUN RUN RUN» → 2. 5 телепортов по 3 с (счётчик 3 SEC; опоздал: −20 HP) →
3. босс у стены с иероглифами (новые спрайты слабого босса), вокруг электрическое поле (−50% HP, отталкивает); босс разворачивается и стреляет, когда игрок атакует.
   Надеть optovisor: полоса прогресса + таймер 30 с, на комнату выходят 6 волн врагов 5 класса; выстрелы в босса откатывают прогресс.
   Прогресс заполнен → игрок гибнет, мини-игра с начала; время вышло → «FIGHT OR DIE».
4. «FIGHT OR DIE»: босс + бойцы 6 класса (3 штуки по очереди — слотов спрайтов только 2), у каждого 20 клеток.
5. Осталось 2 жизни: босс вращается с аурой, остаётся только Redvisor; глаз в точке респауна → кристалл виден в Redvisor → взял → белая вспышка → «ALL CRYSTALS FOUND» → портал.
Кнопка TR: глаз = «надень optovisor», кнопка видна только в optovisor.
Спрайты босса: tools/art_src/new/weak_boss.jpg (front/walk/attack); вид сзади и повороты остались от старого пингвина.

## v10.17
- Weapons no longer squash while firing: the weapon picture is not swapped during a shot (the 4-blank tile upload showed two halves of different frames); muzzle flash is its own sprite.
- Spawn/teleport portals, holes and medkit portals are hidden when a close (Super FX painted) enemy stands in front of them.
- A hole inside the camera view that is too close to the hero is skipped; close spawns only happen out of sight.
- Broken-camera cracks were tried and removed again (they cost too much FPS).
- Fixed overlay OAM clash (OAM_CHG).
- Not done yet: the new boss sequence (bike chase, wire puzzle...).

## v10.18 - boss redesign, stage 1
- The boss has 5 lives. After each of the first 4 deaths it falls (death frames 12-14) and gets up again with a full bar and a medkit for the hero. The 5th time it stays down and three class-6 fighters come one after another (2 slots); when they are dead the old sequence (dark jump, teleport race, glyph wall...) goes on - the new stages (motorcycle, RUN RUN RUN, HACK BOSS KEY, final) are still to come.
- Fall/get-up uses the existing death frames; the prone/push-up storyboard art is not cut yet. Not verified: sound, the full play-through.

## v10.18b
- Boss fall = your storyboard (Stumble, Prone, Push-Up Mid, then standing); lying frame is the real Prone picture. Cut by tools/gen_art.py from tools/art_src/new/boss_story.png.
- Boss back, turning, aim and fire pictures are scaled to the same height as the standing one (the turn to shoot no longer shrinks it).

## v10.19 - boss redesign, stages 2-3 (the chase)
- Chain now: 5 lives with fall/get-up -> 3 class-6 fighters one by one -> white flash, black "RUN RUN RUN" -> the chase -> glyph wall (optovisor) -> FIGHT OR DIE -> spin + crystal. The old dark-jump and teleport-race stages are gone from the chain.
- The chase (bst 10): the map is swapped for a straight 4-cell corridor (given back afterwards), the hero rides automatically (LEFT/RIGHT steer, no weapon, no radiation). The boss rides ahead on a bike (boss frames 10/11 = bike back view from bike_sheet.png), switching lanes and dropping a barrel (class-6 slot frame 10, barrel.png) in its lane; now and then a circular saw (frame 11, drawn by gen_art.saw_img) comes in the other lane. A hit takes 20 (barrel) / 30 (saw) of the life bar only; if the bar empties it refills and the 60 s clock restarts. 60 s survived -> back to the boss room (hero at the plate, boss at the glyph wall).
- Not done yet: teleports + bike arrival cut-scene, handlebar hands, the HACK BOSS KEY wire puzzle, boss vanishing into a teleport in the last fight; the second glyph visit with 3 class-6 fighters. One obstacle at a time (2 enemy slots).


## v10.20
- Enemies keep 120 units from walls (no half-drawn sprites); an enemy already inside the margin can still walk out.
- Enemy weapons by class: 1 quanton (fireball), 2 + plasmagun (green ball), 3 + rocket (red ball, white ring, slow), 4+ + beamchain (instant white line, light damage).
- Class 5 and 6: railgun, one shot per 5 s for all enemies together; a hit gives the camera a crack that a cyan strip washes away from top to bottom (colour-math HDMA, like the death strip).
- Class 6: kozematy once per 20 s: the screen goes white for 2 s (fade after), repeated high beeps (S_RICO) stand in for the ringing.
- Redvisor / TR eye marker: two eyes, the pupils follow the middle of the screen (left / centre / right).
- The attack state lives in tv_buf (free after the HUD tour), so no new RAM.

## v10.21 (boss, part 1; boss stages only compile-tested, not seen in luna)
- Wave 1: every life starts with a 10 s shield (shots ricochet, cyan flicker); while shielded the boss fires 3 plasma balls every 2 s (left of the hero, right of him, at the camera).
- Boss hurt frame = the screaming penguin (frame 9).
- Chain: 5 lives -> 3 class-6 fighters -> boss gets up -> barrel wave (barrel = class-6 slot, homes on the hero's strafing, a jump sends it back at the boss, 3 hits) -> RUN RUN RUN chase -> glyph wall.
- Glyph wall: KEY DECRYPT, 10 cells, 1 per second, x2 while the hero shoots (the boss then calls class-6 fighters), stops and hides when farther than FIELD_R+100 from the boss; full = last fight.
- Last scene: crystal taken -> ALL CRYSTALS FOUND -> camera turns to the boss, it spins, a teleport opens, it vanishes, exit teleport opens.
- Art: explosion frame 27 in spr_big (not used yet). Not done: chase with barrel explosions / boss life bar / asphalt / handlebars, teleport-line arrival animation.

## v10.22
- Beamchain line goes to the crosshair on a hit and past the hero (left/right) on a miss; rail crack vanishes after ~5 s (no cyan wash) and no new crack while one is shown; rail 1/10 s, kozematy 1/40 s, enemy beamchain half as often; wall margin 170 (narrow corridors keep the old 92 rule).
- Spawns: a spawn point in view and close is skipped at most 40 times, then it opens anyway (rooms no longer stall).
- TR: one eye again (two only for the redvisor crystal), the yellow card is gone; text on the wall: NO POWER / POWER ON / GATE OPEN.
- Boss: shield ring of yellow sparks; barrel = horizontal barrel (frames 10/11 of class 6: 0 deg and 45 deg views), explosion = big block frame 27.
- Chase: tunnel-dim palette, no timer; dodged barrel turns, line up (LEFT/RIGHT) and press R to blow it up; 5 hits kill (boss life bar = 5 glyph pairs); then back in the boss room at (4736,1000) with a white strip climbing.
- Glyph wall: KEY DECRYPT text on the wall right of the boss, blue ring of FIELD_R, progress works within FIELD_R+700, boss shoots back while attacked.
- Not done: handlebar sprites (the images are not in the project), asphalt marks, max 3 enemies (MAXE=2).

## v10.23
- TR button: two eyes (like the redvisor mark). The pupils look straight ahead whatever the camera does and slide sideways only while the hero strafes (Y/A) - state in tv_buf[21] (`lk_s`).
- Boss, wave 1: while the boss is up, one helper (quanton or plasma, 6 squares) joins it from a boss-room point every 5-9 s (the second enemy slot), `boss_helper()`.
- Chase: the bike handlebars (your three pictures, tools/gen_hbar.py -> res/spr_hbar.bin, 128x32, left / straight / right by the D-pad) use the weapon's tile slot (4 sprites, OAM 0-3); a dashed yellow centre line of 6 dots rushes toward the bike. Fixed: the dim tunnel palette was recomputed every frame and ate the blank, so VRAM uploads (the handlebars) were dropped.
- New stage between KEY DECRYPT and FIGHT OR DIE: bst 12, the wires. Four wires on the dimmed room with falling 0/1. Current from VOLTAGE (bottom-left) must reach GROUND (top-right). 7 switches (circle: bar = open, "I" = closed), LEFT/RIGHT choose, A/B/X/R flip it and the next switch. Wires have breaks (segments), the live segments show running 0/1. Start state needs 3 flips (e.g. 0, 2, 4). State in tv_buf[23..25].
- Boss music = your industrial_motion_-_smash.s3m (snd/smash.s3m), orders 6-13: `python3 tools/mod2it.py snd/smash.s3m snd/boss.it 6-13 45000 BOSS 14 all` (previous: snd/boss_fightxm_prev.it.bak).
- Debug: DBG_CS now also closes the briefing card; DBG_BST=12 starts the wires.
- v10.23b: fixed pickups vanishing on the whole level (the boss-shield ring code hid the 8 item OAM slots every frame; now only while a ring/line/explosion effect is on).

## v10.24
- New arena room 9 = the big hall after the yellow door (maps/stage1.json rooms: [4,14,21,20,2]): entering it locks the doors until the fights are cleared.
- Room fights are continuous: no pause between waves, enemies come from 5 fixed open cells of the room (`room_pt`) in the hero's sector (the stall came from spawn points lying in an inactive sector).
- A close spawn is shown at once as the painted (Super FX) form, no ground clipping while it materialises.
- TR button and its eyes belong to the REDVISOR now (NO POWER / POWER ON / GATE OPEN text, the trigger); the wall text still powers the gate through the optovisor. The eyes (TR and last crystal) look along the walls: pupils follow the side of the axis the hero stands on (camera-relative), straight within +-32 units (`eye_glyph`).
- The stuck "glyphs" were the crack's 4 sprites drawn before the big block's tiles were really in VRAM: all big-block sprites wait for `big_rep == 0` (`BIG_OK`).
- Crack lives 2 s. rt_ and wv_t are u8 (RAM).

## v10.25 — boss level fixes
- Wave 1: only 4 helpers per boss life (class 5, 6, 5, 6), one at a time; class 6 is visible while they fight. Each hit on the shield takes 1/10 of it (10 hits and the boss is vulnerable); it still runs out after 10 s.
- Wave 2: barrels only fly with the Nightvisor on ("WEAR NIGHTVISOR"); without it the boss is invisible and shoots the hero.
- Wave 3 (chase): the barrel moved at 7 while the bike at 10, so after a dodge it never got ahead again. Now the bike is slower (6), the barrel spawns 2400 ahead, rolls at the hero, and after a dodge catches up (9) so it can be shot. Hit detection is swept (no skipping at big steps).
- Nightvisor: door and gate frames glow (texture class 29 -> reserved palette index 11, painted only by the night palette).
- Untested in the emulator: wave 1 helper flow, wave 2 with/without Nightvisor in the real boss room, a full chase run with shooting.

### v10.25b — chase
- Barrel cycle: it rolls from the boss at the hero (dodge by lane), turns after the hero, rolls back to the boss and sits on its axis for 4 s; shoot it (R) while lined up with the boss's lane. Not shot: it vanishes and the boss throws a new one. The boss keeps its lane while it holds a barrel.
- Smoothness: the bike moves every frame (`bike_advance` in `update_player`), the world logic only runs every second frame.
- Door outlines: dark seam in normal palettes, glow only in the night vision.

### v10.25c — fix
- Reverted the door-outline-in-texture trick: it stole palette index 11 (the brightest shade) that the enemy sprites painted by the Super FX also use, so all close enemies and the boss lost their highlights and looked smeared. Palette and LUT are as before v10.25.
- Night vision door marks are now crisp OBJ `[ ]` brackets (like the enemy target brackets) on the two nearest doors/gates, drawn in the 4 spare tele-oval slots (not shown while an enemy is teleporting in).

## v10.26
- Wires minigame redone to the user's sketch: 4 thick wires (red = VOLTAGE on top, blue, yellow, black = GROUND), 2 round switches on each (knob up = 1, knob left = 0), random start positions, d-pad chooses a switch (the chosen one has a green face and a yellow ring), R (or A/B/X) flips it. A white current runs from the left end of the red wire; a switch that is up passes it on, one that is left drops it to the next wire; GROUND is the right end of the black wire. A dead end sends the current back along the circuit; any flip restarts it. Dark green room, falling green 0/1 sprites. Art: `tools/gen_wires.py` -> `res/spr_wire.bin` (loaded into the weapon tile slot like the handlebars) + `res/wire_pal.h` (the sprite palettes 1, 3, 4..7 are replaced while it runs and put back at the end).
- Sprite limit lesson: small OBJ = 16x16 (2 tiles per line), large = 32x32; a line holds 34 tiles and the LOWEST sprites are dropped first, so the minigame uses 5 slots of 32 px and keeps the rain at the lowest indices.
- Wave 2 banner "JUMP JUMP JUMP" is back; "WEAR NIGHTVISOR" is repeated every ~3 s while it is off.
- Chase: visors forced off; the boss is shown from behind (frame 11) instead of flickering front/back; a shot barrel explodes ON the boss's bike (the explosion follows it) and the boss "turns round" (front/back views flash for ~1 s). A real 360 degree spin needs 8 more boss frames (the sprite tables have a fixed 20 frames for all types).

## v10.27
- Black cards with a big title and a small "Note:" line (tools/gen_notes.py -> res/spr_note.bin; the note is a strip of 16x16 sprites in the weapon tile slot): JUMP UP ON A CLOUD (wave 2), CATCH UP OR GET OUT (chase), WAIT NOT TO LATE (before Key Decrypt), TURN ON THE LIGHT (before the wires), GET AWAY FROM THE NIGHT (last fight).
- Barrel wave: blinking up-arrows on both sides while a barrel comes.
- Chase: the boss sweeps across the road and throws a barrel each time it crosses the hero's axis; barrel faster; after a dodge it flies back on the same vertical line to the boss's horizontal axis (no lock on the boss); hits the boss -> gone; otherwise lies 10 s, a shot blows it up, the boss takes a hit and spins 5 s border to border. Handlebars are uploaded in one blank (no flicker).
- Last fight repeats wave 1 (shield 10 hits + helpers 5/6/5/6). After the redvisor crystal: camera turns, the Exit teleport lights, the boss runs into it, the camera stays on the teleport, control returns.
- Redvisor eyes swapped back; chaingun was silent because SNESMOD has only 16 effect slots (18 samples were loaded): samples merged, chaingun louder, new footstep sample (S_STEP).
- Class 5/6 may blink to a random respawn point when hit; all weapons reload in 2 s; Nightvisor door marks need line of sight.

## v10.28
- В бою (враги живы, уровень / 1-я волна босса / последний бой) все визоры выключаются и блокируются ("NO VISORS IN COMBAT"); цикл дверей ночного визора отфильтрован по расстоянию (причина падения FPS не профилировалась — вероятно проекция/LOS всех 20 дверей каждый кадр).
- Брифинг: 2 страницы (←/→), R — выход ("PRESS R FOR EXIT MENU"); на 2-й странице "ALL VISORS ARE UNAVAILABLE IN COMBAT TO SAVE THE POWER".
- Счётчик ENM:kk/tt (справа внизу) только в боях (комнаты, 1-я волна босса); показывается и убирается в туториале.
- Верх HUD: REP:n (full repair) и MDK:n (аптечки) вместо *R / *L (BG-текст; в туториале мигают).
- Чёрные карточки боёв/миниигр ждут R ("PRESS R FOR EXIT"); исправлена порча 2-й строки заметок (повторная загрузка тайлов).
- Мотоциклы: удар бочки — трещина на 3 с, без взрыва; сбитая бочка исчезает, короткий взрыв, затем вращение босса (6 видов босса на мотоцикле, <2 с); бочка вдвое быстрее; после гонки нет прокрутки; у стены с иероглифами босс — спрайт на земле.

## v10.29
- Chaingun: сэмпл с более плотным телом/хвостом (был тише остального оружия); шаги: новый звук (щелчок+удар) и шаг по пройденному расстоянию, а не по фазе покачивания (раньше звучали слишком часто/слитно). Банк звуков почти на пределе SPC-памяти (+116 Б к v10.28; +586 Б уже вешало игру) — сэмплы больше не удлинять.
- Комната погони: свой текстуры пола (слот 3 из 4 в GPRAM, переключается через V_TEXF при bst==10): асфальт, янтарные разделители полос (текстура повторяется каждые 256 ед. → 3 разделителя), следы шин; спрайтовая пунктирная линия убрана.
- Ракеты врагов: скорость 16→46; промах пролетает мимо и взрывается о стену (спрайт взрыва, fx kind 2), попадание — короткая красная вспышка (hurt_t) как раньше.
- v10.29a: REP/MDK скрываются, пока на экране верхний баннер (раньше текст накладывался).

## v10.30
- ENM в финальной битве: босс в состоянии E_TELE (телепорт) раньше "терял" счётчик — теперь считается всегда, пока босс жив.
- Nightvisor выключается и изымается у игрока после миниигры с бочками (враги-«невидимки» type 2 после этого видны всегда — флаг nv_gone).
- Мотоциклы: визоры заблокированы (VISORS BLOCKED), палитра всегда «тусклая» (красной вспышки/зелёного фона нет).
- Иероглифы: optovisor выключается и изымается в момент старта прогрессбара; бар идёт сам, атаки по боссу его ускоряют (+40 за попадание, раньше откатывали); бойцы class 6 видны без optovisor.
- Чёрные карточки: на время карточки сбрасываются белая засветка (kozematy), трещина, вспышки; Note грузится за один blank (8 рядов), без «ползущей» прорисовки.
- Последний бой: кристалл и глаза у стены с иероглифами (crystal_pos), а не под боссом.
- Меню визоров: "[VISORS BLOCKED]" вместо NO VISORS в бою/на мотоцикле, баннер VISORS BLOCKED.
- v10.30a: шаги чаще (бег): один шаг на 430 ед. пути вместо 700 (≈5 шагов/с).
- v10.30b: счётчик ENM первой волны/финала накопительный: убито/20 (4 помощника × 5 жизней), не сбрасывается между смертями босса и не прыгает 1/4→0/4; в бою с 3 бойцами — ENM x/3.

## v10.30c
- Bike room: the dim palette is now written with shifts only (the divide loop of the old routine outlasted vblank, so the night-vision green stayed in CGRAM).
- ENM counter: counts only the boss's helpers (wave 1 / final fight) and the 3 fighters; room and "ghost" kills no longer count; hidden after the 5th fall. Ghost spawns from the look-room no longer join boss fights (the "extra class-6 enemy").
- Untested by hand: ENM counter in a full real playthrough.

## v10.30d
- Wave 1: the boss cannot be hurt (ricochet, "KILL THE HELPERS") until all 4 helpers of the life (5,6,5,6) have come and are dead; so the 20-count is real and no helper is left over for the 3-fighters phase.
- Bike: the second moving "----" mark (the boss's sweep target) is no longer drawn; only the 5-barrel life bar stays.
- Glyph wall: the boss no longer shoots; when attacked it turns and rolls a barrel at the hero (a jump or a miss ends it, it does not return). KEY DECRYPT grows only while the hero hits the boss (1 cell/s as in the old idle mode, no speed-up), and stalls while a summoned fighter lives. Debug: -DDBG_HITB.
- Untested by hand: all of the above in real play (checked only: barrel hits the hero, bar grows and stalls with a fighter alive).

## v10.30e
- ENM in the boss fights shows the helpers of the CURRENT life (k/4), not a summed k/20.

## v10.30f
- Bike: the boss now shows a side view of the bike (facing its way across the road) from the start; the pure rear view looked like a standing figure. The spin views are unchanged.
- Bike: the second bar (the aim life bar over the boss) is hidden; only the 5-barrel bar stays. Debug: -DDBG_SPIN.
- Untested: which side view faces which way (may be mirrored).

## v10.30g
- Wave 1 logic: 3 class-5 helpers come during the whole boss fight (one at a time, independent of how many times the boss falls; ENM k/3); the boss is hurt normally again (no invulnerability). When the boss's lives are over and it lies unconscious, 3 class-6 fighters come (ENM k/3); killing them ends wave 1. Leftover helpers are removed when the fighters phase starts. The final fight keeps its 4 helpers (5, 6, 5, 6).
- Untested in real play.

## v10.30h
- Bike: back to the rear view of the bike while the boss rides (the side views are only used in the spin after a barrel hit). The v10.30f side-view idea is dropped.

## v10.30i
- Glyph wall: the barrel now starts 170 units in front of the boss (toward the hero) instead of inside its picture, where it merged with the boss into one odd shape.

## v10.30j
- Glyph wall (unwinnable bug): the summon cooldown ran down while a fighter was alive, so after killing one the next came the moment the boss was hit again; the bar never got a fighter-free moment to grow. Now the cooldown (5 s) restarts while a fighter lives and runs only after it dies, so the bar always gets time to fill. Debug flags: -DDBG_HITB, -DDBG_KILLF.

## v10.30k
- The fallen boss (lying between lives / while the fighters come) is no longer auto-aimed and shots pass through it, so the cursor stays on the live helper.
- The Super FX close-up frames of the boss (res/gspr_e0_*.bin, gspr.h) were stale (generated before the bike frames existed): regenerated with tools/gen_gspr.py. NB: `make clean` is needed after regenerating, the .bin files are not tracked as dependencies.
- The painted picture in the Game Pak RAM is reloaded when the bike room / the glyph wall start and once a second afterwards, so a stale picture (a bike sprite at the glyph wall, a wrong sprite at the start of the chase) cannot stay.
- Untested in real play.

## v10.30l
- CHEAT (debug): RIGHT RIGHT LEFT LEFT R L skips the boss minigame that is on (waves -> fighters -> barrels -> bike -> teleport race -> glyph wall -> wires). Not in the last fight / red crystal / final scene.
- Glyph wall redesigned: the optovisor (taken away at once) starts KEY DECRYPT, 1 cell per second; every 2 cells a class 6 fighter comes (ENM 0/1), the visors are blocked (VISORS BLOCKED) until it is dead, the boss turns to the hero and rolls barrels meanwhile (they do not come back); hits on the boss go to the fighter (one shared life); when the fighter is dead the boss turns its back, the optovisor is given back (WEAR OPTOVISOR) and the decryption goes on. Before the optovisor, shots at the boss do nothing: it turns, rolls a barrel, stands with its back again. 5 stages of 2 cells = full bar -> wires.
- Sprites: the boss's picture in VRAM is uploaded again every 16 frames at the glyph wall / teleport race / bike (every 64 frames for the others): a DMA that missed the blank left a half-painted sprite (flat bottom, "ground sprite") until the next frame change.
- Untested by hand: the whole glyph wall flow in real play, the cheat in every stage (checked: glyph wall -> wires).

## v10.30m
- Cut-scene before the chase (bst 9, phases in bw_y): after the 5th barrel the boss opens a teleport and runs into it, the hero (pad dead) runs after it, white flash, the chase road: the boss on foot walks to its bike (a prop, still drawn with the rider - the rider-less bike art is not made yet), sits down and rides off, the hero turns his head to the right, another bike appears there (teleport ring), then the black card "CATCH UP OR GET OUT" and the chase.
- After the 5th barrel hit in the chase the picture goes dark (eyelid fade), the hero rides in to the glyph wall (bst 13: boss with its back to the hero), then the black card "WAIT NOT TO LATE".
- Glyph wall: a "KEY" label (monitor) stands on the wall behind the boss; no life bar over the boss. Optovisor on: the bar fills 1 cell per second (12 cells = 3 rounds); after every 4 cells the boss switches the optovisor off and sends a class 6 fighter (ENM 0/1, visors blocked), rolling a barrel about every 3 s. Fighter dead: the boss turns its back again, the optovisor comes back. After the 3rd fighter the boss runs off to the left and the monitor shows 0101, then the black card "TURN ON THE LIGHT" and the wires.
- Wires solved: the screen dims (eyelids close), the room is lit when they open, then the black card "GET AWAY FROM THE NIGHT" and the last fight.
- Minimap: enemies are red cells, the boss a blue cell (BG2 tiles 17/18 are repainted every time something moves; colours 4 and 8 of the map palette).
- Debug: the cheat now also skips the cut-scene (bst 9) and the arrival (bst 13); flags DBG_FIN (glyph wall: bar done), DBG_KILLF=N (kills the fighter of the glyph wall every 150 frames after frame N); DBG_BOSS now spawns the boss correctly (argument order fixed). The cheat code and the eye flag live in tv_buf and are cleared when the HUD tour ends.

## v10.30n - parked bikes (artist's sprites)
- The two parked bikes of the pre-chase cut-scene now use the rider-less bike pictures drawn by the artist (tools/gen_prop.py builds res/spr_prop.bin + prop_pal.h from the sheet: 3 sizes of 64x64, 3 of 32x32, 4 of 16x16, own palette).
- They are props (type 0, hid 7) and exist only in the cut-scene; they are removed before the bike mini-game. They never use the Super FX close-up path or the wall-palette fade.

## v10.30o - fixes after play-testing
- Top HUD (hearts, C n/9): its sprites are written again after every black card / stage change (they stayed blank for a while at the start of the bike chase).
- Bike handlebars: palette and tiles are refreshed every 16/32 frames, so a stale palette or half-written strip can not stay.
- Camera: the auto-turn of the last teleport scene (bst 11) has a wider dead zone (no hunting left-right); the screen shake now fades in the cut-scene/minigame stages that return early.
- DBG_B10END=N: debug hook that ends the bike stage after frame N.

## v10.30p - HUD tour, stripes, class 5/6, cards, last fight
- HUD tour: no white frame any more; the part that is explained blinks (hearts, crystals, REP, MDK) like the revive / medkit items. New steps: SELECTED WEAPON (the weapon name blinks), AMMO, RELOAD BAR (a demo bar is shown), ENEMIES. 8 steps of 2.5 s.
- The two stripes round the black top bar were the backdrop colour (colour 0 of the lit room palette) showing in the 16 lines above the view: BG2 rows 29 and 31 now paint them black (row 31 only in its upper 4 lines, tile 80).
- Class 5 and 6 no longer shoot on their own: a class 5 fires its burst of 3, a class 6 fires the railgun beam, once for every 2 cells of its own life that are lost.
- Black cards: "PRESS R FOR EXIT" removed (R still leaves), the note sits 20 lines lower.
- Last fight (bst 4) redone: the boss has a shield of 5 charges (ENM:n/5 counts them); it rolls barrels; jump over one and it rolls back - shoot it when it is near the boss (< 1000 units) and a charge is gone; if nobody shoots it the boss takes nothing. With the shield down the boss repeats: 2 railgun shots, 4 rockets, a plasma volley, a run at the hero to hit; the hero shoots it down. At 2 cells it spins (bst 5) with the redvisor ON, banner FIND CRYSTAL HERE and a TIME:n counter of 5 s. When the time is up the life and the shield are back, a class 5 fighter joins (no barrels until it is dead) and the fight starts again. Cheat (RIGHT RIGHT LEFT LEFT R L) in the fight: shield off and 3 cells left.
- Debug flags: DBG_TM (BG layer mask), DBG_F4 (auto jump/shoot in the last fight), DBG_GOD.

## v10.30q
- Cut-scene before the bikes: the second prop bike reused a barrel's VRAM slot and showed the barrel (stale `shown_f/shown_v`); both props now force a re-upload.
- Last fight: a spent barrel (hit the boss's side, hurt the hero, blocked) stayed frozen and blocked the next one; it is now removed before the next throw.
- Arrival at the glyph wall: 5 s of the boss at the terminal with "KEY ENCRYPT" on top, then the black card.
- Debug: `DBG_B10END=N` now calls bike_exit() directly.

## v10.30r
- HUD tour: the REPAIR / MEDKIT titles no longer show a coloured artefact (the ':' glyph of the tour text was drawn as a stray sprite; the titles are now "REPAIR REP 2" / "MEDKIT MDK 5"); the eyes are not drawn during the tour.
- Visor menu: no N / O / R panel in the middle of the screen (the strip at the bottom already names the visor and shows ON / OFF); R (on or off) returns to the weapons at once.
- Glyph wall arrival (after the bikes): the boss rides up on its bike from the left (starts 480 units left of the wall, comes 80 closer, ~2 s), puts the bike down in front of itself (rider-less prop), runs to the wall as a side view and turns its back; the hero's camera follows it all the time; then 5 s at the terminal with "KEY ENCRYPT" on top, then the black card.
- Last fight: the redvisor is NOT switched on for the hero any more; the "find the crystal" pause is 20 s (TIME:nn).
- The eye of the boss room (waves, barrels, last fight; only on foot): an eye stands on one of 8 spots (never near the boss / the crystal) for 10 s, then jumps. Put the redvisor on while it is there: a medkit appears on that spot for 10 s (seen only through the redvisor and only within 640 units, picked up within 300). Take the redvisor off: the medkit moves to the eye's current spot. The redvisor is allowed in fights only while this eye is up. State lives in tv_buf[62], [24], [25], [29] (wires minigame slots, cleared by wire_init).
- Debug: DBG_EYE writes the eye state to tv_buf[37].

## Map editor: textures and open levels

`python3 tools/mapedit/server.py` -> http://localhost:8766/ (needs Pillow; `MAPEDIT_MAPS=<dir>` points it at another maps folder).

- Texture library: pictures live in `maps/textures/<category>/` (sky, space, scifi, ship, stone, acid, lava, water, sand, misc). "Upload ZIP" keeps the zip's folders as categories; single images go to the chosen category.
- Pickers (by category + search) for wall ids 1-9, floor, ceiling and sky.
- Open level: checkbox, sky texture or colour (ceiling picker is hidden).
- Floor set (up to 8 textures) and the "Floor paint" tool (shift = rectangle, right click = erase): per-tile floors for lava, water, acid, sand, stone paths. Stored as `floor_set` and `floor_tiles` (H x W, index+1, 0 = level floor).
- NOT implemented in the game yet: `gen_level.py` and the engine ignore `open`, `sky_*`, `floor_set`, `floor_tiles` and the new `wall_tex` paths; the editor only saves them.

Included library: 65 tiles (32x32) cut from the Gemini texture sheets, in `maps/textures/<category>/` (sky, space, scifi, ship, stone, water, lava, acid, sand, misc).

### 3D preview (`tools/mapedit/play3d.py`)
`pip install pygame numpy pillow`, then `python3 tools/mapedit/play3d.py [stage]` (or `--file map.json`), or the "Play 3D" button in the editor (opens a window on the same computer; pressing it again updates the open window). WASD/arrows + mouse, Shift run, E door, Tab noclip, M minimap, F fog, PgUp/PgDn stage. The view reloads when the map file changes. It shows walls/doors/floors/sky/open levels/markers from the JSON; it is not the game's renderer.

## v10.31 - build from any stage file
- `STAGE_JSON=maps/stage2.json bash tools/build_all.sh` builds the ROM from another map (gen_art, gen_level, gen_screens, stage_music all honour `STAGE_JSON`; default maps/stage1.json). The shipped space_fps.sfc is built from maps/stage2.json (editor level, custom wall/floor/ceiling textures).
- stage2 had open tiles on the right map border (x=31, y=57..62): rays would leave the map, so they were closed with walls; the editor's validator now reports open border tiles as an error.
- stage2 has 51 spawns / 32 items / 37 doors; the C RAM band fell to 292 B, so spawn_pos and spawn_room are read from ROM again (they are only used a few times per update): 547 B free.
- Not in the game yet: open levels/sky, per-tile floors (floor_set/floor_tiles), 32x32 stage tiles other than the 9 wall slots.

## v10.32 - special floors (editor + game, first part)
Editor: left palette "Special floor" (shift = rectangle, right click = erase): checkpoint, lava floor, acid floor, water electro, water (safe), dark hole, radiation zone, hidden wall, and "teleport pair" (click the IN tile, then the OUT tile; up to 4 pairs). Triggers are items: waterbox (hydrant: lava -> water), electrobox (electro water -> water), hiddenbox (hidden walls off). The 3D preview (`play3d.py`) draws all of them animated. Map JSON fields: `floor_kind` (H x W), `teleports`, items kinds 18/19/20. The validator checks placement and warns about hazards without their trigger. Demo level: maps/stage_demo.json (corridor south of the yellow door of stage2).
Game (main.c `floor_update`, tables in res/level.h from gen_level.py: `fk_map`, `cp_xy`, `tp_xy`):
- Checkpoints: the start tile is checkpoint 0, the others in map scan order; stepping on one makes it the respawn point ("CHECKPOINT"). Total death (no resurrections left) now respawns at the last checkpoint instead of the start.
- Lava, electro water and dark hole kill at once and respawn at the last checkpoint WITHOUT using a resurrection. A jump over lava/electro (pz > 12) is safe. Triggers: waterbox / electrobox switch them off (the floor is harmless afterwards).
- Acid: -3 power cells on entering and every second while standing in it (a jump over it is safe).
- Dark hole: on the ground the hero sinks (camera goes down); 90 sink points (~1.5 s standing, ~0.7 s running) = death. In the air nothing happens, jumping again on the hole keeps you up; leaving the hole lifts the camera back.
- Radiation zone: -1 power cell per second, off while the super bottle works (30 s). A level with radiation zones switches the old whole-level radiation off (FK_RAD).
- Hidden wall: solid for the hero and the enemies (map_w value 15) but not drawn (the GSU map stays empty there); the hiddenbox trigger removes them. A GSU map resync keeps them invisible.
- Teleport: stepping on an IN tile moves the hero to its OUT tile (flash, sound); locked until he leaves the tiles.
- Warnings: "LAVA / ELECTRO WATER / ACID / RADIATION AHEAD", "DARK HOLE: JUMP" one or two tiles ahead.
NOT done yet: the GSU floor pass still draws one floor texture, so the special floors are NOT visible in the game (a per-tile floor needs the integer tile position, which the floor loop drops: its fx/fy wrap every tile); the trigger items use the placeholder crate sprite, not your hydrant/electrobox/flask pictures; the transvisor; the quest editor.
Build: `STAGE_JSON=maps/stage_demo.json bash tools/build_all.sh`. RAM: C band 520 B free (limit 512).

## v10.33 - enemy room waves (editor + game)
- Editor, panel "Enemy room waves": per arena room a "waves" checkbox (off = the room does not lock, its enemies are plain spawn points) and a multiplier (1..8): every enemy point in the room sends `multiplier` clones. Total = enemies x multiplier, sent in waves of 3 (8 = 3+3+2). `spawn_mult` = clones per spawn point outside the rooms.
- Old maps without the fields keep the old behaviour (rooms 15 / 9 enemies, 4 per outside point). stage2.json / stage_demo.json use multiplier 1 and spawn_mult 1.
- gen_level emits `spawn_cnt[]` / `room_tot[]`; the kill counter at the bottom right uses `room_tot`.

## v10.34 - pause, level brightness / palette
- START pauses the game (banner "PAUSE - PRESS START"); not during cards, cut-scenes, death.
- Editor "Level look": brightness 40..200 % and palette (hue) -180..180 deg per level (`brightness`, `hue` in the map JSON). tools/tint.py (run by build_all.sh after the generators) rewrites bg_pal, obj_pal (sprites, enemies), boss_pal, prop_pal, so walls, floor, enemies and boss follow the level colours. HUD bar/menu palettes (hbar_pal, scr_pal) stay untouched. The editor preview (CSS filter) and play3d are approximate.

## v10.35 - hazard zones with indexes
- Connected blocks (4-neighbours) of lava / electro water / hidden wall tiles are zones, numbered per kind in scan order (1, 2, ... max 8 per kind). The editor shows the numbers and outlines a zone when the mouse is over it.
- Triggers (hydrant 18, electrobox 19, hiddenbox 20) carry a target zone (4th item field; 0 / missing = every zone of the kind). The editor field "New trigger: target zone" sets it when the trigger is placed.
- fk_map now holds `kind | zone << 4`; `item_zone[]` holds the trigger targets; the game keeps one bit mask per kind (`fk_om`). Tested: zone 2 switched off, zone 1 still kills.

## v10.36 - cheat, ports, stage2 update
- Cheat UP UP DOWN DOWN L L wins the level at once (next stage screen). The old cheats (right right left left R L: boss skip; left left right right L R: go to boss) stay.
- Map editor port is 8766. stage2.json replaced by the new map (brightness 168, hue -156, 10 rooms, 3 teleports, zones). gen_level counts teleports as connections for the reachability check and only warns about an empty closed pocket (an item or enemy in a pocket is still an error).
- One ROM holds one stage (2 MB is full): `STAGE_JSON=maps/stage2.json bash tools/build_all.sh` for stage2, `bash tools/build_all.sh` for stage1.
- item_pos / spawn_cls are read from ROM again (551 B of C RAM free on the 51-spawn level).

## v11 plan - 8bpp view (after the Doom II SNES source, GPL-3.0: ideas only, no code or art taken)
Doom/FX: Mode 3 (BG1 256 colours, BG2 16), GSU bitmap 160 px wide, 8bpp, 21 MHz, one 256-colour palette in which the 16-colour art (weapons, faces, font) lives in groups of 16.
Ours today: Mode 1, 256x128 4bpp bitmap (128 columns x 2 px), 32 texel classes -> LUT -> 16 colours. Budget for 8bpp 160x128: 20 KB of Game Pak RAM (64 KB, tables use $0200-$7D00, enemy pictures at $C000), 20 KB of VRAM (BG1 chr), DMA +25 %.
Work list: (1) SCMR 8bpp/H160 and 160 columns in gsu_w / gsu_fc / gsu_s; (2) texel byte = palette index (ramp*8+level), LUT (index x distance x 2 light directions) = colour index, 128 codes -> LUT 4 KB, relocate tables; (3) gen_art/gen_tables: per-stage palette fitted to the real textures (k-means into ramps), real colours instead of classes; (4) Mode 3 (BG3 is unused), BG2 HUD keeps 16 colours, OBJ palettes live in CGRAM 128-255 so the view gets 0-127; (5) VRAM re-layout (BG1 chr 0x2800 words); (6) enemies by GSU scaling, drop the pre-scaled sizes; (7) more levels in the freed ROM.
Milestone 1 (spike): walls only, 160x128 8bpp, real texture colours, measure frame time. Nothing of this is built yet.

## v11.1 — wall colour groups (a hue per wall)
- The source wall textures are almost grey, so one 16-colour view palette made every wall the same colour. 8 bpp turned out too slow (spike `space_fps_v11`: walls only, 160x128 = ~2.6 frames per frame), so the view stays 4 bpp.
- Every wall id belongs to a colour group (`WALL_GRP` in tools/gen_tables.py: 0 = the old blue-grey, 1 rust, 2 green, 3 steel blue, 4 khaki). Groups 1..4 are BG1 palettes 4..7 (CGRAM 64..127), built with the same 9 variants (flash, hurt, night ...) as palette 0 (`bg_palg`).
- gsu_w stores the wall id of every column at GP RAM WIDB ($7D00, 128 bytes); the program is 511 of 512 bytes (the column loop test got shorter to make room).
- `pmap_calc()` picks the group of every 8x8 tile column (majority of its 4 wall columns); `gsuDmaFullFrameM` (gsu_loader.asm, a copy of the lib's gsuDmaFullFrame) writes the 16 rows of map high bytes (`hb[32]`, 16 small DMAs on channel 5, the source address is reset every row) just before the 16 KB picture, in the same blank band.
- Only for the normal palette variants (`pal_cur < 9`); the special palette states (title fade, tunnel dim, rail ...) use palette 0 only.
- stage.json: `"wall_group": {"3": 2, ...}` (wall id -> group 0..4), `"group_tint": {"1": [1.5, 0.9, 0.55], ...}`.
- Known: floor and ceiling take the colour of the wall column above them; seams between walls are 8 px steps.

## v11.2 — wall groups from the real textures
- gen_art.py writes res/wall_curves.json: for each wall id the colour of the actual texture at 11 brightness steps (chroma ratio to luma). gen_tables.py clusters the 14 curves (k-means) into the 4 wall groups (BG1 palettes 4..7); a group's palette = the mean curve of its walls, chroma gain 2.5. stage.json `wall_group` / `group_tint` still override.
- Uploaded wall textures (`wall_tex`) therefore bring their own colours.
- Limit: one 16-colour palette per 8x8 tile, brightness steps only: rust + grey + teal inside ONE texture become one colour curve. Full texture colour needs 8 bpp.

## v11.3 — 8 bpp view (Doom-style textures), step 2 of the port
- The view is now a 160 x 104 bitmap in **8 bpp** (BG mode 3: BG1 = the Super FX bitmap, BG2 = the HUD), shown centred (screen x 48..207, bitmap row 0 on line VIEW_Y+12 so the old horizon line is kept).
- 112 view colours = 10 colour ramps taken from the real wall/floor textures (k-means) + 4 accent ramps (amber / green / red / cyan lamps, they keep glowing in the dark) x 8 light levels. A wall texel is a ramp+level code, the LUT turns (side, distance light) into a CGRAM slot, so one texture can have many colours.
- CGRAM slots: the 112 colours live in the slots HUD (16..20, 24, 32..35, 48..51) and sprites (128..255) do not use (`vruns` in palette.h); `cg_view()` uploads them in 5 DMA runs.
- Palette effects (hurt, flash, death, alarm, super attack, night/opto/red visor, rail, white-out, grey tour, matrix, bike dim) work on the 112 colours (`view_fx`); the eyelid blink uses the screen brightness.
- Focal length stays 128 px (the sprite projection is unchanged), so the field of view is 64 degrees instead of 90; pitch is limited to +-11 rows (the floor/ceiling row table has 64 rows).
- GSU: gsu_fc floor/ceiling spans 4 px at a time, gsu_w 2 px columns; `gsuDmaFrame8` copies the 16.6 KB frame in the letterbox window.
- Temporary: enemies are sprites only (the Super FX enemy painter is still 4 bpp and is switched off, GM_ON 30000); the HUD still sits where it was (the bottom 8 rows of the view are covered by the black HUD strip).
- Speed: about 15 fps in a quiet room (measured: rendered frames per 600 video frames), step 3 is to overlap the two Super FX jobs and the blank-time work.

## v13 — step 3a: 224 x 104 view (PAL), NOT YET BUILT OR RUN
Changes (made without a toolchain, check the build first):
- View 224 x 104, 112 wall columns of 2 px (was 160 x 104 / 80): `NC` in tools/gen_tables.py (ANGO/COFF/ZBUF cover 112 columns, FOV 82 degrees at focal 128), `gsu_w.sfx` loop count 112, `gsu_fc.sfx` 56 spans of 4 px per row and the left-edge offsets 56 a (was 40 a; the four blocks gained one byte each, check the 512 byte cache limit).
- VRAM: BG1 columns now 13 tiles apart (28 x 13 = 364 tiles, 0x2D80 words, BLANK8 = 364); map entry c*13+r; `gsuDmaFrame8` steps 416 words per column, 28 columns, 23 296 bytes.
- DMA window: the last start line is top + 159 = PAL (312 lines). On NTSC the frame does not fit the blank time: use PAL only.
- main.c: VIEW_XS 16, VIEW_W / VIEW_COLS, ZBUF copy 224 bytes, column clamp, weapon-lowering and wall-ahead probes at the new centre column 56.
- The Super FX enemy painter stays off (GM_ON very large): its pictures at $C000 would overwrite frame columns 16..27 (the frame now ends at $DFFF).
- Next (3b): hybrid columns, 1 px for walls closer than ~2.5 tiles. gsu_w.sfx is 511 of 512 bytes, so it needs a second GSU job for the near columns.
- v13 fixes: `oamHideRange` / `nmiSetLag` defined in main.c (the stock OpenSNES lacks them: build error); `room_pt()` falls back to wall margins 92 and 40 (stage2 rooms 0, 2, 3, 5, 8 have no cell with margin 170, so their fights never started and the doors stayed locked: the corridor north of the start is room 8).
- v13b: walls are plotted row by row (the v10.5 row-pair trick made every wall pixel 2x2: 112 x 52 effective resolution); `ROM_REGION := 2` (PAL header) in the Makefile; close enemies keep their own OBJ palette (the blend toward the BG colours was built for the 4 bpp view and painted them as one dark colour).
- A screenshot of v13 showed the right ~25% of the view black: only ~21 of 28 columns reached VRAM. 21 columns = 17.5 KB = exactly the NTSC blank time, so that run was NTSC (emulator region / header). The view needs PAL.
# Vectron-Space
