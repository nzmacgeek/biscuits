#pragma once
// BlueyOS Keyboard Driver - "Bingo's Keyboard - Tap tap tap!"
// Episode ref: "Stories" - Bingo types out all her stories
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

void keyboard_init(void);
char keyboard_getchar(void);   // blocking - waits for a key
int  keyboard_poll(void);      // non-blocking - returns char or -1 if empty
int  keyboard_available(void); // returns 1 if key is waiting in buffer
