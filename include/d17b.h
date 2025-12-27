/*
 * D17B/D37C Minuteman Guidance Computer Emulator
 *
 * Supports both:
 *   - D17B (Minuteman I) - 39 instructions
 *   - D37C (Minuteman II/III) - 57 instructions (adds DIV, ORA, rotates, etc.)
 *
 * Architecture:
 *   - 24-bit word length
 *   - Serial-binary arithmetic
 *   - Rotating disc memory @ 6000 RPM
 *   - D17B: 2944 words, D37C: 7222 words
 *   - Rapid-access loops: U(1), F(4), E(8), H(16), L(1), V(4), R(4)
 *
 * References:
 *   - D17B Computer Programming Manual (Sep 1971)
 *   - MCUG Documentation (Tulane University)
 *   - D37C Conversion for General Purpose Applications (AFIT, Mar 1974)
 *
 * Copyright 2025 Zane Hambly - Apache 2.0 License
 */

#ifndef D17B_H
#define D17B_H

#include <stdint.h>
#include <stdbool.h>

/* Word size constants */
#define WORD_BITS       24
#define WORD_MASK       0x00FFFFFF
#define SIGN_BIT        0x00800000
#define MAGNITUDE_MASK  0x007FFFFF

/* Memory layout */
#define CHANNELS        47          /* Channels 00-46 (octal) */
#define SECTORS         128         /* Sectors per channel */
#define MAIN_MEMORY     (CHANNELS * SECTORS)  /* 6016 words theoretical */
#define ACTUAL_MEMORY   2944        /* Actual addressable words */

/* Disc timing - 6000 RPM = 100 revolutions/second */
#define DISC_RPM        6000
#define WORD_TIME_US    78.125      /* Microseconds per word time */
#define SECTOR_TIME_US  78.125      /* Same as word time */
#define REV_TIME_MS     10.0        /* 10ms per revolution */

/* Rapid-access loop sizes */
#define U_LOOP_SIZE     1
#define F_LOOP_SIZE     4
#define E_LOOP_SIZE     8
#define H_LOOP_SIZE     16
#define L_LOOP_SIZE     1
#define V_LOOP_SIZE     4
#define R_LOOP_SIZE     4

/* Channel addresses (octal -> decimal) */
#define CHAN_F_LOOP     0x2A        /* 52 octal */
#define CHAN_H_LOOP     0x2C        /* 54 octal */
#define CHAN_E_LOOP     0x2E        /* 56 octal */
#define CHAN_U_LOOP     0x30        /* 60 octal */
#define CHAN_L_REG      0x34        /* 64 octal */
#define CHAN_V_LOOP     0x38        /* 70 octal */
#define CHAN_R_LOOP     0x3A        /* 72 octal */

/* Instruction format (24 bits):
 *
 *  23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |    OP CODE    | F|     Sp      |        C (channel)    |  S (sector) |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * OP CODE: 4 bits (T23-T20) - operation code
 * F:       1 bit  (T19)     - flag bit for flag store mode
 * Sp:      4 bits (T18-T15) - next sector pointer
 * C:       6 bits (T14-T9)  - channel address (operand)
 * S:       7 bits (T8-T2)   - sector address (operand)
 *
 * Note: Bits numbered T24-T0 in documentation, T24 is sign
 */

/* Instruction field extraction */
#define GET_OPCODE(w)   (((w) >> 20) & 0x0F)
#define GET_FLAG(w)     (((w) >> 19) & 0x01)
#define GET_SP(w)       (((w) >> 15) & 0x0F)
#define GET_CHANNEL(w)  (((w) >> 9) & 0x3F)
#define GET_SECTOR(w)   (((w) >> 2) & 0x7F)
#define GET_FLAG_CODE(w) ((w) & 0x07)  /* Low 3 bits for flag store */

/* Opcode definitions (4-bit primary opcodes) */
typedef enum {
    OP_SHIFT    = 0x0,  /* 00 - Shift/Rotate instructions */
    OP_SCL      = 0x1,  /* 04 - Split Compare and Limit */
    OP_TMI_TZE  = 0x2,  /* 10 - Transfer on Minus (D17B) / Transfer on Zero (D37C) */
    OP_SMP      = 0x4,  /* 20 - Split Multiply */
    OP_MPY      = 0x5,  /* 24 - Multiply */
    OP_TMI      = 0x6,  /* 30 - Transfer on Minus */
    OP_DIV_MPM  = 0x7,  /* 34 - Divide (D37C) / Multiply Magnitude (D17B) */
    OP_SPECIAL  = 0x8,  /* 40 - Special instructions (COM, MIM, ANA, ORA, etc.) */
    OP_CLA      = 0x9,  /* 44 - Clear and Add */
    OP_TRA      = 0xA,  /* 50 - Transfer (unconditional jump) */
    OP_STO      = 0xB,  /* 54 - Store Accumulator */
    OP_SAD      = 0xC,  /* 60 - Split Add */
    OP_ADD      = 0xD,  /* 64 - Add */
    OP_SSU      = 0xE,  /* 70 - Split Subtract */
    OP_SUB      = 0xF,  /* 74 - Subtract */
} d17b_opcode_t;

/* Special instruction sub-opcodes (when primary opcode is 0x8/40) */
typedef enum {
    SPEC_BOC    = 0x01, /* 40 02 - Binary Output C */
    SPEC_BOA    = 0x04, /* 40 10 - Binary Output A */
    SPEC_BOB    = 0x05, /* 40 12 - Binary Output B */
    SPEC_RSD    = 0x08, /* 40 20 - Reset Detector */
    SPEC_HPR    = 0x09, /* 40 22 - Halt and Proceed */
    SPEC_DOA    = 0x0B, /* 40 26 - Discrete Output A */
    SPEC_VOA    = 0x0C, /* 40 30 - Voltage Output A */
    SPEC_VOB    = 0x0D, /* 40 32 - Voltage Output B */
    SPEC_VOC    = 0x0E, /* 40 34 - Voltage Output C */
    SPEC_ORA    = 0x10, /* 40 40 - OR to Accumulator (D37C only) */
    SPEC_ANA    = 0x11, /* 40 42 - AND to Accumulator */
    SPEC_MIM    = 0x12, /* 40 44 - Minus Magnitude */
    SPEC_COM    = 0x13, /* 40 46 - Complement */
    SPEC_DIB    = 0x14, /* 40 50 - Discrete Input B */
    SPEC_DIA    = 0x15, /* 40 52 - Discrete Input A */
    SPEC_GPT    = 0x18, /* 40 60 - Generate Parity (D37C) */
    SPEC_EFC    = 0x19, /* 40 62 - Enable Fine Countdown */
    SPEC_HFC    = 0x18, /* 40 60 - Halt Fine Countdown */
    SPEC_LPR    = 0x1E, /* 40 7x - Load Phase Register */
} d17b_special_t;

/* Shift instruction sub-opcodes (when primary opcode is 0x0/00) */
typedef enum {
    SHIFT_SAL   = 0x08, /* 00 20 - Split Accumulator Left */
    SHIFT_ALS   = 0x09, /* 00 22 - Accumulator Left Shift */
    SHIFT_SLL   = 0x0A, /* 00 24 - Split Left, Left */
    SHIFT_ALC   = 0x0B, /* 00 26 - Accumulator Left Cycle/Rotate (D37C) */
    SHIFT_SRL   = 0x0B, /* 00 26 - Split Right, Left (D17B) - same slot as ALC */
    SHIFT_SAR   = 0x0C, /* 00 30 - Split Accumulator Right */
    SHIFT_ARS   = 0x0D, /* 00 32 - Accumulator Right Shift */
    SHIFT_SLR   = 0x0E, /* 00 34 - Split Left, Right */
    SHIFT_ARC   = 0x0F, /* 00 36 - Accumulator Right Cycle/Rotate (D37C) */
    SHIFT_SRR   = 0x0F, /* 00 36 - Split Right, Right (D17B) - same slot as ARC */
    SHIFT_COA   = 0x10, /* 00 40 - Character Output A */
} d17b_shift_t;

/* CPU state structure */
typedef struct {
    /* Main registers - all 24-bit */
    uint32_t A;         /* Accumulator */
    uint32_t L;         /* Lower Accumulator */
    uint32_t N;         /* Number register (internal, for multiply) */
    uint32_t I;         /* Instruction register / Location counter */

    /* Phase register - 3 bits (8 states) */
    uint8_t P;          /* Phase register (0-7) */

    /* Rapid-access loops */
    uint32_t U;                     /* U-loop (1 word) */
    uint32_t F[F_LOOP_SIZE];        /* F-loop (4 words) */
    uint32_t E[E_LOOP_SIZE];        /* E-loop (8 words) */
    uint32_t H[H_LOOP_SIZE];        /* H-loop (16 words) */
    uint32_t V[V_LOOP_SIZE];        /* V-loop (4 words, incremental input) */
    uint32_t R[R_LOOP_SIZE];        /* R-loop (4 words, resolver input) */

    /* Main disc memory - organized as channels x sectors */
    uint32_t memory[CHANNELS][SECTORS];

    /* Disc position tracking */
    uint32_t current_sector;        /* Current sector (0-127) */
    uint64_t cycle_count;           /* Total word times elapsed */

    /* Status flags */
    bool halted;                    /* Computer is halted */
    bool error;                     /* Error condition */
    bool d37c_mode;                 /* D37C mode: enables DIV, ORA, rotates, TZE */

    /* I/O state */
    uint32_t discrete_in_a;         /* Discrete input A (24 bits) */
    uint32_t discrete_in_b;         /* Discrete input B (24 bits) */
    uint32_t discrete_out_a;        /* Discrete output A (32 bits) */
    int16_t voltage_out[4];         /* Voltage outputs A-C (±10V as ±32767) */
    uint8_t binary_out[4];          /* Binary outputs A-C */

    /* Detector and countdown */
    bool detector;                  /* Detector input state */
    uint32_t fine_countdown;        /* Fine countdown timer */
    bool countdown_enabled;         /* Countdown running */

} d17b_cpu_t;

/* Function prototypes */

/* Initialization */
void d17b_init(d17b_cpu_t *cpu);
void d17b_reset(d17b_cpu_t *cpu);

/* Memory access */
uint32_t d17b_read(d17b_cpu_t *cpu, uint8_t channel, uint8_t sector);
void d17b_write(d17b_cpu_t *cpu, uint8_t channel, uint8_t sector, uint32_t value);

/* Execution */
int d17b_step(d17b_cpu_t *cpu);
int d17b_run(d17b_cpu_t *cpu, uint64_t max_cycles);

/* Instruction execution */
void d17b_exec_arithmetic(d17b_cpu_t *cpu, uint32_t instruction);
void d17b_exec_shift(d17b_cpu_t *cpu, uint32_t instruction);
void d17b_exec_control(d17b_cpu_t *cpu, uint32_t instruction);
void d17b_exec_io(d17b_cpu_t *cpu, uint32_t instruction);
void d17b_exec_special(d17b_cpu_t *cpu, uint32_t instruction);

/* Flag store */
void d17b_flag_store(d17b_cpu_t *cpu, uint8_t flag_code, uint32_t value);

/* Utility */
uint32_t d17b_add_24bit(uint32_t a, uint32_t b);
uint32_t d17b_sub_24bit(uint32_t a, uint32_t b);
uint32_t d17b_complement(uint32_t val);
void d17b_multiply(d17b_cpu_t *cpu, uint32_t operand, bool split);
void d17b_divide(d17b_cpu_t *cpu, uint32_t divisor);  /* D37C only */

/* Debug */
void d17b_dump_state(d17b_cpu_t *cpu);
void d17b_disassemble(uint32_t instruction, char *buffer, size_t bufsize);

#endif /* D17B_H */
