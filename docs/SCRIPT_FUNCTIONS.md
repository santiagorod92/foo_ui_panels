# Panels UI script functions

<!-- Generated from the table in src/core/script_runtime.cpp by `make docs` — do not edit. -->

The `$functions` this engine adds to foobar2000's titleformat. Every standard titleformat
function (`$if`, `$sub`, `$upper`, …) and field works as well. Coordinates are in skin
pixels; colours are `r-g-b` unless noted.

| Function | Status | What it does |
|---|---|---|
| `$panel(name,type,x,y,w,h)` | yes | Hosts a panel (native view or UI element) at the rect. |
| `$windowstyle(hidetitlebar\|showtitlebar)` | yes | Shows or hides the player window's title bar. |
| `$settitle(text)` | yes | The window/taskbar title. |
| `$settray(tooltip)` | yes | Asks for a tray (Windows) / menu-bar (macOS) icon. |
| `$eval(expr)` | yes | Integer arithmetic (+ - * / %, () or {} grouping); $get/$getpvar may be nested. |
| `$get(name)` | yes | A $puts value (replayed, so a stored draw command runs), else the pvar of that name. |
| `$puts(name,value)` | yes | Sets a per-paint scratch variable. |
| `$getpvar(name)` | yes | A persistent variable (names ignore case). |
| `$setpvar(name,value)` | yes | Sets a persistent variable. |
| `$greater(a,b)` | yes | 1 if a > b (integers), else nothing. |
| `$fileexists(path)` | yes | 1 if the file exists (relative to the skin folder). |
| `$cwb_fileexists(path)` | yes | foo_cwb_hooks' spelling of $fileexists. |
| `$calculate_blend_target(colour)` | yes | Black if the hex colour is light, else white (Columns UI). |
| `$offset_colour(from,to,amount)` | yes | Shifts a hex colour toward another by amount 0..255 (Columns UI). |
| `$font(face,size[,style[,r-g-b]])` | yes | Selects the font; style: bold italic underline glow-r-g-b glowexpand-N glowalpha-N. |
| `$set_font(face,size[,style[,r-g-b]])` | yes | Same as $font. |
| `$textcolor(r-g-b)` | yes | Colour of following text. |
| `$set_font_color(r-g-b)` | yes | Same as $textcolor. |
| `$alignabs(x,y,w,h[,halign[,valign]])` | yes | Box for the literal text that follows (left/center/right, top/center). |
| `$drawstring(text,x,y,w,h[,r-g-b[,opts]])` | yes | Draws text in a box; opts: center right vcenter, and wrap (several lines — this engine's addition). |
| `$draw_text(text,x,y,w,h[,opts])` | yes | Draws text in a box in the current text colour. |
| `$calcwidth(text)` | yes | Pixel width of text in the current font. |
| `$drawrect(x,y,w,h,spec)` | yes | Rectangle; spec: brushcolor-r-g-b pencolor-r-g-b alpha-N (null = none). |
| `$drawroundrect(x,y,w,h,rx,ry,r-g-b)` | yes | Filled rounded rectangle. |
| `$gradientrect(x,y,w,h,top,bottom)` | yes | Vertical gradient between two r-g-b colours. |
| `$gp_set_brush(a-r-g-b)` | yes | Brush for $gp_fill_rectangle (r-g-b = opaque). |
| `$gp_fill_rectangle(x,y,w,h)` | yes | Fills with the GDI+ brush. |
| `$gp_set_pen(a-r-g-b[,width])` | yes | Pen for $gp_draw_rectangle (dash/join arguments ignored). |
| `$gp_draw_rectangle(x,y,w,h)` | yes | Outline centred on the rectangle's edges, like GDI+. |
| `$imageabs(x,y,w,h,path[,align])` | yes | Draws an image (wildcards and the track's album art allowed). |
| `$draw_image(x,y,w,h,path,…)` | yes | Same as $imageabs. |
| `$imageabs2(w,h,srcX,srcY,srcW,srcH,x,y,path[,opts])` | yes | Draws an image scaled into w×h (0 = natural / cap), optionally cropped; opts: alpha-N ROTATEFLIP-N. |
| `$button(x,y,?,?,w,h,normal,hover,action[,TOOLTIP:text])` | yes | Image button (w/h 0 = the image's size). |
| `$button2(x,y,?,?,w,h,normal,hover,action[,TOOLTIP:text])` | yes | Button whose states are draw commands or a text label. |
| `$textbutton(x,y,w,h,normal,hover[,action[,TOOLTIP,text]])` | yes | Text button. |
| `$imagebutton(x,y,normal,hover,action[,TOOLTIP,text])` | yes | Small image button (rating stars). |
| `$scplsetlayout(…)` | ignored | Single Column Playlist layout — ignored (the native playlist has its own). |
