function on_paint(gr) {
    gr.GdiDrawText('other', window.GetFontDUI(0), window.GetColourDUI(0), 4, 4, window.Width - 8, 20, 0);
}

function on_notify_data(name, info) {
    if (name === 'bridge' && info && info.n === 7) panelsui.SetValue('bridge.other', 'got ' + info.n);
}
