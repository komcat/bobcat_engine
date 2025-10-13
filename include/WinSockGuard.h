#pragma once

// This header ensures WinSock2 is included before Windows.h
#ifdef _WIN32
#ifndef _WINSOCK2_INCLUDED_GUARD
#define _WINSOCK2_INCLUDED_GUARD

// Define WIN32_LEAN_AND_MEAN to exclude rarely-used stuff
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Include WinSock2 first
#include <winsock2.h>
#include <ws2tcpip.h>

// Then Windows.h
#include <windows.h>

// Prevent Windows.h from including winsock.h
#define _WINSOCKAPI_
#endif
#endif