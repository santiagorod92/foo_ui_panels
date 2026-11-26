#include "http_get.h"
#include "log.h"

namespace pui {

bool http_get(const std::string& url, std::string& body, abort_callback& ab, const char* tag, size_t max_bytes) {
    try {
        auto req = http_client::get()->create_request("GET");
        req->add_header("User-Agent", "foo_ui_panels (foobar2000 component)");
        file::ptr f = req->run(url.c_str(), ab);
        char buf[4096];
        body.clear();
        for (;;) {
            t_size n = f->read(buf, sizeof buf, ab);
            if (!n) break;
            body.append(buf, n);
            if (body.size() > max_bytes) break;
        }
        return !body.empty();
    } catch (const std::exception& e) {
        log::warn(tag, "GET " + url + " failed: " + e.what());
        return false;
    } catch (...) { return false; }
}

}
