#pragma once
#include "navidrome_library_api.h"

namespace pui {

inline navidrome::navidrome_library_api::ptr navidrome_library() {
    service_enum_t<navidrome::navidrome_library_api> e;
    navidrome::navidrome_library_api::ptr api;
    e.next(api);
    return api;
}

template <class T>
service_ptr_t<T> navidrome_library_as() {
    service_ptr_t<T> out;
    auto api = navidrome_library();
    if (api.is_valid() && api->is_configured()) api->service_query_t(out);
    return out;
}

}
