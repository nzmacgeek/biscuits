#pragma once
// BlueyOS Shell - "Bluey's Command Post"
// "I'm in charge!" - Bluey Heeler, every single episode
// Episode ref: "Camping" - Bluey sets up base camp and gives everyone a job
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.
#include "../include/types.h"

#define SHELL_LINE_MAX   256
#define SHELL_ARGS_MAX   16
#define SHELL_CWD_MAX    256
#define SHELL_HIST_MAX   32

void shell_init(void);
void shell_run(void);   /* never returns */
