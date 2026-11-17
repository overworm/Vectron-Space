.include "memmap.inc"
.include "assets.inc"

;************************************************
; snesmod soundbank data                        *
; total size:     104428 bytes                  *
;************************************************

.BANK 40
.ORG 0
.SECTION "SOUNDBANK0" FORCE ; need dedicated bank(s)

soundbank0:
.incbin "./soundbank.bnk" read $8000
.ENDS

.BANK 41
.ORG 0
.SECTION "SOUNDBANK1" FORCE ; need dedicated bank(s)

soundbank1:
.incbin "./soundbank.bnk" skip $8000 read $8000
.ENDS

.BANK 42
.ORG 0
.SECTION "SOUNDBANK2" FORCE ; need dedicated bank(s)

soundbank2:
.incbin "./soundbank.bnk" skip $10000 read $8000
.ENDS

.BANK 43
.ORG 0
.SECTION "SOUNDBANK3" FORCE ; need dedicated bank(s)

soundbank3:
.incbin "./soundbank.bnk" skip $18000
.ENDS

