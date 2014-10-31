; 2013-12-18 William R Sowerbutts

        .module crt0

        ; Ordering of segments for the linker.
        ; WRS: Note we list all our segments here, even though
        ; we don't use them all, because their ordering is set
        ; when they are first seen.
        .area _CODE
        .area _CODE2
        .area _CONST
        .area _DATA
        .area _INITIALIZED
        .area _BSEG
        .area _BSS
        .area _HEAP
        ; note that areas below here may be overwritten by the heap at runtime, so
        ; put initialisation stuff in here
        .area _INITIALIZER
        .area _GSINIT
        .area _GSFINAL
        .area _COMMONMEM

        ; imported symbols
        .globl _fuzix_main
        .globl init_early
        .globl init_hardware
        .globl s__INITIALIZER
        .globl s__COMMONMEM
        .globl l__COMMONMEM
        .globl s__DATA
        .globl l__DATA
        .globl kstack_top

        ; startup code
        .area _CODE
init:
        ld sp, #kstack_top

	ld a, #'*'
	out (1), a
        ; Configure memory map
        call init_early

	; move the common memory where it belongs    
	ld hl, #s__INITIALIZER
	ld de, #s__COMMONMEM
	ld bc, #l__COMMONMEM
	ldir
	ld a, #'*'
	out (1), a
	; then zero the data area
	ld hl, #s__DATA
	ld de, #s__DATA + 1
	ld bc, #l__DATA - 1
	ld (hl), #0
	ldir
	ld a, #'*'
	out (1), a

        ; Hardware setup
        call init_hardware

        ; Call the C main routine
        call _fuzix_main
    
        ; main shouldn't return, but if it does...
        di
stop:   halt
        jr stop

