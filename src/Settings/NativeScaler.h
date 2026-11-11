#pragma once
#include <cstring>

// Identifiers resolved by a native renderer module before the ordinary
// libplacebo filter path. Keep this list independent of any one implementation
// so upstream can add or remove a native scaler without changing settings or
// presentation policy.
enum class NativeScalerKind
{
    None,
    NguSharp,
};

inline NativeScalerKind ResolveNativeScaler(const char* name)
{
    if (name && !std::strcmp(name, "ngu_sharp"))
        return NativeScalerKind::NguSharp;
    return NativeScalerKind::None;
}

inline bool IsNativeScalerIdentifier(const char* name)
{
    return ResolveNativeScaler(name) != NativeScalerKind::None;
}
