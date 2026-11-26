#pragma once
#include "../ui/view.h"
#include <atomic>
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
    void on_destroy() override;

private:
    void on_text_changed() override;
    void on_enter() override { run_search(); }
    void on_escape() override;
    void run_search();
    void search_server(const std::string& query);
    void add_server_hits(const metadb_handle_list& hits);
    gfx::Color col(const char* role, gfx::Color def) const;

    SkinEngine* m_engine = nullptr;
    std::unique_ptr<ui::TextField> m_edit;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort;
    unsigned m_gen = 0;
};

}
