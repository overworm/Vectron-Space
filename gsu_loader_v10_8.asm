;==============================================================================
; gsu_loader.asm - GSU program, Game Pak RAM image, sprite data, copy routine
;==============================================================================
.ifdef SUPERFX

ASSET_SECTION "gsu_fc_code"
gsu_fc:
    .incbin "gsu_fc.sfx.bin"
    .REPEAT 16
    .db $01                 ; NOP: the cache is filled in whole 16-byte lines
    .ENDR
gsu_fc_end:
.ENDS

ASSET_SECTION "gsu_w_code"
gsu_w:
    .incbin "gsu_w.sfx.bin"
    .REPEAT 16
    .db $01
    .ENDR
gsu_w_end:
.ENDS

; block-move loaders: the program goes ROM -> GSU cache with MVN (7 cycles a byte; the library's gsuCacheLoad loop needs about 30)
.SECTION "gsu_fastload" SUPERFREE
gsuLoadFc:
    php
    phb
    rep #$30
    sep #$20
    lda #$00
    sta.l $003030
    sta.l $003031
    rep #$30
    ldx #(gsu_fc & $FFFF)
    ldy #$3100
    lda #(gsu_fc_end - gsu_fc) & $FFF0
    dec a
    mvn :gsu_fc, $00
    plb
    plp
    rtl

gsuLoadW:
    php
    phb
    rep #$30
    sep #$20
    lda #$00
    sta.l $003030
    sta.l $003031
    rep #$30
    ldx #(gsu_w & $FFFF)
    ldy #$3100
    lda #(gsu_w_end - gsu_w) & $FFF0
    dec a
    mvn :gsu_w, $00
    plb
    plp
    rtl
.ENDS

; tables, textures and look-up data for the GSU (tools/gen_tables.py)
ASSET_SECTION "gpram_image"
gpram_image:
    .incbin "res/gpram.bin"
gpram_image_end:
.ENDS

; sprite and background art (tools/gen_sprites.py)
ASSET_SECTION "spr_e0_n0_data"
spr_e0_n0:
    .incbin "res/spr_e0_n0.bin"
.ENDS

ASSET_SECTION "spr_e0_n1_data"
spr_e0_n1:
    .incbin "res/spr_e0_n1.bin"
.ENDS

ASSET_SECTION "spr_e0_n2_data"
spr_e0_n2:
    .incbin "res/spr_e0_n2.bin"
.ENDS

ASSET_SECTION "spr_e0_nb_data"
spr_e0_nb:
    .incbin "res/spr_e0_nb.bin"
.ENDS

ASSET_SECTION "spr_e0_s32_data"
spr_e0_s32:
    .incbin "res/spr_e0_s32.bin"
.ENDS

ASSET_SECTION "spr_e0_s16_data"
spr_e0_s16:
    .incbin "res/spr_e0_s16.bin"
.ENDS

ASSET_SECTION "spr_e1_n0_data"
spr_e1_n0:
    .incbin "res/spr_e1_n0.bin"
.ENDS

ASSET_SECTION "spr_e1_n1_data"
spr_e1_n1:
    .incbin "res/spr_e1_n1.bin"
.ENDS

ASSET_SECTION "spr_e1_n2_data"
spr_e1_n2:
    .incbin "res/spr_e1_n2.bin"
.ENDS

ASSET_SECTION "spr_e1_nb_data"
spr_e1_nb:
    .incbin "res/spr_e1_nb.bin"
.ENDS

ASSET_SECTION "spr_e1_s32_data"
spr_e1_s32:
    .incbin "res/spr_e1_s32.bin"
.ENDS

ASSET_SECTION "spr_e1_s16_data"
spr_e1_s16:
    .incbin "res/spr_e1_s16.bin"
.ENDS

ASSET_SECTION "spr_e2_n0_data"
spr_e2_n0:
    .incbin "res/spr_e2_n0.bin"
.ENDS

ASSET_SECTION "spr_e2_n1_data"
spr_e2_n1:
    .incbin "res/spr_e2_n1.bin"
.ENDS

ASSET_SECTION "spr_e2_n2_data"
spr_e2_n2:
    .incbin "res/spr_e2_n2.bin"
.ENDS

ASSET_SECTION "spr_e2_nb_data"
spr_e2_nb:
    .incbin "res/spr_e2_nb.bin"
.ENDS

ASSET_SECTION "spr_e2_s32_data"
spr_e2_s32:
    .incbin "res/spr_e2_s32.bin"
.ENDS

ASSET_SECTION "spr_e2_s16_data"
spr_e2_s16:
    .incbin "res/spr_e2_s16.bin"
.ENDS

ASSET_SECTION "spr_e3_n0_data"
spr_e3_n0:
    .incbin "res/spr_e3_n0.bin"
.ENDS

ASSET_SECTION "spr_e3_n1_data"
spr_e3_n1:
    .incbin "res/spr_e3_n1.bin"
.ENDS

ASSET_SECTION "spr_e3_n2_data"
spr_e3_n2:
    .incbin "res/spr_e3_n2.bin"
.ENDS

ASSET_SECTION "spr_e3_nb_data"
spr_e3_nb:
    .incbin "res/spr_e3_nb.bin"
.ENDS

ASSET_SECTION "spr_e3_s32_data"
spr_e3_s32:
    .incbin "res/spr_e3_s32.bin"
.ENDS

ASSET_SECTION "spr_e3_s16_data"
spr_e3_s16:
    .incbin "res/spr_e3_s16.bin"
.ENDS

ASSET_SECTION "spr_e4_n0_data"
spr_e4_n0:
    .incbin "res/spr_e4_n0.bin"
.ENDS

ASSET_SECTION "spr_e4_n1_data"
spr_e4_n1:
    .incbin "res/spr_e4_n1.bin"
.ENDS

ASSET_SECTION "spr_e4_n2_data"
spr_e4_n2:
    .incbin "res/spr_e4_n2.bin"
.ENDS

ASSET_SECTION "spr_e4_nb_data"
spr_e4_nb:
    .incbin "res/spr_e4_nb.bin"
.ENDS

ASSET_SECTION "spr_e4_s32_data"
spr_e4_s32:
    .incbin "res/spr_e4_s32.bin"
.ENDS

ASSET_SECTION "spr_e4_s16_data"
spr_e4_s16:
    .incbin "res/spr_e4_s16.bin"
.ENDS

ASSET_SECTION "spr_e5_n0_data"
spr_e5_n0:
    .incbin "res/spr_e5_n0.bin"
.ENDS

ASSET_SECTION "spr_e5_n1_data"
spr_e5_n1:
    .incbin "res/spr_e5_n1.bin"
.ENDS

ASSET_SECTION "spr_e5_n2_data"
spr_e5_n2:
    .incbin "res/spr_e5_n2.bin"
.ENDS

ASSET_SECTION "spr_e5_nb_data"
spr_e5_nb:
    .incbin "res/spr_e5_nb.bin"
.ENDS

ASSET_SECTION "spr_e5_s32_data"
spr_e5_s32:
    .incbin "res/spr_e5_s32.bin"
.ENDS

ASSET_SECTION "spr_e5_s16_data"
spr_e5_s16:
    .incbin "res/spr_e5_s16.bin"
.ENDS

ASSET_SECTION "spr_e6_n0_data"
spr_e6_n0:
    .incbin "res/spr_e6_n0.bin"
.ENDS

ASSET_SECTION "spr_e6_n1_data"
spr_e6_n1:
    .incbin "res/spr_e6_n1.bin"
.ENDS

ASSET_SECTION "spr_e6_n2_data"
spr_e6_n2:
    .incbin "res/spr_e6_n2.bin"
.ENDS

ASSET_SECTION "spr_e6_nb_data"
spr_e6_nb:
    .incbin "res/spr_e6_nb.bin"
.ENDS

ASSET_SECTION "spr_e6_s32_data"
spr_e6_s32:
    .incbin "res/spr_e6_s32.bin"
.ENDS

ASSET_SECTION "spr_e6_s16_data"
spr_e6_s16:
    .incbin "res/spr_e6_s16.bin"
.ENDS

ASSET_SECTION "spr_weapon2_data"
spr_weapon2:
    .incbin "res/spr_weapon2.bin"
.ENDS

ASSET_SECTION "spr_weapon_data"
spr_weapon:
    .incbin "res/spr_weapon.bin"
.ENDS

ASSET_SECTION "spr_wicons_data"
spr_wicons:
    .incbin "res/spr_wicons.bin"
.ENDS

ASSET_SECTION "card_tiles_data"
card_tiles:
    .incbin "res/card_tiles.bin"
.ENDS

ASSET_SECTION "spr_big_data"
spr_big:
    .incbin "res/spr_big.bin"
.ENDS

ASSET_SECTION "spr_obj1_data"
spr_obj1:
    .incbin "res/spr_obj1.bin"
.ENDS

ASSET_SECTION "spr_fx0_data"
spr_fx0:
    .incbin "res/spr_fx0.bin"
.ENDS

ASSET_SECTION "boss_tiles_data"
boss_tiles:
    .incbin "res/boss.bin"
.ENDS

;------------------------------------------------------------------------------
; void copyGpram(void)  - ROM image -> $70:0200 (Game Pak RAM), CPU owns the RAM
;------------------------------------------------------------------------------
.SECTION ".gpram_copy" SUPERFREE
.ACCU 16
.INDEX 16
copyGpram:
    php
    sep #$20
    .ACCU 8
    rep #$10
    .INDEX 16
    ldx #$0000
-   lda.l gpram_image,x
    sta.l $700200,x
    inx
    cpx #(gpram_image_end - gpram_image)
    bne -
    plp
    rtl
.ENDS



ASSET_SECTION "scr_0a_data"
scr_0a:
    .incbin "res/scr_0a.bin"
.ENDS

ASSET_SECTION "scr_0b_data"
scr_0b:
    .incbin "res/scr_0b.bin"
.ENDS

ASSET_SECTION "scr_1a_data"
scr_1a:
    .incbin "res/scr_1a.bin"
.ENDS

ASSET_SECTION "scr_1b_data"
scr_1b:
    .incbin "res/scr_1b.bin"
.ENDS

.endif
