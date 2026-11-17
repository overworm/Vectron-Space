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

ASSET_SECTION "gsu_s_code"
gsu_s:
    .incbin "gsu_s.sfx.bin"
    .REPEAT 16
    .db $01
    .ENDR
gsu_s_end:
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
gsuLoadS:
    php
    phb
    rep #$30
    sep #$20
    lda #$00
    sta.l $003030
    sta.l $003031
    rep #$30
    ldx #(gsu_s & $FFFF)
    ldy #$3100
    lda #(gsu_s_end - gsu_s) & $FFF0
    dec a
    mvn :gsu_s, $00
    plb
    plp
    rtl

; one enemy frame, 2048 bytes: WRAM $7F:0000 -> Game Pak RAM $70:C000 / $70:C800 (the CPU owns the RAM only while the GSU is stopped)
gsMove0:
    php
    phb
    rep #$30
    ldx #$0000
    ldy #$C000
    lda #2047
    mvn $7F, $70
    plb
    plp
    rtl
gsMove1:
    php
    phb
    rep #$30
    ldx #$0000
    ldy #$C800
    lda #2047
    mvn $7F, $70
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

ASSET_SECTION "spr_hbar_data"
spr_hbar:
    .incbin "res/spr_hbar.bin"
.ENDS

ASSET_SECTION "spr_note_data"
spr_note:
    .incbin "res/spr_note.bin"
.ENDS

ASSET_SECTION "spr_wire_data"
spr_wire:
    .incbin "res/spr_wire.bin"
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

ASSET_SECTION "spr_prop_data"
spr_prop:
    .incbin "res/spr_prop.bin"
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

ASSET_SECTION "gspr_e0_a_data"
gspr_e0_a:
    .incbin "res/gspr_e0_a.bin"
.ENDS

ASSET_SECTION "gspr_e0_b_data"
gspr_e0_b:
    .incbin "res/gspr_e0_b.bin"
.ENDS

ASSET_SECTION "gspr_e1_a_data"
gspr_e1_a:
    .incbin "res/gspr_e1_a.bin"
.ENDS

ASSET_SECTION "gspr_e1_b_data"
gspr_e1_b:
    .incbin "res/gspr_e1_b.bin"
.ENDS

ASSET_SECTION "gspr_e2_a_data"
gspr_e2_a:
    .incbin "res/gspr_e2_a.bin"
.ENDS

ASSET_SECTION "gspr_e2_b_data"
gspr_e2_b:
    .incbin "res/gspr_e2_b.bin"
.ENDS

ASSET_SECTION "gspr_e3_a_data"
gspr_e3_a:
    .incbin "res/gspr_e3_a.bin"
.ENDS

ASSET_SECTION "gspr_e3_b_data"
gspr_e3_b:
    .incbin "res/gspr_e3_b.bin"
.ENDS

ASSET_SECTION "gspr_e4_a_data"
gspr_e4_a:
    .incbin "res/gspr_e4_a.bin"
.ENDS

ASSET_SECTION "gspr_e4_b_data"
gspr_e4_b:
    .incbin "res/gspr_e4_b.bin"
.ENDS

ASSET_SECTION "gspr_e5_a_data"
gspr_e5_a:
    .incbin "res/gspr_e5_a.bin"
.ENDS

ASSET_SECTION "gspr_e5_b_data"
gspr_e5_b:
    .incbin "res/gspr_e5_b.bin"
.ENDS

ASSET_SECTION "gspr_e6_a_data"
gspr_e6_a:
    .incbin "res/gspr_e6_a.bin"
.ENDS

ASSET_SECTION "gspr_e6_b_data"
gspr_e6_b:
    .incbin "res/gspr_e6_b.bin"
.ENDS


;==============================================================================
; gsuDmaFrame8 - v11: the 8 bpp view (160x104) from Game Pak RAM to VRAM: 20 tile columns of 13 tiles
; (832 bytes each, 64 bytes a tile; column xt starts at VRAM word xt*512 and at source + xt*1024).
; The wait for the window is the lib's gsuDmaFullFrame (copied): the transfer must fit into the blank bands:
; 16640 bytes = ~109 lines, so the last start line is 262 + top - 109 = 153 + top.
;==============================================================================
.SECTION "gsu_dff8" SUPERFREE
.ACCU 16
.INDEX 16
gsuDmaFrame8:
    php
    rep #$30
    lda.l gsu_pres_bottom
    and #$00FF
    eor #$FFFF
    sec
    adc #225
    sta.l gsu_dff_first
    lda.l gsu_pres_top
    and #$00FF
    clc
    adc #159                 ; PAL: 312 + top - 153 lines (23296 bytes = ~153 lines); NTSC cannot fit this frame
    cmp.l gsu_dff_first
    bcs +
    lda.l gsu_dff_first
+   sta.l gsu_dff_last
    lda.l console_force_blanked
    and #$00FF
    bne @go
@poll:
    jsr _dff8_v
    cmp.l gsu_dff_first
    bcc @poll
    cmp.l gsu_dff_last
    beq @go
    bcc @go
@late:
    jsr _dff8_v
    cmp.l gsu_dff_first
    bcs @late
    bra @poll
@go:
    sep #$20
    .ACCU 8
    lda #$80
    sta.l $2115              ; VMAIN: word increment
    lda #$01
    sta.l $4300              ; mode: 2-register write
    lda #$18
    sta.l $4301              ; dest: VMDATAL
    lda #$70
    sta.l $4304              ; source bank = Game Pak RAM
    rep #$30
    .ACCU 16
    lda.l gsu_dma_src_hi
    and #$00FF
    xba
    tay                      ; Y = source of column 0
    ldx #$0000               ; X = VRAM word of column 0
@col:
    txa
    sta.l $2116
    tya
    sta.l $4302
    lda #832
    sta.l $4305
    sep #$20
    .ACCU 8
    lda #$01
    sta.l $420B
    rep #$20
    .ACCU 16
    tya
    clc
    adc #1024
    tay
    txa
    clc
    adc #416
    tax
    cpx #(28 * 416)
    bne @col
    plp
    rtl

_dff8_v:
    sep #$20
    .ACCU 8
    lda.l $00213F
    lda.l $002137
    lda.l $00213D
    sta.l gsu_dff_v
    lda.l $00213D
    and #$01
    sta.l gsu_dff_v+1
    rep #$20
    .ACCU 16
    lda.l gsu_dff_v
    rts
.ENDS


.endif
