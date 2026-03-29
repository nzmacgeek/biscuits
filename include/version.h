#pragma once
// BlueyOS Version Information
// "It's a big day!" - Bluey, every single episode
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#define BLUEYOS_VERSION_MAJOR  0
#define BLUEYOS_VERSION_MINOR  1
#define BLUEYOS_VERSION_PATCH  0

#ifndef BLUEYOS_BUILD_NUMBER
#define BLUEYOS_BUILD_NUMBER   1
#endif

// Build host/user injected by Makefile via -D flags
#ifndef BLUEYOS_BUILD_HOST
#define BLUEYOS_BUILD_HOST  "unknown-host"
#endif
#ifndef BLUEYOS_BUILD_USER
#define BLUEYOS_BUILD_USER  "unknown-user"
#endif

#define BLUEYOS_BUILD_DATE     __DATE__
#define BLUEYOS_BUILD_TIME     __TIME__

// Stringify helpers
#define _BLUEY_STR2(x) #x
#define _BLUEY_STR(x)  _BLUEY_STR2(x)

#define BLUEYOS_BUILD_NUMBER_STR  _BLUEY_STR(BLUEYOS_BUILD_NUMBER)
#define BLUEYOS_VERSION_STRING    "BlueyOS v0.1.0 (Build #" BLUEYOS_BUILD_NUMBER_STR ")"
#define BLUEYOS_CODENAME          "Bandit"

// "Dad, are you the best dad in the whole world?" - Bluey
// "I reckon, yeah." - Bandit
