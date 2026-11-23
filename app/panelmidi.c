/* SPDX-License-Identifier: MPL-2.0 */
/* PANEL MIDI: the FM-1's panel as a MIDI controller for another FM-1 (an emulated one, on a phone or a computer),
 * LOOM-8 itself deaf to it while it lasts.
 *
 * On: every key, button, encoder and the MASTER pot goes out over USB and Bluetooth as MIDI, and LOOM-8 no longer sees
 * any of them. It keeps playing and keeping time (its sequencer, its clock out, its USB audio); only the panel is let
 * go. Not saved: LOOM-8 always starts with the panel its own. Three ways in and out:
 *   - Setup (GLO, SELECT), USB and MIDI, PANEL MIDI: ALGORITHM turns it on;
 *   - the shortcut: GLO + FX held for a second, on or off (the way back, since the menu is out of reach then);
 *   - the app on the other side: SysEx SB_PANEL (samples.c: F0 7D 46 4C 30 v F7, v 1 on, 0 off, 2 asks), answered
 *     with the state, so the app can switch the FM-1 over and show it.
 *
 * What goes out is RemoteFM1's panel-event map (RemoteFM1 docs/PROTOCOL.md, "panel events"), which fm1-emu's fm1-map
 * profile "remotefm1-panel", FM1VST and the RemoteFM1 hosts take as it is:
 *   keys      note 53 + k on channel 16 (k = 0 is F3), velocity 100, note off on release
 *   buttons   note = the matrix id on channel 15 (OCT- 0, OCT+ 1, FX 2, SEL 3, ENV 4, LFO 5, EDIT 6, GLO 7, HOME 8,
 *             SAVE 9, ARP 10, SEQ 11, PLAY 12, REC 13), velocity 127 down, note off up
 *   encoders  CC 16 + the matrix id on channel 15, 64 + detents (SELECT 0, ALGORITHM 1, KNOBS 1-4 2..5, PRESETS 6)
 *   MASTER    CC 7 (the top 7 bits) then CC 39 (the low 7) on channel 15, 14 bits
 * LOOM-8 never sends on channels 12 to 16, so this does not meet its own notes and CCs.
 *
 * The lights while it lasts: the other FM-1's, when its app sends them (SysEx SB_LEDS: F0 7D 46 4C 31 then 41 levels
 * 0..127 by matrix id, buttons 0..13 then keys 14..40, F7; at least every second), so the panel shows what it now
 * plays: 64 and up lit, 24 and up the middle level, 6 and up dim. Without them (no app sending, or none for 1.5 s):
 * GLO and FX lit, the way back, and nothing else.
 *
 * Part of ui.c (included before input()): ui.btn, ui.keys, ui.btn_used, say(), loom_lights. */

#define PM_CH_BTN 14u                          /* channel 15 */
#define PM_CH_KEY 15u                          /* channel 16 */
#define PM_HOLD_MS 1000u                       /* the chord */
#define PM_CHORD ((1u << B_GLO) | (1u << B_FX))
#define PM_NLED 41u                            /* 14 buttons, 27 keys, by matrix id */
#define PM_LED_MS 1500u                        /* the other side's lights kept this long after the last */

static uint8_t pm_on;                          /* the panel goes out as MIDI (declared in pages.c too: Setup shows it) */
static uint8_t pm_fired;                       /* the chord has acted: wait for it to be let go */
static uint32_t pm_chord_t;                    /* when the chord began (|1: never 0) */
static uint32_t pm_btn, pm_keys;               /* what has been sent as held */
static uint32_t pm_ign;                        /* held when the panel went away: its release is not sent */
static int32_t pm_master = -1000;              /* the MASTER position last sent (0..4096) */
static uint8_t pm_led[PM_NLED];                /* the other side's lights, 0..127 */
static uint32_t pm_led_t;                      /* when they came (|1: never 0; 0: none) */

static void pm_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    /* USB-MIDI event packet: cable 0 and the code index (the status's high nibble), then the three bytes */
    if (plat_midi_out_room(TO_USB | TO_BLE) < 2u)
        return;                                /* a full queue loses a detent, never a note-off's turn: those come first */
    plat_midi_send((st >> 4) | st << 8 | d1 << 16 | d2 << 24, TO_USB | TO_BLE);
}

static uint32_t pm_btn_id(uint32_t i)          /* B_* -> the matrix id */
{
    return i == B_OCTDN ? 0u : i == B_OCTUP ? 1u : i + 2u;
}

static uint32_t pm_enc_id(uint32_t i)          /* EN_* -> the matrix encoder id */
{
    return i == EN_PRESET ? 6u : i >= EN_K1 ? i - EN_K1 + 2u : i;
}

/* let go of what is held (a note off for each), as the panel goes back to LOOM-8 or away from it */
static void pm_release(void)
{
    uint32_t i;
    for (i = 0; i < NB; i++)
        if (pm_btn >> i & 1u)
            pm_send(0x80u | PM_CH_BTN, pm_btn_id(i), 0);
    for (i = 0; i < NKEYS; i++)
        if (pm_keys >> i & 1u)
            pm_send(0x80u | PM_CH_KEY, 53u + i, 0);
    pm_btn = pm_keys = 0;
}

/* on or off, from the chord, Setup or the app. What is held now is LOOM-8's (going away: not sent; coming back: not a
 * new press for LOOM-8) */
static void pm_set(int on)
{
    uint32_t i, btn = plat_buttons();
    if (!on == !pm_on)
        return;
    if (pm_on) {
        pm_release();
        pm_on = 0;
        for (i = 0; i < NE; i++)
            (void)plat_enc((int)i);
        ui.btn = btn;
        ui.keys = plat_keys();
        ui.btn_used |= btn;
        pm_led_t = 0;
        say("PANEL MIDI", "off");
        return;
    }
    pm_on = 1;
    pm_ign = btn;
    pm_btn = pm_keys = 0;
    pm_master = -1000;
    pm_led_t = 0;
    for (i = 0; i < NE; i++)
        (void)plat_enc((int)i);                /* (a turn already counted stays LOOM-8's) */
    say("PANEL MIDI", "on: GLO+FX back");
}

/* the other side's lights (SB_LEDS' arguments) */
static void pm_leds(const uint8_t *a, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < PM_NLED; i++)
        pm_led[i] = i < n ? a[i] : 0u;
    pm_led_t = plat_ms() | 1u;
}

/* the lights while the panel is MIDI: the other side's, else the way back; 0 = not on (LOOM-8's own) */
static int pm_lights(void)
{
    uint32_t i, b = 0, k = 0, mb = 0, mk = 0, db = 0, dk = 0;
    if (!pm_on)
        return 0;
    if (!pm_led_t || plat_ms() - pm_led_t > PM_LED_MS) {
        loom_lights(PM_CHORD, 0, 0, 0, 0);
        plat_lights_mid(0, 0);
        return 1;
    }
    for (i = 0; i < PM_NLED; i++) {
        uint32_t v = pm_led[i], bit;
        if (v < 6u)
            continue;
        if (i < 14u) {                         /* a button, by matrix id -> B_* */
            bit = 1u << (i == 0u ? B_OCTDN : i == 1u ? B_OCTUP : i - 2u);
            if (v >= 64u) b |= bit; else if (v >= 24u) mb |= bit; else db |= bit;
        } else {
            bit = 1u << (i - 14u);
            if (v >= 64u) k |= bit; else if (v >= 24u) mk |= bit; else dk |= bit;
        }
    }
    /* (loom_lights glows the unlit buttons only all together: the dim ones show at the middle level instead) */
    loom_lights(b, k, 0, (dk | mk) & ~k, 0);
    plat_lights_mid((mb | db) & ~b, mk & ~k);
    return 1;
}

/* called first in input(): 1 = the panel went out as MIDI this frame, LOOM-8 is not to look at it */
static int pm_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), now = plat_ms(), i, ch;
    int32_t e, m;

    if ((btn & PM_CHORD) == PM_CHORD && !(btn & ~PM_CHORD) && !keys) {
        if (!pm_chord_t)
            pm_chord_t = now | 1u;
        else if (!pm_fired && now - pm_chord_t >= PM_HOLD_MS) {
            pm_fired = 1;
            pm_set(!pm_on);
            return 1;
        }
    } else {
        pm_chord_t = 0;
        pm_fired = 0;
    }
    if (!pm_on)
        return 0;

    pm_ign &= btn;                             /* (a button held when it began, let go: its note off is not sent) */
    ch = (btn ^ pm_btn) & ~pm_ign;
    for (i = 0; i < NB; i++)
        if (ch >> i & 1u)
            pm_send((btn >> i & 1u ? 0x90u : 0x80u) | PM_CH_BTN, pm_btn_id(i), btn >> i & 1u ? 127u : 0u);
    pm_btn = btn & ~pm_ign;
    ch = keys ^ pm_keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u)
            pm_send((keys >> i & 1u ? 0x90u : 0x80u) | PM_CH_KEY, 53u + i, keys >> i & 1u ? 100u : 0u);
    pm_keys = keys;
    for (i = 0; i < NE; i++)
        if ((e = plat_enc((int)i)) != 0)
            pm_send(0xb0u | PM_CH_BTN, 16u + pm_enc_id(i), (uint32_t)(64 + clamp(e, -63, 63)));
    m = (int32_t)plat_master();
    if (m - pm_master >= 8 || pm_master - m >= 8 || (m != pm_master && (m == 0 || m >= 4095))) {
        uint32_t v14 = (uint32_t)clamp(m * 4, 0, 16383);
        pm_master = m;
        pm_send(0xb0u | PM_CH_BTN, 7u, v14 >> 7);
        pm_send(0xb0u | PM_CH_BTN, 39u, v14 & 127u);
    }
    /* LOOM-8 does not see the panel, but its own lights and clocks go on: keep what it believes in step, so that
     * the chord's release, or a press that began before, is not read as new */
    ui.btn = btn;
    ui.keys = keys;
    ui.btn_used |= btn;
    return 1;
}
