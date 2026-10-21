#ifndef __GUIDUTIL__
#define __GUIDUTIL__

#ifdef _OBJBASE_H_

#ifdef USE_STD_STRING
#include <string>
#endif

//  Outputting GUID names.  If you want to include the name
//  associated with a GUID (eg CLSID_...) then
//
//      GuidNames[yourGUID]
//
//  Returns the name defined in uuids.h as a string

struct GuidStringEntry {
    GUID guid;
    LPCSTR szName;
};

struct WaveStringEntry {
    WORD wFormatTag;
    LPCSTR szName;
};

class CGuidNameList {
    static inline const GuidStringEntry* m_pStrGuids = nullptr;
    static inline size_t                 m_nStrGuids = 0;
    static inline const WaveStringEntry* m_pStrWaves = nullptr;
    static inline size_t                 m_nStrWaves = 0;
public:
    LPCSTR operator [] (const GUID& guid);
#ifdef USE_STD_STRING
    std::string GetString(const GUID& guid);
    void SetExtraGuidStrings(const GuidStringEntry* pStrGuids, const size_t nStrGuids, const WaveStringEntry* pStrWaves, const size_t nStrWaves);
#endif
};

extern CGuidNameList GuidNames;

#ifdef USE_STD_STRING
#define GET_GUID_NAME(guid) GuidNames.GetString(guid).c_str()
#else
#define GET_GUID_NAME(guid) GuidNames[guid]
#endif

#endif // _OBJBASE_H_

#endif // __GUIDUTIL__
