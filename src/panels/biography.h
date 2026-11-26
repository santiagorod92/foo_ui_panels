#pragma once
#include "../ui/view.h"
#include <atomic>
#include <map>
#include <memory>
#include <string>

namespace pui {

class SkinEngine;

class Biography : public ui::View {
public:
    explicit Biography(SkinEngine* engine) : m_engine(engine) {}

    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_destroy() override;

private:
    struct Info {
        bool loading = true, found = false;
        std::string bio, image_url, lastfm_url;
        gfx::ImagePtr image;
    };

    void refresh_artist();
    void fetch(const std::string& artist);
    gfx::Color col(const char* role, gfx::Color def) const;

    SkinEngine* m_engine = nullptr;
    std::string m_artist;
    titleformat_object::ptr m_artistScript;
    std::map<std::string, Info> m_cache;
    int m_scroll = 0, m_content = 0;
    gfx::Rect m_link;
    bool m_overLink = false;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<shared_abort>();
};

}
