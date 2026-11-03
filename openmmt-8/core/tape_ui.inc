static void tape_send(void) __naked
{
    __asm
        mov     r7, #8
        push    acc
00011$:
        pop     acc
        rrc     a
        push    acc
        mov     a, #0x0c
        jnc     00012$
        mov     a, #0x02
00012$:
        mov     _tape_sh, a
00013$:
        mov     dptr, #0xff06
        mov     a, #0x7e
        movx    @dptr, a
        jnb     _P1_5, 00014$
        mov     a, _tape_sh
        jnz     00013$
        djnz    r7, 00011$
        pop     acc
        clr     c
        ret
00014$:
        pop     acc
        setb    c
        ret
    __endasm;
}

static void tape_progress(void)
{
    if (page == 1)
        ui_screen(T_TAPEVFY);
    else if (page == 3 || page == 4) {
        ui_screen(page == 3 ? T_TAPEPRT : T_TAPESNG);
        lcd_num(13, tape_num, 2);
    } else
        ui_screen(T_TAPELOD);
}

static void tape_tab_dptr(void) __naked
{
    __asm
        mov     a, r6
        mov     dptr, #0x0400
        cjne    a, #100, 00001$
00001$:
        jc      00002$
        add     a, #0x9c
        mov     dptr, #0x0502
00002$:
        rl      a
        add     a, dpl
        mov     dpl, a
        clr     a
        addc    a, dph
        mov     dph, a
        ret
    __endasm;
}

static void tape_decode(void) __naked
{
    __asm
        mov     dptr, #0xff06
        mov     a, #0x7e
        movx    @dptr, a
        clr     _ET0
00030$:
        mov     r5, #0
00031$:
        mov     r4, #1
        lcall   00060$
        mov     a, r5
        rrc     a
        mov     r5, a
        cjne    a, #0x55, 00031$
        lcall   00040$
        cjne    a, #0x80, 00030$
        lcall   00040$
        mov     r3, #200
00001$:
        mov     r0, #4
00002$:
        lcall   00040$
        djnz    r0, 00002$
        lcall   00040$
        lcall   00040$
        mov     r6, a
        push    ar6
        mov     a, r6
        mov     dpl, a
        .db     0x12
        .dw     _tape_lcd_part
        pop     ar6
        lcall   00040$
        mov     b, a
        mov     a, _tape_expect
        cjne    a, #0xff, 77315$
        sjmp    77320$
77315$:
        xrl     a, r6
        jz      77320$
        clr     _tape_go
        sjmp    88392$
77320$:
        mov     a, b
        jnz     77330$
        .db     0x12
        .dw     _tape_tab_dptr
        clr     a
        movx    @dptr, a
        inc     dptr
        movx    @dptr, a
        sjmp    88392$
77330$:
        mov     a, b
        add     a, #2
        mov     r4, a
        mov     dptr, #0x04e6
        movx    a, @dptr
        mov     r2, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     a, r2
        add     a, r4
        mov     r0, a
        mov     a, r7
        addc    a, #0
        mov     r1, a
        mov     dptr, #0x04e6
        mov     a, r0
        movx    @dptr, a
        inc     dptr
        mov     a, r1
        movx    @dptr, a
        .db     0x12
        .dw     _tape_tab_dptr
        mov     a, r7
        movx    @dptr, a
        inc     dptr
        mov     a, r2
        movx    @dptr, a
        mov     dpl, r2
        mov     dph, r7
        mov     a, r4
        movx    @dptr, a
        inc     dptr
        clr     a
        movx    @dptr, a
        mov     a, r2
        add     a, #1
        mov     _tape_w, a
        mov     a, r7
        addc    a, #0
        mov     (_tape_w + 1), a
        mov     _tape_n, b
        setb    _tape_go
        sjmp    88392$
88392$:
        mov     a, b
        jz      00003$
        mov     r0, a
        inc     r0
00004$:
        lcall   00040$
        jb      _tape_go, 77390$
        sjmp    77391$
77390$:
        mov     dpl, _tape_w
        mov     dph, (_tape_w + 1)
        movx    @dptr, a
        inc     _tape_w
        mov     a, _tape_w
        jnz     77391$
        inc     (_tape_w + 1)
        mov     a, _tape_n
        add     a, #0xff
        mov     _tape_n, a
        jz      77393$
        sjmp    77391$
77393$:
        clr     _tape_go
77391$:
        djnz    r0, 00004$
        sjmp    00005$
00003$:
        lcall   00040$
00005$:
        mov     r0, #4
00006$:
        lcall   00040$
        djnz    r0, 00006$
        mov     a, r3
        add     a, #0xff
        mov     r3, a
        jz      00089$
        ljmp    00001$
00040$:
        mov     r7, #8
        mov     r5, #0
00041$:
        mov     r4, #2
        lcall   00060$
        mov     a, r5
        rrc     a
        mov     r5, a
        djnz    r7, 00041$
        mov     a, r5
        ret
00060$:
00061$:
        mov     dptr, #0xff06
        mov     a, #0x7e
        movx    @dptr, a
        jnb     _P1_5, 00067$
        jnb     _P3_2, 00062$
        sjmp    00061$
00062$:
        mov     r1, #0
00063$:
        inc     r1
        jnb     _P1_5, 00067$
        jnb     _P3_2, 00063$
        clr     c
        mov     a, r1
        subb    a, #60
        ret
00067$:
        pop     acc
        pop     acc
        djnz    r4, 00067$
00089$:
        mov     dptr, #0xff06
        mov     a, #0x7e
        movx    @dptr, a
        jnb     _P1_5, 00092$
        sjmp    00089$
00092$:
        jb      _tape_clk, 00093$
        setb    _ET0
00093$:
        setb    _tape_aborted
        setb    _ui_dirty
        ret
    __endasm;
}

static void tape_save(void) __naked
{
    __asm
        jnb     _tape_wait, 00005$
        ret
00020$:
        lcall   _tape_send
        jc      00022$
        ret
00022$:
        pop     acc
        pop     acc
        sjmp    00090$
00060$:
        mov     b, a
        xrl     a, r4
        mov     r4, a
        mov     a, b
        add     a, r2
        mov     r2, a
        jnc     00061$
        inc     r3
00061$:
        mov     a, b
        sjmp    00020$
00090$:
        setb    _tape_aborted
        setb    _ui_dirty
00091$:
        clr     _ET1
        mov     _TH1, #0xff
        mov     _P3, #0xff
        jb      _tape_clk, 00092$
        setb    _ET0
00092$:
        ret
00005$:
        mov     dptr, #0xff06
        mov     a, #0x7e
        movx    @dptr, a
        setb    _ET0
        mov     _TH1, #0x30
        setb    _TR1
        mov     _tape_sh, #0
        setb    _ET1
        mov     r0, #0xdc
        mov     r1, #0x05
00006$:
        clr     a
        lcall   00020$
        mov     a, r0
        add     a, #0xff
        mov     r0, a
        cjne    a, #0xff, 00007$
        mov     a, r1
        add     a, #0xff
        mov     r1, a
00007$:
        mov     a, r0
        orl     a, r1
        jnz     00006$
        mov     a, #1
        lcall   00020$
        mov     _tape_sh, #0xf0
        jnb     _tape_wait, 00010$
00008$:
        mov     dptr, #0xff06
        mov     a, #0x7e
        movx    @dptr, a
        jb      _P1_5, 00010$
        ljmp    00090$
00010$:
        mov     a, _tape_sh
        jnz     00010$
        mov     r0, #10
00011$:
        clr     a
        lcall   00020$
        djnz    r0, 00011$
        mov     a, #0x55
        lcall   00020$
        mov     a, #0x80
        lcall   00020$
        mov     a, #0x01
        lcall   00020$
        mov     r6, #0
00030$:
        push    ar6
        mov     a, r6
        mov     dpl, a
        .db     0x12
        .dw     _tape_lcd_part
        pop     ar6
        clr     a
        mov     r2, a
        mov     r3, a
        mov     r4, a
        mov     r0, #4
00034$:
        clr     a
        lcall   00020$
        djnz    r0, 00034$
        mov     a, #0x55
        lcall   00020$
        mov     a, r6
        lcall   00060$
        .db     0x12
        .dw     _tape_tab_dptr
        movx    a, @dptr
        jz      00050$
        mov     r1, a
        inc     dptr
        movx    a, @dptr
        mov     dpl, a
        mov     dph, r1
        movx    a, @dptr
        add     a, #0xfe
        mov     r1, a
        lcall   00060$
        inc     r1
        inc     dptr
00037$:
        movx    a, @dptr
        inc     dptr
        lcall   00060$
        djnz    r1, 00037$
        sjmp    00051$
00050$:
        clr     a
        lcall   00060$
        clr     a
        lcall   00060$
00051$:
        mov     a, r3
        lcall   00020$
        mov     a, r2
        lcall   00020$
        mov     a, r4
        lcall   00020$
        clr     a
        lcall   00020$
        inc     r6
        cjne    r6, #200, 00029$
        clr     _tape_run
        ljmp    00091$
00029$:
        sjmp    00030$
    __endasm;
}

void ui_tape(void)
{
    tape_save();
    if (tape_aborted)
        return;
    if (tape_wait && !tape_heard) {
        uint8_t i, p;
        for (i = 0; i < 200u; ++i) {
            p = P3;
            if ((tape_p3_ref ^ p) & 0x04u) {
                tape_p3_ref = p;
                if (++tape_edges >= TAPE_EDGES) {
                    tape_heard = 1;
                    ui_dirty = 1;
                    break;
                }
            }
        }
    }
    if (tape_heard) {
        tape_progress();
        if (!tape_decode_go) {
            tape_decode_go = 1;
            ui_dirty = 1;
        } else
            tape_decode();
    }
}
