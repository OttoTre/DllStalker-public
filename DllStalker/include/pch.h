// pch.h: wstępnie skompilowany plik nagłówka.

#ifndef PCH_H
#define PCH_H

#if defined(_DEBUG) || defined(DEBUGRELEASE)
    #ifndef ENABLE_DUMPER
        #define ENABLE_DUMPER
    #endif
#else
    // Release build
#endif

#include "framework.h"

#endif //PCH_H
