const fails = [];
const minis = [];
function check(what, ok) { if (!ok) fails.push(what); }
function rect_is(r, x, y, w, h) { return !!r && r.X === x && r.Y === y && r.Width === w && r.Height === h; }

function on_paint(gr) {
    gr.GdiDrawText('probe ' + panelsui.PanelName, window.GetFontDUI(0), window.GetColourDUI(0), 4, 4, window.Width - 8, 24, 0);
}

function on_panelsui_mini_mode(on) { minis.push(on); }

check('name at load', panelsui.PanelName === 'Probe');

const steps = [
    () => {
        check('transparent', window.IsTransparent === true);
        check('colour', window.GetColourDUI(0) === 0xffff0000);
        check('font', window.GetFontDUI(0).Name === 'Arial');
        check('rect', rect_is(panelsui.GetPanelRect(), 20, 20, 300, 160));
        check('zoom field', /^[0-9]+$/.test(panelsui.Eval('%pui_zoom%')));
        panelsui.SetValue('bridge.v', '42');
        check('value', panelsui.GetValue('bridge.v') === '42' && panelsui.Eval('$getvalue(bridge.v)') === '42');
        panelsui.MovePanel('Other', 340, 120, 180, 90);
        panelsui.RunAction('JSPANEL:bridge:{"n":7}');
    },
    () => {
        check('move', rect_is(panelsui.GetPanelRect('Other'), 340, 120, 180, 90));
        check('notify', panelsui.GetValue('bridge.other') === 'got 7');
        panelsui.ShowPanel('Other', false);
    },
    () => {
        check('hide', panelsui.GetPanelRect('Other') === null);
        panelsui.ResetPanel('Other');
        panelsui.RunAction('MINIMODE:TOGGLE');
    },
    () => {
        check('reset', rect_is(panelsui.GetPanelRect('Other'), 340, 20, 200, 100));
        check('mini field', panelsui.Eval('$if(%pui_mini_mode%,1,0)') === '1');
        panelsui.RunAction('MINIMODE:TOGGLE');
    },
    () => {
        check('mini events', minis.join() === 'true,false');
        console.log('bridge: result ' + (fails.length ? 'fail: ' + fails.join(', ') : 'ok'));
    },
];
steps.forEach((step, i) => window.SetTimeout(step, 2000 + i * 1500));
