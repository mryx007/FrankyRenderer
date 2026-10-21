#include "streams.h"
#include "guidutil.h"

#ifdef _OBJBASE_H_

/*  Stuff for printing out our GUID names */

constexpr GuidStringEntry g_GuidNames[] = {
#define OUR_GUID_ENTRY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
{ { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, #name },
    #include <uuids.h>
};

CGuidNameList GuidNames;
int g_cGuidNames = sizeof(g_GuidNames) / sizeof(g_GuidNames[0]);

LPCSTR CGuidNameList::operator [] (const GUID &guid)
{
    for (int i = 0; i < g_cGuidNames; i++) {
        if (g_GuidNames[i].guid == guid) {
            return g_GuidNames[i].szName;
        }
    }
    if (guid == GUID_NULL) {
        return "GUID_NULL";
    }
    if (guid == GUID{ 0xe487eb08, 0x6b26, 0x4be9, 0x9d, 0xd3, 0x99, 0x34, 0x34, 0xd3, 0x13, 0xfd }) {
        return "MEDIATYPE_Subtitle";
    }

    return "Unknown GUID Name";
}

#ifdef USE_STD_STRING
std::string CGuidNameList::GetString(const GUID& guid)
{
    std::string str(GuidNames[guid]);
    if (str != "Unknown GUID Name") {
        return str;
    }

    if (memcmp(&guid.Data2, &MEDIASUBTYPE_YUY2.Data2, sizeof(GUID) - sizeof(GUID::Data1)) == 0) {
        if (guid.Data1 & 0xFFFF0000) {
            // FOURCC guids {xxxxxxxx-0000-0010-8000-00AA00389B71}
            // but not WAVE guids {0000xxxx-0000-0010-8000-00AA00389B71}
            str = "MEDIASUBTYPE_";
            uint32_t fourcc = guid.Data1;

            for (unsigned i = 0; i < 4; i++) {
                const uint32_t c = fourcc & 0xff;
                if (c > 32) {
                    str += (char)c;
                } else {
                    str += '[';
                    str.append(std::to_string(c));
                    str += ']';
                }
                fourcc >>= 8;
            }
            return str;
        }

        if (m_pStrWaves) {
            // exra WAVE guids {0000xxxx-0000-0010-8000-00AA00389B71}
            const WORD wFormatTag = (WORD)guid.Data1;

            for (size_t i = 0; i < m_nStrWaves; i++) {
                if (m_pStrWaves[i].wFormatTag == wFormatTag) {
                    str = "MEDIASUBTYPE_";
                    str.append(m_pStrWaves[i].szName);

                    return str;
                }
            }
        }
    }
    else if (m_pStrGuids) {
        // exra guids
        for (size_t i = 0; i < m_nStrGuids; i++) {
            if (m_pStrGuids[i].guid == guid) {
                return m_pStrGuids[i].szName;
            }
        }
    }

    std::wstring wstr(39, 0);
    int ret = StringFromGUID2(guid, wstr.data(), wstr.length());
    if (ret == 39) {
        str.assign(wstr.begin(), wstr.end());
    }

    return str;
}

void CGuidNameList::SetExtraGuidStrings(const GuidStringEntry* pStrGuids, const size_t nStrGuids, const WaveStringEntry* pStrWaves, const size_t nStrWaves)
{
    m_pStrGuids = pStrGuids;
    m_nStrGuids = nStrGuids;
    m_pStrWaves = pStrWaves;
    m_nStrWaves = nStrWaves;
}
#endif

#endif /* _OBJBASE_H_ */
