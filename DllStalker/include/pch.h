// pch.h

#ifndef PCH_H
#define PCH_H

#if defined(_DEBUG)
    #ifndef ENABLE_DUMPER
        #define ENABLE_DUMPER
    #endif
#else
    // Relase build
#endif

#include "framework.h"

#endif //PCH_H
