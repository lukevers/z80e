#include "asic.h"
#include "debugger.h"
#include "disassemble.h"
#include "tui.h"
#include "commands.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <signal.h>

#include "hooks.h"

typedef struct {
    ti_device_type device;
    asic_t *device_asic;
    char *rom_file;
    int cycles;
    int print_state;
    int stop;
    int debugger;
    int no_rom_check;
} appContext_t;

appContext_t context;

appContext_t create_context(void) {
    appContext_t context;
    context.device = TI83p;
    context.rom_file = NULL;
    context.cycles = -1;
    context.print_state = 0;
    context.stop = 0;
    context.no_rom_check = 0;
    return context;
}

void setDevice(appContext_t *context, char *target) {
    if (strcasecmp(target, "TI73") == 0) {
        context->device = TI73;
    } else if (strcasecmp(target, "TI83p") == 0) {
        context->device = TI83p;
    } else if (strcasecmp(target, "TI83pSE") == 0) {
        context->device = TI83pSE;
    } else if (strcasecmp(target, "TI84p") == 0) {
        context->device = TI84p;
    } else if (strcasecmp(target, "TI84pSE") == 0) {
        context->device = TI84pSE;
    } else if (strcasecmp(target, "TI84pCSE") == 0) {
        context->device = TI84pCSE;
    } else {
        printf("Incorrect usage. See z80e --help.\n");
        exit(1);
    }
}

struct run_disassemble_state {
    struct disassemble_memory memory;
    debugger_state_t *state;
};

uint8_t run_command_read_byte(struct disassemble_memory *state, uint16_t pointer) {
    struct run_disassemble_state *dstate = (struct run_disassemble_state *)state;

    return ti_read_byte(dstate->state->asic->mmu, pointer);
}

int run_command_write(struct disassemble_memory *state, const char *format, ...) {
    struct run_disassemble_state *dstate = (struct run_disassemble_state *)state;

    va_list list;
    va_start(list, format);

    return dstate->state->vprint(dstate->state, format, list);
}

int command_run(debugger_state_t *state, int argc, char **argv) {
    context.stop = 0;
    uint16_t instructions = -1;

    struct run_disassemble_state dstate;
    dstate.memory.read_byte = run_command_read_byte;
    dstate.memory.current = state->asic->cpu->registers.PC;
    dstate.state = state;

    if ((argc == 2 && strcmp(argv[1], "--help") == 0) || argc > 2) {
        state->print(state, "run [instructions] - run a specified number of instructions\n"
		" If no number is specified, the emulator will run until interrupted (^C).\n");
	return 0;
    } else if(argc == 2) {
        instructions = parse_expression(state, argv[1]);
        context.debugger = 1;
        for (; instructions > 0; instructions--) {
            if (gDebuggerState.echo) {
                state->print(state, "0x%04X: ", state->asic->cpu->registers.PC);
                parse_instruction(&(dstate.memory), run_command_write);
                state->print(state, "\n");
            }
            cpu_execute(context.device_asic->cpu, 1);
        }
        context.debugger = 2;
        return 0;
    }

    context.debugger = 1;
    while (1) {
        if (gDebuggerState.echo) {
            state->print(state, "0x%04X: ", state->asic->cpu->registers.PC);
            parse_instruction(&(dstate.memory), run_command_write);
            state->print(state, "\n");
        }
        cpu_execute(state->asic->cpu, 1);
        if (context.stop) {
            context.debugger = 2;
            return 0;
        }
    }
    context.debugger = 2;
    return 0;
}

int command_step(debugger_state_t *state, int argc, char **argv) {
    char *_argv[] = { "run", "1" };
    return command_run(state, 2, _argv);
}

void print_help(void) {
    printf("z80e - Emulate z80 calculators\n"
           "Usage: z80e [flags] [rom]\n\n"
           "Flags:\n"
           "\t-d <device>: Selects a device to emulate. Available devices:\n"
           "\t\tTI73, TI83p, TI83pSE, TI84p, TI84pSE, TI84pCSE\n"
           "\t-c <cycles>: Emulate this number of cycles, then exit. If omitted, the machine will be emulated indefinitely.\n"
           "\t--print-state: Prints the state of the machine on exit.\n"
           "\t--no-rom-check: Skips the check that ensure the provided ROM file is the correct size.\n"
           "\t--debug: Enable the debugger, which is enabled by interrupting the emulator.\n");
}

void handleFlag(appContext_t *context, char flag, int *i, char **argv) {
    char *next;
    switch (flag) {
        case 'd':
            next = argv[++*i];
            setDevice(context, next);
            break;
        case 'c':
            next = argv[++*i];
            context->cycles = atoi(next);
            break;
        default:
            printf("Incorrect usage. See z80e --help.\n");
            exit(1);
            break;
    }
}

void handleLongFlag(appContext_t *context, char *flag, int *i, char **argv) {
    if (strcasecmp(flag, "device") == 0) {
        char *next = argv[*i++];
        setDevice(context, next);
    } else if (strcasecmp(flag, "print-state") == 0) {
        context->print_state = 1;
    } else if (strcasecmp(flag, "no-rom-check") == 0) {
        context->no_rom_check = 1;
    } else if (strcasecmp(flag, "debug") == 0) {
        context->debugger = 1;
    } else if (strcasecmp(flag, "help") == 0) {
        print_help();
        exit(0);
    } else {
        printf("Incorrect usage. See z80e --help.\n");
        exit(1);
    }
}

void sigint_handler(int sig) {
    signal(SIGINT, sigint_handler);

    printf("\n Caught interrupt, stopping emulation\n");
    context.stop = 1;

    if (context.debugger > 1) {
        exit(0);
    }

    fflush(stdout);
}

int main(int argc, char **argv) {
    context = create_context();
    signal(SIGINT, sigint_handler);

    int i;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (*a == '-') {
            a++;
            if (*a == '-') {
                handleLongFlag(&context, a + 1, &i, argv);
            } else {
                while (*a) {
                    handleFlag(&context, *a++, &i, argv);
                }
            }
        } else {
            if (context.rom_file == NULL) {
                context.rom_file = a;
            } else {
                printf("Incorrect usage. See z80e --help.\n");
                return 1;
            }
        }
    }

    asic_t *device = asic_init(context.device);
    context.device_asic = device;
    if (context.rom_file == NULL) {
        printf("Warning: No ROM file specified\n");
    } else {
        FILE *file = fopen(context.rom_file, "r");
        if (!file) {
            printf("Error opening '%s'.\n", context.rom_file);
            asic_free(device);
            return 1;
        }
        int length;
        fseek(file, 0L, SEEK_END);
        length = ftell(file);
        fseek(file, 0L, SEEK_SET);
        if (!context.no_rom_check && length != device->mmu->settings->flash_pages * 0x4000) {
            printf("Error: This file does not match the required ROM size of %d bytes, but it is %d bytes (use --no-rom-check to override).\n",
		device->mmu->settings->flash_pages * 0x4000, length);
            fclose(file);
            asic_free(device);
            return 1;
        }
        length = fread(device->mmu->flash, 0x4000, device->mmu->settings->flash_pages, file);
        fclose(file);
    }

    init_hooks();
    register_command("run", command_run, NULL);
    register_command("step", command_step, NULL);
    register_hexdump("hexdump", device->mmu);
    register_disassemble("disassemble", device->mmu);
    register_print_registers("print_registers", device->cpu);
    register_print_expression("expression");
    register_stack("stack");
    register_print_mappings("mappings");
    register_on_read("on_read");

    if (context.debugger) {
        context.debugger = 2;
        tui_tick(device);
        context.debugger = 1;
    } else {
        if (context.cycles == -1) { // Run indefinitely
            while (1) {
                // TODO: Timings
                cpu_execute(device->cpu, 1);

                if (context.stop) {
                    break;
                }
            }
        } else {
            cpu_execute(device->cpu, context.cycles);
        }
    }

    if (context.print_state) {
        z80registers_t r = device->cpu->registers;
        printf("   AF: 0x%04X   BC: 0x%04X   DE: 0x%04X  HL: 0x%04X\n", r.AF, r.BC, r.DE, r.HL);
        printf("  'AF: 0x%04X  'BC: 0x%04X  'DE: 0x%04X 'HL: 0x%04X\n", r._AF, r._BC, r._DE, r._HL);
        printf("   PC: 0x%04X   SP: 0x%04X   IX: 0x%04X  IY: 0x%04X\n", r.PC, r.SP, r.IX, r.IY);
        printf("Flags: ");
        if (r.flags.S) printf("S ");
        if (r.flags.Z) printf("Z ");
        if (r.flags.H) printf("H ");
        if (r.flags.PV) printf("P/V ");
        if (r.flags.N) printf("N ");
        if (r.flags.C) printf("C ");
        if (r.F == 0) printf("None set");
        printf("\n");
    }
    asic_free(device);
    return 0;
}
