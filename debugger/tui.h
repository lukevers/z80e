#ifndef DEBUGGER_TUI_H
#define DEBUGGER_TUI_H

#include "asic.h"

char **tui_parse_commandline(const char *, int *);
void tui_tick(asic_t *asic);

#endif
