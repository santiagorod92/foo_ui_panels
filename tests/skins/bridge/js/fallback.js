function on_paint(gr) {
    gr.GdiDrawText('fallback', window.GetFontDUI(0), window.GetColourDUI(0), 2, 2, window.Width - 4, 20, 0);
}

panelsui.SetValue('bridge.fallback', panelsui.PanelName);
