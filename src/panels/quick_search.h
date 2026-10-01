// Native "Quick Search Toolbar" (legacy fooAvA panel type; no stock DUI equivalent): a small
// text box in the skin's top bar. Typing (or Enter) fills a "Search results" playlist with every
// track from the Media Library and from all playlists that matches the query, using foobar2000's
// own search syntax; Esc clears it and closes the box (showsr pvar).
#pragma once
#include "../ui/view.h"
#include <memory>
#include <string>

namespace pui {

class SkinEngine;

class QuickSearch : public ui::View, private ui::TextFieldDelegate {
public:
    explicit QuickSearch(SkinEngine* engine) : m_engine(engine) {}

    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_resize(int w, int h) override;
    void on_timer(int id) override;
    void on_visibility(bool shown) override;
    void on_focus() override;
    void on_destroy() override { m_edit.reset(); }

private:
    void on_text_changed() override;
    void on_enter() override { run_search(); }
    void on_escape() override;
    void run_search();
    gfx::Color col(const char* role, gfx::Color def) const;

    SkinEngine* m_engine = nullptr;
    std::unique_ptr<ui::TextField> m_edit;
};

} // namespace pui
