/*
 * D17B Minuteman I Guidance Computer Emulator
 * Main entry point and test harness
 *
 * Copyright 2025 Zane Hambly - Apache 2.0 License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d17b.h"

/*
 * D17B Instruction Encoding Helper
 *
 * Format (24 bits):
 *   T23-T20: OPCODE (4 bits)
 *   T19:     FLAG (1 bit)
 *   T18-T15: Sp - next sector low 4 bits (4 bits)
 *   T14-T9:  Channel (6 bits)
 *   T8-T2:   Sector (7 bits)
 *   T1-T0:   unused (2 bits)
 *
 * Opcodes (4-bit):
 *   CLA = 9 (0x9), ADD = 13 (0xD), SUB = 15 (0xF)
 *   TRA = 10 (0xA), STO = 11 (0xB), SPECIAL = 8 (0x8)
 */
#define ENCODE_INSTR(op, flag, sp, chan, sec) \
    ((((op) & 0xF) << 20) | (((flag) & 0x1) << 19) | \
     (((sp) & 0xF) << 15) | (((chan) & 0x3F) << 9) | (((sec) & 0x7F) << 2))

/* Load a simple test program */
static void load_test_program(d17b_cpu_t *cpu) {
    /*
     * Test program: Add two numbers and halt
     *
     * Channel 00:
     *   Sector 000: CLA 00,001 ; Load operand A     -> next=002
     *   Sector 001: 00000005   ; Data: 5
     *   Sector 002: ADD 00,003 ; Add operand B      -> next=004
     *   Sector 003: 00000003   ; Data: 3
     *   Sector 004: STO 00,006 ; Store result       -> next=005
     *   Sector 005: HPR        ; Halt
     *   Sector 006: 00000000   ; Result location
     *
     * Expected: A = 5 + 3 = 8 (octal 10)
     */

    /* CLA 00,001 - opcode 9, next sector 002 */
    cpu->memory[0][0] = ENCODE_INSTR(0x9, 0, 2, 0, 1);

    /* Data: 5 */
    cpu->memory[0][1] = 0x000005;

    /* ADD 00,003 - opcode 13 (0xD), next sector 004 */
    cpu->memory[0][2] = ENCODE_INSTR(0xD, 0, 4, 0, 3);

    /* Data: 3 */
    cpu->memory[0][3] = 0x000003;

    /* STO 00,006 - opcode 11 (0xB), next sector 005 */
    cpu->memory[0][4] = ENCODE_INSTR(0xB, 0, 5, 0, 6);

    /* HPR - Special opcode 8, sector field = 22 octal = 18 decimal */
    cpu->memory[0][5] = ENCODE_INSTR(0x8, 0, 6, 0, 18);

    /* Result location */
    cpu->memory[0][6] = 0x000000;
}

/* Interactive mode */
static void run_interactive(d17b_cpu_t *cpu) {
    char cmd[256];
    char disasm[64];

    printf("D17B Emulator - Interactive Mode\n");
    printf("Commands: s(tep), r(un), d(ump), q(uit), l(oad addr), m(emory addr)\n\n");

    while (1) {
        /* Show current instruction */
        uint8_t ch = (cpu->I >> 9) & 0x3F;
        uint8_t sec = (cpu->I >> 2) & 0x7F;
        uint32_t instr = d17b_read(cpu, ch, sec);
        d17b_disassemble(instr, disasm, sizeof(disasm));

        printf("[%02o:%03o] %08o  %s\n", ch, sec, instr, disasm);
        printf("> ");

        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        switch (cmd[0]) {
            case 's':  /* Step */
                d17b_step(cpu);
                if (cpu->halted) {
                    printf("*** HALTED ***\n");
                }
                break;

            case 'r':  /* Run */
                printf("Running...\n");
                d17b_run(cpu, 10000);
                if (cpu->halted) {
                    printf("*** HALTED after %llu cycles ***\n",
                           (unsigned long long)cpu->cycle_count);
                }
                break;

            case 'd':  /* Dump state */
                d17b_dump_state(cpu);
                break;

            case 'l':  /* Load address */
                {
                    unsigned int addr;
                    if (sscanf(cmd + 1, "%o", &addr) == 1) {
                        cpu->I = addr << 2;
                        printf("Set I to %08o\n", cpu->I);
                    }
                }
                break;

            case 'm':  /* Memory dump */
                {
                    unsigned int ch, sec;
                    if (sscanf(cmd + 1, "%o %o", &ch, &sec) == 2) {
                        for (int i = 0; i < 8 && (sec + i) < SECTORS; i++) {
                            printf("  [%02o:%03o] %08o\n",
                                   ch, sec + i, cpu->memory[ch][sec + i]);
                        }
                    }
                }
                break;

            case 'q':
                printf("Goodbye.\n");
                return;

            case '\n':
                break;

            default:
                printf("Unknown command: %c\n", cmd[0]);
                break;
        }
    }
}

/* Simple automated test */
static int run_test(void) {
    d17b_cpu_t cpu;

    printf("D17B Emulator - Automated Test\n");
    printf("===============================\n\n");

    d17b_init(&cpu);
    load_test_program(&cpu);

    printf("Initial state:\n");
    d17b_dump_state(&cpu);

    printf("\nRunning test program (5 + 3 = ?)...\n\n");

    /* Run until halt */
    d17b_run(&cpu, 1000);

    printf("Final state:\n");
    d17b_dump_state(&cpu);

    /* Check result */
    uint32_t result = cpu.memory[0][6];
    printf("\nResult at [00:006]: %08o (%d)\n", result, result);

    if (result == 8) {
        printf("\n*** TEST PASSED: 5 + 3 = 8 ***\n");
    } else {
        printf("\n*** TEST FAILED: Expected 8, got %d ***\n", result);
        return 1;
    }

    /* D37C Division Test */
    printf("\n=== D37C HARDWARE DIVISION TEST ===\n");
    printf("Testing: 24 / 4 = 6\n\n");

    d17b_reset(&cpu);
    cpu.d37c_mode = true;  /* Enable D37C mode */

    /*
     * Division test program:
     *   Sector 000: CLA 00,001 ; Load high dividend (0)
     *   Sector 001: 00000000   ; Data: 0 (high part of 24)
     *   Sector 002: STO L      ; Store to L (we'll load L differently)
     *   Actually, simpler: just set up A:L directly and execute DIV
     */

    /* Direct setup: A = 0, L = 24, then DIV by 4 */
    cpu.A = 0;
    cpu.L = 24;  /* Dividend = 24 (in L since A:L forms 46-bit dividend) */

    /* DIV 00,001 - opcode 7 (DIV/MPM), channel 00, sector 001 */
    cpu.memory[0][0] = ENCODE_INSTR(0x7, 0, 2, 0, 1);  /* DIV 00,001 -> next=002 */
    cpu.memory[0][1] = 4;  /* Divisor: 4 */
    cpu.memory[0][2] = ENCODE_INSTR(0x8, 0, 3, 0, 18); /* HPR - halt */

    cpu.I = 0;  /* Start at 00,000 */
    d17b_run(&cpu, 100);

    printf("After DIV: A = %d (quotient), L = %d (remainder)\n",
           cpu.A & MAGNITUDE_MASK, cpu.L & MAGNITUDE_MASK);

    if ((cpu.A & MAGNITUDE_MASK) == 6 && (cpu.L & MAGNITUDE_MASK) == 0) {
        printf("*** D37C DIV TEST PASSED: 24 / 4 = 6 remainder 0 ***\n");
    } else {
        printf("*** D37C DIV TEST FAILED ***\n");
        return 1;
    }

    /* D37C Rotate Test */
    printf("\n=== D37C ROTATE TEST ===\n");
    printf("Testing: ALC (rotate left 1 bit) on 0x800001\n\n");

    d17b_reset(&cpu);
    cpu.A = 0x800001;  /* Bit 23 and bit 0 set */

    /*
     * ALC 1 - rotate left 1 bit, should become 0x000003
     *
     * Shift instruction sector field format:
     *   Bits 6-3: sub-opcode (0x0B = ALC = 26 octal = 010 110)
     *   Bits 2-0: shift count
     *
     * So sector = (0x0B << 3) | shift_count = (11 << 3) | 1 = 89
     * But wait, the sub_op extraction is (sector >> 3) & 0x1F
     * And sub_op 0x0B is for 00 26 which means sector high bits should encode 26
     *
     * Actually in the code: sub_op = (sector >> 3) & 0x1F
     * For sub_op = 0x0B = 11, we need sector >> 3 = 11
     * So sector = (11 << 3) | 1 = 89 (shift count = 1)
     */
    cpu.memory[0][0] = ENCODE_INSTR(0, 0, 1, 0, (0x0B << 3) | 1);  /* ALC shift=1 */
    cpu.memory[0][1] = ENCODE_INSTR(0x8, 0, 2, 0, 18); /* HPR */

    cpu.I = 0;
    d17b_step(&cpu);

    printf("After ALC 1: A = 0x%06X (expected 0x000003)\n", cpu.A);

    if (cpu.A == 0x000003) {
        printf("*** D37C ROTATE TEST PASSED ***\n");
    } else {
        printf("*** D37C ROTATE TEST FAILED ***\n");
        return 1;
    }

    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}

int main(int argc, char *argv[]) {
    printf("\n");
    printf("  ╔═══════════════════════════════════════════════════════╗\n");
    printf("  ║   D17B/D37C MINUTEMAN GUIDANCE COMPUTER EMULATOR      ║\n");
    printf("  ║                                                       ║\n");
    printf("  ║   D17B: Minuteman I (1962) - 39 instructions          ║\n");
    printf("  ║   D37C: Minuteman II/III (1965) - 57 instructions     ║\n");
    printf("  ║                                                       ║\n");
    printf("  ║   400 nuclear missiles still fly on this code         ║\n");
    printf("  ╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    if (argc > 1 && strcmp(argv[1], "-i") == 0) {
        /* Interactive mode */
        d17b_cpu_t cpu;
        d17b_init(&cpu);
        load_test_program(&cpu);
        run_interactive(&cpu);
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "-t") == 0) {
        /* Test mode */
        return run_test();
    } else {
        printf("Usage: %s [-i|-t]\n", argv[0]);
        printf("  -i  Interactive mode\n");
        printf("  -t  Run automated tests\n");
        printf("\nRunning default test...\n\n");
        return run_test();
    }
}
