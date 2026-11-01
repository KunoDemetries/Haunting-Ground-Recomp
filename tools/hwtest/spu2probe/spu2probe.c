/* IOP half of the struct_capture SPU2 envelope probe (docs/SOURCES.md 2026-10-25, "SPU2
 * sustain rate 0x7F"). Plays a silent looping ADPCM voice (core 0 voice 0, volume 0) with
 * Haunting Ground's ADSR settings and records ENVX over time, then after key-off. The EE
 * passes the address of a 16-byte-aligned buffer as argv[1] (hex); results go there by SIF
 * DMA before the module returns. Our own program, using the console's ROM LIBSD.
 *
 * Record (4 words): op (5 = key-on phase sample, 6 = after key-off), ADSR1<<16 | ADSR2,
 * microseconds since key-on (or since key-off), ENVX.
 */
#include "irx_imports.h"

IRX_ID("hg_spu2probe", 1, 1);

#define SAMPLES_ON 100
#define SAMPLES_OFF 64
#define SPU_DATA 0x10000u   /* byte address, clear of the reserved output areas */

static const unsigned short adsr2_cases[] = {
    0x5FEF, 0x5FCF, 0x5FC0,   /* the game's voices: sustain linear decrease, rate 0x7F */
    0x5F80, 0x5F40, 0x5EC0, 0x5DC0,   /* rates 0x7E, 0x7D, 0x7B, 0x77 for comparison */
    0xDFC0,                   /* exponential decrease, rate 0x7F */
    0x1FC0 };                 /* increase, rate 0x7F */
#define CASES (sizeof adsr2_cases / sizeof adsr2_cases[0])

static unsigned int results[CASES * (SAMPLES_ON + SAMPLES_OFF) * 4] __attribute__((aligned(64)));
static unsigned char block[64] __attribute__((aligned(64)));

static unsigned int usec_now(void) {
    iop_sys_clock_t clock;
    u32 sec, usec;
    GetSystemTime(&clock);
    SysClock2USec(&clock, &sec, &usec);
    return sec * 1000000u + usec;
}

static unsigned int parse_hex(const char *s) {
    unsigned int v = 0;
    for (; *s; ++s) {
        char c = *s;
        v = v * 16 + (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
    }
    return v;
}

int _start(int argc, char *argv[]) {
    unsigned int i, k, n = 0, start;
    SifDmaTransfer_t dma;
    int id;
    if (argc < 2) return MODULE_NO_RESIDENT_END;
    sceSdInit(0);
    /* Four silent ADPCM blocks looping to the first: flags loop-start, -, -, loop-end+repeat. */
    for (i = 0; i < sizeof block; ++i) block[i] = 0;
    block[1] = 0x04; block[49] = 0x03;
    sceSdVoiceTrans(0, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, block, (u32 *)SPU_DATA, sizeof block);
    sceSdVoiceTransStatus(0, 1);
    sceSdSetParam(SD_VOICE(0, 0) | SD_VPARAM_VOLL, 0);
    sceSdSetParam(SD_VOICE(0, 0) | SD_VPARAM_VOLR, 0);
    sceSdSetParam(SD_VOICE(0, 0) | SD_VPARAM_PITCH, 0x1000);
    sceSdSetAddr(SD_VOICE(0, 0) | SD_VADDR_SSA, SPU_DATA);
    for (k = 0; k < CASES; ++k) {
        const unsigned int adsr1 = 0x80FF, adsr2 = adsr2_cases[k];
        sceSdSetSwitch(SD_SWITCH_KOFF, 1);
        DelayThread(300000);   /* let the previous release finish */
        sceSdSetParam(SD_VOICE(0, 0) | SD_VPARAM_ADSR1, adsr1);
        sceSdSetParam(SD_VOICE(0, 0) | SD_VPARAM_ADSR2, adsr2);
        sceSdSetSwitch(SD_SWITCH_KON, 1);
        start = usec_now();
        for (i = 0; i < SAMPLES_ON; ++i) {
            results[n++] = 5; results[n++] = adsr1 << 16 | adsr2;
            results[n++] = usec_now() - start;
            results[n++] = sceSdGetParam(SD_VOICE(0, 0) | SD_VPARAM_ENVX);
            DelayThread(20000);
        }
        sceSdSetSwitch(SD_SWITCH_KOFF, 1);
        start = usec_now();
        for (i = 0; i < SAMPLES_OFF; ++i) {
            results[n++] = 6; results[n++] = adsr1 << 16 | adsr2;
            results[n++] = usec_now() - start;
            results[n++] = sceSdGetParam(SD_VOICE(0, 0) | SD_VPARAM_ENVX);
            DelayThread(2000);
        }
    }
    dma.src = results; dma.dest = (void *)parse_hex(argv[1]); dma.size = sizeof results; dma.attr = 0;
    id = sceSifSetDma(&dma, 1);
    while (sceSifDmaStat(id) >= 0) DelayThread(1000);
    return MODULE_NO_RESIDENT_END;
}
