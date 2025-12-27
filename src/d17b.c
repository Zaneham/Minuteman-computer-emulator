/*
 * D17B Minuteman I Guidance Computer Emulator
 * Core CPU implementation
 *
 * Copyright 2025 Zane Hambly - Apache 2.0 License
 */

#include <stdio.h>
#include <string.h>
#include "d17b.h"

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void d17b_init(d17b_cpu_t *cpu) {
    memset(cpu, 0, sizeof(d17b_cpu_t));
    d17b_reset(cpu);
}

void d17b_reset(d17b_cpu_t *cpu) {
    /* Clear registers */
    cpu->A = 0;
    cpu->L = 0;
    cpu->N = 0;
    /* I register holds current location: channel in bits 14-9, sector in bits 8-2 */
    cpu->I = 0;  /* Start at channel 00, sector 000 */
    cpu->P = 0;

    /* Clear rapid-access loops */
    cpu->U = 0;
    memset(cpu->F, 0, sizeof(cpu->F));
    memset(cpu->E, 0, sizeof(cpu->E));
    memset(cpu->H, 0, sizeof(cpu->H));
    memset(cpu->V, 0, sizeof(cpu->V));
    memset(cpu->R, 0, sizeof(cpu->R));

    /* Reset disc position */
    cpu->current_sector = 0;
    cpu->cycle_count = 0;

    /* Clear status */
    cpu->halted = false;
    cpu->error = false;
    cpu->d37c_mode = true;  /* Default to D37C mode (superset) */

    /* Clear I/O */
    cpu->discrete_in_a = 0;
    cpu->discrete_in_b = 0;
    cpu->discrete_out_a = 0;
    memset(cpu->voltage_out, 0, sizeof(cpu->voltage_out));
    memset(cpu->binary_out, 0, sizeof(cpu->binary_out));

    cpu->detector = false;
    cpu->fine_countdown = 0;
    cpu->countdown_enabled = false;
}

/* ============================================================================
 * MEMORY ACCESS
 * ============================================================================ */

uint32_t d17b_read(d17b_cpu_t *cpu, uint8_t channel, uint8_t sector) {
    /* Handle rapid-access loops - these are always accessible */
    switch (channel) {
        case CHAN_U_LOOP:  /* 60 octal - U loop (1 word) */
            return cpu->U;

        case CHAN_L_REG:   /* 64 octal - L register */
            return cpu->L;

        case CHAN_F_LOOP:  /* 52 octal - F loop (4 words) */
            return cpu->F[sector & 0x03];

        case CHAN_E_LOOP:  /* 56 octal - E loop (8 words) */
            return cpu->E[sector & 0x07];

        case CHAN_H_LOOP:  /* 54 octal - H loop (16 words) */
            return cpu->H[sector & 0x0F];

        case CHAN_V_LOOP:  /* 70 octal - V loop (4 words) */
            return cpu->V[sector & 0x03];

        case CHAN_R_LOOP:  /* 72 octal - R loop (4 words) */
            return cpu->R[sector & 0x03];

        default:
            /* Main disc memory - only accessible when sector aligns */
            if (channel < CHANNELS && sector < SECTORS) {
                return cpu->memory[channel][sector];
            }
            return 0;
    }
}

void d17b_write(d17b_cpu_t *cpu, uint8_t channel, uint8_t sector, uint32_t value) {
    value &= WORD_MASK;  /* Ensure 24-bit */

    switch (channel) {
        case CHAN_U_LOOP:
            cpu->U = value;
            break;

        case CHAN_L_REG:
            cpu->L = value;
            break;

        case CHAN_F_LOOP:
            cpu->F[sector & 0x03] = value;
            break;

        case CHAN_E_LOOP:
            cpu->E[sector & 0x07] = value;
            break;

        case CHAN_H_LOOP:
            cpu->H[sector & 0x0F] = value;
            break;

        case CHAN_V_LOOP:
            cpu->V[sector & 0x03] = value;
            break;

        case CHAN_R_LOOP:
            cpu->R[sector & 0x03] = value;
            break;

        default:
            if (channel < CHANNELS && sector < SECTORS) {
                cpu->memory[channel][sector] = value;
            }
            break;
    }
}

/* ============================================================================
 * 24-BIT ARITHMETIC (Sign-Magnitude)
 * ============================================================================
 *
 * The D17B uses sign-magnitude representation:
 *   Bit 23: Sign (0 = positive, 1 = negative)
 *   Bits 22-0: Magnitude
 *
 * This is NOT two's complement!
 */

static inline int32_t to_signed(uint32_t val) {
    if (val & SIGN_BIT) {
        return -(int32_t)(val & MAGNITUDE_MASK);
    }
    return (int32_t)(val & MAGNITUDE_MASK);
}

static inline uint32_t from_signed(int32_t val) {
    if (val < 0) {
        return SIGN_BIT | ((-val) & MAGNITUDE_MASK);
    }
    return val & MAGNITUDE_MASK;
}

uint32_t d17b_add_24bit(uint32_t a, uint32_t b) {
    int32_t sa = to_signed(a);
    int32_t sb = to_signed(b);
    int32_t result = sa + sb;

    /* Clamp to 23-bit magnitude range */
    if (result > (int32_t)MAGNITUDE_MASK) {
        result = MAGNITUDE_MASK;
    } else if (result < -(int32_t)MAGNITUDE_MASK) {
        result = -(int32_t)MAGNITUDE_MASK;
    }

    return from_signed(result);
}

uint32_t d17b_sub_24bit(uint32_t a, uint32_t b) {
    int32_t sa = to_signed(a);
    int32_t sb = to_signed(b);
    int32_t result = sa - sb;

    if (result > (int32_t)MAGNITUDE_MASK) {
        result = MAGNITUDE_MASK;
    } else if (result < -(int32_t)MAGNITUDE_MASK) {
        result = -(int32_t)MAGNITUDE_MASK;
    }

    return from_signed(result);
}

uint32_t d17b_complement(uint32_t val) {
    /* Toggle sign bit */
    return val ^ SIGN_BIT;
}

void d17b_multiply(d17b_cpu_t *cpu, uint32_t operand, bool split) {
    /*
     * Multiply: A * operand -> A:L (48-bit result)
     * Split multiply uses only 10-bit operands
     */
    int32_t a = to_signed(cpu->A);
    int32_t b = to_signed(operand);

    if (split) {
        /* Split word: use only bits 23-14 and 11-2 */
        a = (a >> 14) & 0x3FF;
        if (cpu->A & SIGN_BIT) a = -a;
        b = (b >> 14) & 0x3FF;
        if (operand & SIGN_BIT) b = -b;
    }

    int64_t product = (int64_t)a * (int64_t)b;

    /* Result goes to A (high) and L (low) */
    if (product < 0) {
        product = -product;
        cpu->A = SIGN_BIT | ((product >> 23) & MAGNITUDE_MASK);
        cpu->L = product & MAGNITUDE_MASK;
    } else {
        cpu->A = (product >> 23) & MAGNITUDE_MASK;
        cpu->L = product & MAGNITUDE_MASK;
    }
}

void d17b_divide(d17b_cpu_t *cpu, uint32_t divisor) {
    /*
     * D37C Division: A:L / divisor -> A (quotient), L (remainder)
     *
     * The dividend is the 46-bit value in A:L (A is high, L is low).
     * IMPORTANT: |divisor| must be > |A| for valid results.
     */
    if ((divisor & MAGNITUDE_MASK) == 0) {
        /* Division by zero - set error flag */
        cpu->error = true;
        return;
    }

    /* Get signs */
    bool dividend_neg = (cpu->A & SIGN_BIT) != 0;
    bool divisor_neg = (divisor & SIGN_BIT) != 0;
    bool quotient_neg = dividend_neg ^ divisor_neg;

    /* Work with magnitudes */
    uint64_t dividend = ((uint64_t)(cpu->A & MAGNITUDE_MASK) << 23) |
                        (cpu->L & MAGNITUDE_MASK);
    uint32_t div_mag = divisor & MAGNITUDE_MASK;

    /* Perform division */
    uint64_t quotient = dividend / div_mag;
    uint64_t remainder = dividend % div_mag;

    /* Check for overflow (quotient must fit in 23 bits) */
    if (quotient > MAGNITUDE_MASK) {
        cpu->error = true;
        quotient = MAGNITUDE_MASK;  /* Saturate */
    }

    /* Apply signs */
    cpu->A = (uint32_t)quotient & MAGNITUDE_MASK;
    if (quotient_neg && quotient != 0) {
        cpu->A |= SIGN_BIT;
    }

    cpu->L = (uint32_t)remainder & MAGNITUDE_MASK;
    if (dividend_neg && remainder != 0) {
        cpu->L |= SIGN_BIT;
    }
}

/* ============================================================================
 * FLAG STORE
 * ============================================================================
 *
 * The flag store feature allows storing A to a rapid-access loop
 * simultaneously with instruction execution.
 *
 * Flag codes:
 *   00 - No flag operation
 *   02 - F-loop (selected by last 2 bits of operand)
 *   04 - Telemetry output
 *   06 - Channel 50 (modifiable)
 *   10 - E-loop (selected by last 3 bits)
 *   12 - L-register
 *   14 - H-loop (selected by last 4 bits)
 *   16 - U-loop
 */

void d17b_flag_store(d17b_cpu_t *cpu, uint8_t flag_code, uint32_t operand_sector) {
    uint32_t value = cpu->A;

    switch (flag_code) {
        case 0x00:  /* No flag operation */
            break;

        case 0x02:  /* F-loop */
            cpu->F[operand_sector & 0x03] = value;
            break;

        case 0x04:  /* Telemetry output */
            /* TODO: Trigger telemetry timing signal */
            break;

        case 0x06:  /* Channel 50 (modifiable memory) */
            d17b_write(cpu, 0x28, (operand_sector - 2) & 0x7F, value);
            break;

        case 0x08:  /* E-loop */
            cpu->E[operand_sector & 0x07] = value;
            break;

        case 0x0A:  /* L-register */
            cpu->L = value;
            break;

        case 0x0C:  /* H-loop */
            cpu->H[operand_sector & 0x0F] = value;
            break;

        case 0x0E:  /* U-loop */
            cpu->U = value;
            break;

        default:
            break;
    }
}

/* ============================================================================
 * INSTRUCTION EXECUTION
 * ============================================================================ */

void d17b_exec_arithmetic(d17b_cpu_t *cpu, uint32_t instr) {
    uint8_t opcode = GET_OPCODE(instr);
    uint8_t channel = GET_CHANNEL(instr);
    uint8_t sector = GET_SECTOR(instr);
    uint32_t operand = d17b_read(cpu, channel, sector);

    /* Handle flag store if flag bit set */
    if (GET_FLAG(instr)) {
        d17b_flag_store(cpu, GET_FLAG_CODE(instr), sector);
    }

    switch (opcode) {
        case OP_CLA:  /* 44 - Clear and Add */
            cpu->A = operand;
            break;

        case OP_ADD:  /* 64 - Add */
            cpu->A = d17b_add_24bit(cpu->A, operand);
            break;

        case OP_SUB:  /* 74 - Subtract */
            cpu->A = d17b_sub_24bit(cpu->A, operand);
            break;

        case OP_SAD:  /* 60 - Split Add (operates on halves) */
            /* Split word format: bits 23-14 and 11-2 are two 10-bit values */
            {
                uint32_t a_hi = (cpu->A >> 12) & 0xFFF;
                uint32_t a_lo = cpu->A & 0xFFF;
                uint32_t o_hi = (operand >> 12) & 0xFFF;
                uint32_t o_lo = operand & 0xFFF;
                cpu->A = ((a_hi + o_hi) << 12) | ((a_lo + o_lo) & 0xFFF);
                cpu->A &= WORD_MASK;
            }
            break;

        case OP_SSU:  /* 70 - Split Subtract */
            {
                uint32_t a_hi = (cpu->A >> 12) & 0xFFF;
                uint32_t a_lo = cpu->A & 0xFFF;
                uint32_t o_hi = (operand >> 12) & 0xFFF;
                uint32_t o_lo = operand & 0xFFF;
                cpu->A = ((a_hi - o_hi) << 12) | ((a_lo - o_lo) & 0xFFF);
                cpu->A &= WORD_MASK;
            }
            break;

        case OP_MPY:  /* 24 - Multiply */
            d17b_multiply(cpu, operand, false);
            break;

        case OP_SMP:  /* 20 - Split Multiply */
            d17b_multiply(cpu, operand, true);
            break;

        case OP_DIV_MPM:  /* 34 - Divide (D37C) / Multiply Magnitude (D17B) */
            if (cpu->d37c_mode) {
                /* D37C: DIV - Hardware division */
                d17b_divide(cpu, operand);
            } else {
                /* D17B: MPM - Multiply Magnitude */
                uint32_t abs_a = cpu->A & MAGNITUDE_MASK;
                uint32_t abs_op = operand & MAGNITUDE_MASK;
                cpu->A = abs_a;
                d17b_multiply(cpu, abs_op, false);
            }
            break;

        case OP_STO:  /* 54 - Store Accumulator */
            d17b_write(cpu, channel, sector, cpu->A);
            break;

        default:
            break;
    }
}

void d17b_exec_shift(d17b_cpu_t *cpu, uint32_t instr) {
    uint8_t sector = GET_SECTOR(instr);
    uint8_t sub_op = (sector >> 3) & 0x1F;  /* Bits that determine shift type */
    uint8_t shift_count = sector & 0x07;    /* Low 3 bits = shift count */

    if (shift_count == 0) shift_count = 8;  /* 0 means shift 8 */

    switch (sub_op) {
        case 0x08:  /* SAL - Split Accumulator Left */
            /* Shift each 12-bit half separately */
            {
                uint32_t hi = (cpu->A >> 12) & 0xFFF;
                uint32_t lo = cpu->A & 0xFFF;
                hi = (hi << shift_count) & 0xFFF;
                lo = (lo << shift_count) & 0xFFF;
                cpu->A = (hi << 12) | lo;
            }
            break;

        case 0x09:  /* ALS - Accumulator Left Shift */
            cpu->A = (cpu->A << shift_count) & WORD_MASK;
            break;

        case 0x0A:  /* SLL - Split Left, Left shift */
            {
                uint32_t hi = (cpu->A >> 12) & 0xFFF;
                hi = (hi << shift_count) & 0xFFF;
                cpu->A = (hi << 12) | (cpu->A & 0xFFF);
            }
            break;

        case 0x0B:  /* ALC (D37C) / SRL (D17B) */
            if (cpu->d37c_mode) {
                /* ALC - Accumulator Left Cycle (Rotate) */
                uint32_t val = cpu->A & WORD_MASK;
                cpu->A = ((val << shift_count) | (val >> (24 - shift_count))) & WORD_MASK;
            } else {
                /* SRL - Split Right, Left shift */
                uint32_t lo = cpu->A & 0xFFF;
                lo = (lo << shift_count) & 0xFFF;
                cpu->A = (cpu->A & 0xFFF000) | lo;
            }
            break;

        case 0x0C:  /* SAR - Split Accumulator Right */
            {
                uint32_t hi = (cpu->A >> 12) & 0xFFF;
                uint32_t lo = cpu->A & 0xFFF;
                hi = hi >> shift_count;
                lo = lo >> shift_count;
                cpu->A = (hi << 12) | lo;
            }
            break;

        case 0x0D:  /* ARS - Accumulator Right Shift */
            cpu->A = cpu->A >> shift_count;
            break;

        case 0x0E:  /* SLR - Split Left, Right shift */
            {
                uint32_t hi = (cpu->A >> 12) & 0xFFF;
                hi = hi >> shift_count;
                cpu->A = (hi << 12) | (cpu->A & 0xFFF);
            }
            break;

        case 0x0F:  /* ARC (D37C) / SRR (D17B) */
            if (cpu->d37c_mode) {
                /* ARC - Accumulator Right Cycle (Rotate) */
                uint32_t val = cpu->A & WORD_MASK;
                cpu->A = ((val >> shift_count) | (val << (24 - shift_count))) & WORD_MASK;
            } else {
                /* SRR - Split Right, Right shift */
                uint32_t lo = cpu->A & 0xFFF;
                lo = lo >> shift_count;
                cpu->A = (cpu->A & 0xFFF000) | lo;
            }
            break;

        case 0x10:  /* COA - Character Output A */
            /* Output 4-bit character */
            /* TODO: Implement character output */
            break;

        default:
            break;
    }
}

void d17b_exec_control(d17b_cpu_t *cpu, uint32_t instr) {
    /* Note: TRA and TMI are handled directly in d17b_step */
    uint8_t opcode = GET_OPCODE(instr);
    uint8_t channel = GET_CHANNEL(instr);
    uint8_t sector = GET_SECTOR(instr);

    switch (opcode) {
        case OP_SCL:  /* 04 - Split Compare and Limit */
            {
                uint32_t operand = d17b_read(cpu, channel, sector);
                /*
                 * SCL compares split words and limits the result.
                 * If |A_hi| > |operand_hi| then A_hi = sign(A_hi) * |operand_hi|
                 * Same for low halves.
                 */
                int16_t a_hi = (cpu->A >> 12) & 0xFFF;
                int16_t a_lo = cpu->A & 0xFFF;
                int16_t o_hi = (operand >> 12) & 0xFFF;
                int16_t o_lo = operand & 0xFFF;

                /* Sign extend 12-bit to 16-bit */
                if (a_hi & 0x800) a_hi |= 0xF000;
                if (a_lo & 0x800) a_lo |= 0xF000;
                if (o_hi & 0x800) o_hi |= 0xF000;
                if (o_lo & 0x800) o_lo |= 0xF000;

                /* Limit */
                if (a_hi > o_hi) a_hi = o_hi;
                if (a_hi < -o_hi) a_hi = -o_hi;
                if (a_lo > o_lo) a_lo = o_lo;
                if (a_lo < -o_lo) a_lo = -o_lo;

                cpu->A = ((a_hi & 0xFFF) << 12) | (a_lo & 0xFFF);
            }
            break;

        default:
            break;
    }
}

void d17b_exec_special(d17b_cpu_t *cpu, uint32_t instr) {
    uint8_t sector = GET_SECTOR(instr);
    uint8_t sub_op = (sector >> 1) & 0x3F;

    switch (sub_op) {
        case 0x10:  /* ORA - OR to Accumulator (D37C only) */
            if (cpu->d37c_mode) {
                cpu->A = cpu->A | cpu->L;
            }
            break;

        case 0x11:  /* ANA - AND to Accumulator */
            cpu->A = cpu->A & cpu->L;
            break;

        case 0x12:  /* MIM - Minus Magnitude */
            cpu->A = SIGN_BIT | (cpu->A & MAGNITUDE_MASK);
            break;

        case 0x13:  /* COM - Complement */
            cpu->A = d17b_complement(cpu->A);
            break;

        case 0x09:  /* HPR - Halt and Proceed */
            cpu->halted = true;
            break;

        case 0x08:  /* RSD - Reset Detector */
            cpu->detector = false;
            break;

        case 0x19:  /* EFC - Enable Fine Countdown */
            cpu->countdown_enabled = true;
            break;

        case 0x18:  /* HFC - Halt Fine Countdown */
            cpu->countdown_enabled = false;
            break;

        case 0x1E:  /* LPR - Load Phase Register */
        case 0x1F:
            cpu->P = sector & 0x07;
            break;

        /* I/O instructions */
        case 0x15:  /* DIA - Discrete Input A */
            cpu->A = cpu->discrete_in_a;
            break;

        case 0x14:  /* DIB - Discrete Input B */
            cpu->A = cpu->discrete_in_b;
            break;

        case 0x0B:  /* DOA - Discrete Output A */
            cpu->discrete_out_a = cpu->A;
            break;

        case 0x0C:  /* VOA - Voltage Output A */
            cpu->voltage_out[0] = (int16_t)to_signed(cpu->A >> 15);
            break;

        case 0x0D:  /* VOB - Voltage Output B */
            cpu->voltage_out[1] = (int16_t)to_signed(cpu->A >> 15);
            break;

        case 0x0E:  /* VOC - Voltage Output C */
            cpu->voltage_out[2] = (int16_t)to_signed(cpu->A >> 15);
            break;

        case 0x04:  /* BOA - Binary Output A */
            cpu->binary_out[0] = (cpu->A >> 22) & 0x03;
            break;

        case 0x05:  /* BOB - Binary Output B */
            cpu->binary_out[1] = (cpu->A >> 22) & 0x03;
            break;

        case 0x01:  /* BOC - Binary Output C */
            cpu->binary_out[2] = (cpu->A >> 22) & 0x03;
            break;

        default:
            break;
    }
}

/* ============================================================================
 * MAIN EXECUTION LOOP
 * ============================================================================ */

int d17b_step(d17b_cpu_t *cpu) {
    if (cpu->halted) {
        return -1;
    }

    /* Fetch instruction from I-register location */
    uint8_t channel = (cpu->I >> 9) & 0x3F;
    uint8_t sector = (cpu->I >> 2) & 0x7F;
    uint32_t instr = d17b_read(cpu, channel, sector);

    uint8_t opcode = GET_OPCODE(instr);
    uint8_t next_sp = GET_SP(instr);  /* Next sector pointer (low 4 bits) */
    bool jumped = false;

    /* Dispatch based on opcode */
    switch (opcode) {
        case OP_SHIFT:
            d17b_exec_shift(cpu, instr);
            break;

        case OP_SPECIAL:
            d17b_exec_special(cpu, instr);
            break;

        case OP_TRA:
            /* TRA sets I directly */
            {
                uint8_t target_ch = GET_CHANNEL(instr);
                uint8_t target_sec = GET_SECTOR(instr);
                cpu->I = (target_ch << 9) | (target_sec << 2);
                jumped = true;
            }
            break;

        case OP_TMI_TZE:  /* Opcode 10: TZE (D37C) / TMI (D17B) */
            if (cpu->d37c_mode) {
                /* TZE - Transfer on Zero: jump if A == 0 */
                if ((cpu->A & MAGNITUDE_MASK) == 0) {
                    uint8_t target_ch = GET_CHANNEL(instr);
                    uint8_t target_sec = GET_SECTOR(instr);
                    cpu->I = (target_ch << 9) | (target_sec << 2);
                    jumped = true;
                }
            } else {
                /* TMI - Transfer on Minus: jump if A < 0 */
                if (cpu->A & SIGN_BIT) {
                    uint8_t target_ch = GET_CHANNEL(instr);
                    uint8_t target_sec = GET_SECTOR(instr);
                    cpu->I = (target_ch << 9) | (target_sec << 2);
                    jumped = true;
                }
            }
            break;

        case OP_TMI:  /* Opcode 30: TMI - Transfer on Minus (both modes) */
            if (cpu->A & SIGN_BIT) {
                uint8_t target_ch = GET_CHANNEL(instr);
                uint8_t target_sec = GET_SECTOR(instr);
                cpu->I = (target_ch << 9) | (target_sec << 2);
                jumped = true;
            }
            break;

        case OP_SCL:
            d17b_exec_control(cpu, instr);
            break;

        default:
            d17b_exec_arithmetic(cpu, instr);
            break;
    }

    /* Advance to next instruction if we didn't jump */
    if (!jumped) {
        /*
         * The Sp field (4 bits) specifies the low 4 bits of the next sector.
         * For simplicity, we just use Sp directly as the next sector.
         * A full emulator would wait for disc rotation to match.
         */
        cpu->I = (channel << 9) | (next_sp << 2);
    }

    /* Advance disc position */
    cpu->current_sector = (cpu->current_sector + 1) & 0x7F;
    cpu->cycle_count++;

    /* Update fine countdown if enabled */
    if (cpu->countdown_enabled && cpu->fine_countdown > 0) {
        cpu->fine_countdown--;
    }

    return 0;
}

int d17b_run(d17b_cpu_t *cpu, uint64_t max_cycles) {
    uint64_t start = cpu->cycle_count;

    while (!cpu->halted && (cpu->cycle_count - start) < max_cycles) {
        if (d17b_step(cpu) < 0) {
            break;
        }
    }

    return cpu->halted ? -1 : 0;
}

/* ============================================================================
 * DEBUG UTILITIES
 * ============================================================================ */

void d17b_dump_state(d17b_cpu_t *cpu) {
    printf("=== D17B CPU State ===\n");
    printf("A:  %08o (%+d)\n", cpu->A, to_signed(cpu->A));
    printf("L:  %08o\n", cpu->L);
    printf("I:  %08o (CH:%02o SEC:%03o)\n",
           cpu->I, (cpu->I >> 9) & 0x3F, (cpu->I >> 2) & 0x7F);
    printf("P:  %d\n", cpu->P);
    printf("U:  %08o\n", cpu->U);
    printf("Cycles: %llu\n", (unsigned long long)cpu->cycle_count);
    printf("Halted: %s\n", cpu->halted ? "YES" : "NO");
    printf("\nF-loop: ");
    for (int i = 0; i < F_LOOP_SIZE; i++) printf("%08o ", cpu->F[i]);
    printf("\nE-loop: ");
    for (int i = 0; i < E_LOOP_SIZE; i++) printf("%08o ", cpu->E[i]);
    printf("\nH-loop: ");
    for (int i = 0; i < 4; i++) printf("%08o ", cpu->H[i]);
    printf("...\n");
}

static const char* opcode_names[] = {
    "SHIFT", "SCL", "TMI", "???", "SMP", "MPY", "TMI", "MPM",
    "SPEC", "CLA", "TRA", "STO", "SAD", "ADD", "SSU", "SUB"
};

void d17b_disassemble(uint32_t instr, char *buffer, size_t bufsize) {
    uint8_t opcode = GET_OPCODE(instr);
    uint8_t flag = GET_FLAG(instr);
    uint8_t channel = GET_CHANNEL(instr);
    uint8_t sector = GET_SECTOR(instr);

    snprintf(buffer, bufsize, "%s%s %02o,%03o",
             opcode_names[opcode],
             flag ? "*" : "",
             channel, sector);
}
