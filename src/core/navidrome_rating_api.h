// Vendored copy of foo_navidrome's public service contract (NavidromeRatingService.h in that
// repo) — the interface + GUID only, no implementation. Lets us set a rating on a navidrome://
// track without going through metadb_io_v2 (fails: "Tagging of this file format is not
// supported" — there's no real file to tag). Query via service_enum_t<navidrome_rating_api>;
// zero enumerated instances when foo_navidrome isn't installed, so callers must check first.
//
// GUID must stay byte-for-byte identical to the one in foo_navidrome's copy — that's what makes
// service_enum_t find it across the two separately-built DLLs.
#pragma once
#include "../fb2k.h"

namespace navidrome {

class NOVTABLE navidrome_rating_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(navidrome_rating_api);
public:
    virtual bool is_navidrome_track(const metadb_handle_ptr& track) = 0;
    virtual void set_rating_async(const metadb_handle_ptr& track, int stars) = 0;
};

// {DAD72066-852D-4787-9ECE-9E778F42DE08}
inline const GUID navidrome_rating_api::class_guid =
    { 0xdad72066, 0x852d, 0x4787, { 0x9e, 0xce, 0x9e, 0x77, 0x8f, 0x42, 0xde, 0x08 } };

} // namespace navidrome
