#pragma once
#include <windows.h>

namespace hndcam
{
    // User interface in French when Windows is displayed in French or uses French regional formats
    // (e.g. English Windows set up in Belgium or France), English otherwise (reference language).
    inline bool UiFrench()
    {
        static const bool french = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH
            || PRIMARYLANGID(LANGIDFROMLCID(GetUserDefaultLCID())) == LANG_FRENCH;
        return french;
    }

    inline const wchar_t* Tr(const wchar_t* english, const wchar_t* french) { return UiFrench() ? french : english; }
    inline const char* Tr(const char* english, const char* french) { return UiFrench() ? french : english; } // UTF-8
}
