#include <TiltedOnlinePCH.h>
#include "ScopedReference.h"
#include <TESObjectREFR.h>

ScopedReference::ScopedReference(uint32_t aHandle) noexcept
{
    // ID 17201 retains the resolved object. Unlike the legacy GetByHandle
    // wrapper, keep that retain until the caller has finished reading/applying.
    using TLookup = bool(uint32_t&, TESObjectREFR*&);
    POINTER_SKYRIMSE(TLookup, lookup, 17201);
    lookup.Get()(aHandle, m_pReference);
}

ScopedReference::~ScopedReference() noexcept
{
    if (m_pReference)
        m_pReference->handleRefObject.DecRefHandle();
}
