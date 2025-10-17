#pragma once

#ifdef _WIN32
// Include winsock2 first to get proper types
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// Now define _WINSOCKAPI_ to prevent ACSC.h from including winsock.h
#define _WINSOCKAPI_

// Include ACSC.h - it will skip winsock.h but get the types from winsock2.h
#include "ACSC.h"

#else
#include "ACSC.h"
#endif