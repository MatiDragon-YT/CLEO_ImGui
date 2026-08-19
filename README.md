# CLEO ImGui v1.2.0
Project inspired by [CLEO ImGui](https://github.com/user-grinch/CLEOImGui) by User-Grinch, respecting the order of its opcodes.

Created by: Matias A. Rossi (MatiDragon)

![front page](17856073058932.webp)

----

The project offers partial support for PC opcodes in Android.


<h2>OPCODES</h2>

```ini
#-------------CLEO IMGUI MOBILE---------

# Order of "CLEO ImGui"

0F01=4,imgui_begin %1d% show %2d% flags %3d% mouse_visible %4d% 
0F02=0,imgui_end 
0F03=3,imgui_checkbox label %1d% imguitypeflags %2d% var (int) %3d% 
0F04=3,imgui_button label %1d% width (float) %2d% height (float) %3d% 
0F05=5,imgui_calc_text_height text %1d% hide_after (bool) %2d% warp_width (float) %3d% imguitypeflags %4d% pointer/var (float) %5d% 
0F06=5,imgui_calc_text_width text %1d% hide_after (bool) %2d% warp_width (float) %3d% imguitypeflags %4d% pointer/var (float) %5d% 
0F07=3,imgui_set_next_window_pos pos x (float) %1d% pos y (float) %2d% imguicond %3d% 
0F08=3,imgui_set_window_pos pos x (float) %1d% pos y (float) %2d% imguicond %3d% 
0F09=1,imgui_get_font_size var (float) %1d% 
0F0A=3,imgui_set_next_window_size width (float) %1d% height (float) %2d% imguicond %3d% 
0F0B=3,imgui_set_window_size width (float) %1d% height (float) %2d% imguicond %3d% 
0F0C=0,imgui_show_demo_window 
0F0D=0,imgui_show_style_editor 
0F0E=1,imgui_text text %1d% 
0F0F=1,imgui_text_wrapped text %1d% 
0F10=1,imgui_text_disabled text %1d% 
0F11=5,imgui_text_colored text %1d% red (float) %2d% green (float) %3d% blue (float) %4d% alpha (float) %5d% 
0F12=2,imgui_columns count (int) %1d% border (int) %2d% 
0F13=0,imgui_next_column 
0F14=0,imgui_spacing 
0F15=2,imgui_dummy padding x (float) %1d% padding y (float) %2d% 
0F16=0,imgui_sameline 
0F17=7,imgui_slider_int label %1d% imguitypeflags %2d% var %3d% min (int) %4d% max (int) %5d% imguisliderflags %6d% imguislidercount %7d% 
0F18=7,imgui_slider_float label %1d% imguitypeflags %2d% var %3d% min (float) %4d% max (float) %5d% imguisliderflags %6d% imguislidercount %7d% 
0F19=5,imgui_color_edit label %1d% imguitypeflags %2d% var %3d% imguicoloreditflags %4d% imguicoloreditalpha %5d% 
0F1A=5,imgui_color_picker label %1d% imguitypeflags %2d% var %3d% imguicoloreditflags %4d% imguicoloreditalpha %5d% 
0F1B=5,imgui_begin_child label %1d% width (float) %2d% height (float) %3d% border (int) %4d% imguiwindowflags %5d% 
0F1C=0,imgui_end_child 
0F1D=5,imgui_input_int label %1d% imguitypeflags %2d% var %3d% imguiinputtextflags %4d% imguiinputcount %5d% 
0F1E=5,imgui_input_float label %1d% imguitypeflags %2d% var %3d% imguiinputtextflags %4d% imguiinputcount %5d% 
0F1F=5,imgui_input_text label %1d% hint %2d% var (char) %3d% buffer size (int) %4d% imguiinputtextflags %5d% 
0F20=6,imgui_input_text_multiline label %1d% var (char) %2d% buffer size (int) %3d% width (float) %4d% height (float) %5d% imguiinputtextflags %6d% 
0F25=0,imgui_separator 
0F26=1,imgui_get_cleo_imgui_version var (float) %1d% 
0F27=1,imgui_get_version var (float) %1d% 
0F28=1,imgui_get_framerate var (int) %1d% 
0F29=8,imgui_color_button label %1d% red (float) %2d% green (float) %3d% blue (float) %4d% alpha (float) %5d% imguicoloreditflags %6d% width (float) %7d% height (float) %8d% 
0F2A=0,imgui_bullet 
0F2B=1,imgui_bullet_text label %1d% 
0F2C=0,imgui_newline 
0F2D=1,imgui_set_tooltip label %1d% 
0F2E=6,imgui_color_tooltip label %1d% red (float) %2d% green (float) %3d% blue (float) %4d% alpha (float) %5d% imguicoloreditflags %6d% 
0F2F=2,imgui_is_item_hovered indentifier %1d% imguihoveredflags %2d% 
0F30=1,imgui_is_item_focused indentifier %1d% 
0F31=1,imgui_is_item_activated indentifier %1d% 
0F32=1,imgui_is_item_deactivated indentifier %1d% 
0F33=1,imgui_is_item_active indentifier %1d% 
0F34=2,imgui_is_item_clicked indentifier %1d% imguimousebutton %2d% 
0F35=2,imgui_is_window_hovered indentifier %1d% imguihoveredflags %2d% 
0F36=2,imgui_is_window_focused indentifier %1d% imguihoveredflags %2d% 
0F37=4,imgui_radio_button label %1d% imguitypeflags %2d% pointer/var (int) %3d% number (int) %4d% 
0F38=2,imgui_collasping_header label %1d% imguitreenodeflags %2d% 
0F39=4,imgui_progress_bar label %1d% fraction (float) %2d% width (float) %3d% height (float) %4d% 
0F3A=2,imgui_get_window_posy imguitypeflags %1d% pointer/var (float) %2d% 
0F3B=2,imgui_get_window_posx imguitypeflags %1d% pointer/var (float) %2d% 
0F3C=2,imgui_get_window_width imguitypeflags %1d% pointer/var (float) %2d% 
0F3D=2,imgui_get_window_height imguitypeflags %1d% pointer/var (float) %2d% 
0F3E=5,imgui_selectable label %1d% check mark (int) %2d% imguiselectableflags %3d% width (float) %4d% height (float) %5d% 
0F40=2,imgui_load_image path %1d% var (int) %2d% 
0F41=4,imgui_image indentifier %1d% var (int) %2d% width (float) %3d% height (float) %4d% 
0F42=16,imgui_image_ex indentifier %1d% var (int) %2d% width (float) %3d% height (float) %4d% uv x (float) %5d% uv0 y (float) %6d% uv x (float) %7d% uv0 y (float) %8d% red (float) %9d% green (float) %10d% blue (float) %11d% alpha (float) %12d% border red (float) %13d% border green (float) %14d% border blue (float) %15d% border alpha (float) %16d% 
0F43=4,imgui_image_button indentifier %1d% var (int) %2d% width (float) %3d% height (float) %4d% 
0F44=16,imgui_image_button_ex indentifier %1d% var (int) %2d% width (float) %3d% height (float) %4d% uv0 x (float) %5d% uv0 y (float) %6d% uv1 x (float) %7d% uv1 y (float) %8d% red (float) %9d% green (float) %10d% blue (float) %11d% alpha (float) %12d% border red (float) %13d% border green (float) %14d% border blue (float) %15d% border alpha (float) %16d% 
#0F45=1,imgui_get_game_path pointer (char) %1d% 
0F46=4,imgui_invisible_button label %1d% width (float) %2d% height (float) %3d% imguibuttonflags %4d% 
0F47=6,imgui_drawlist_add_circle center x (float) %1d% center y (float) %2d% radius (float) %3d% color (int) %4d% segment (int) %5d% thickness (int) %6d% 
0F48=5,imgui_drawlist_add_circle_filled center x (float) %1d% center y (float) %2d% radius (float) %3d% color (int) %4d% segment (int) %5d% 
0F49=8,imgui_drawlist_add_rect min x (float) %1d% min y (float) %2d% max x (float) %3d% max y (float) %4d% color (int) %5d% rounding (int) %6d% corners (int) %7d% thickness (int) %8d% 
0F4A=7,imgui_drawlist_add_rect_filled min x (float) %1d% min y (float) %2d% max x (float) %3d% max y (float) %4d% color (int) %5d% rounding (int) %6d% corners (int) %7d% 
0F4B=8,imgui_drawlist_add_rect_filled_multicolor min x (float) %1d% min y (float) %2d% max x (float) %3d% max y (float) %4d% color up left (int) %5d% color up right (int) %6d% color down left (int) %7d% color down right (int) %8d% 
0F4C=5,imgui_drawlist_add_text text %1d% pos x (float) %2d% pos y (float) %3d% radius (float) %4d% color (int) %5d% 
0F4D=8,imgui_drawlist_add_triangle point1 x (float) %1d% point1 y (float) %2d% point2 x (float) %3d% point2 y (float) %4d% point3 x (float) %5d% point3 y (float) %6d% color (int) %7d% thickness (int) %8d% 
0F4E=7,imgui_drawlist_add_triangle_filled point1 x (float) %1d% point1 y (float) %2d% point2 x (float) %3d% point2 y (float) %4d% point3 x (float) %5d% point3 y (float) %6d% color (int) %7d% 
0F4F=0,imgui_begin_main_menu_bar 
0F50=0,imgui_end_main_menu_bar 
0F51=1,imgui_menu_item label %1d% 
0F52=0,imgui_style_colors_classic 
0F53=0,imgui_style_colors_dark 
0F54=0,imgui_style_colors_default 
0F55=0,imgui_style_colors_light 
0F57=2,imgui_get_style imguistyleoffsets %1d% var (float) %2d% 
0F58=2,imgui_set_style imguistyleoffsets %1d% value (float) %2d% 
0F59=2,imgui_set_style_int imguistyleoffsets %1d% value (int) %2d% 
0F5A=5,imgui_get_color imguicoloroffsets %1d% red (float) %2d% green (float) %3d% blue (float) %4d% alpha (float) %5d% 
0F5B=5,imgui_set_color imguicoloroffsets %1d% red (float) %2d% green (float) %3d% blue (float) %4d% alpha (float) %5d% 
0F5C=1,imgui_push_item_width width (float) %1d% 
0F5D=0,imgui_pop_item_width 
0F5E=2,imgui_push_item_flag imguiitemflags %1d% enabled (int) %2d% 
0F5F=0,imgui_pop_item_flag 
0F60=2,imgui_get_window_content_region_width imguitypeflags %1d% pointer/var (float) %2d% 
0F61=2,imgui_get_frame_height imguitypeflags %1d% pointer/var (float) %2d% 
0F62=1,imgui_get_frame_height_with_spacing var (float) %1d% 
0F63=2,imgui_get_style_int imguistyleoffsets %1d% var (int) %2d% 

// Order of "CLEO ImGui Redux"

#2200 IMGUI_BEGIN_FRAME
#2201 IMGUI_END_FRAME
2202=7,%7d% = imgui_begin %1d% state %2d% no_title %3d% no_resize %4d% no_move %5d% auto_resize %6d%
2203=0,imgui_end // is 0F02
2204=1,imgui_begin_main_menu_bar %1d% state %2d%
2205=0,imgui_end_main_menu_bar // is 0F50
2206=1,imgui_begin_child %1d%
2207=0,imgui_end_child // is 0F1C
#2208 IMGUI_TABS
2209=imgui_collasping_header %1d% flags %2d%  // is 0F38
220A=3,imgui_set_window_pos %1d% %2d% cond %3d% // is 0F08
220B=3,imgui_set_window_size %1d% %2d% cond %3d% // is 0F0B
220C=3,imgui_set_next_window_pos %1d% %2d% cond %3d% // is 0F07
220D=3,imgui_set_next_window_size %1d% %2d% cond %3d% // is 0F0A
220E=1,imgui_text %1d% // is 0F0E
#220F IMGUI_TEXT_CENTERED
2210=1,imgui_text_disabled %1d% // is 0F10
2211=1,imgui_text_wrapped %1d% // is 0F0F
2212=5,imgui_text_colored %1d% rgba_float %2d% %3d% %4d% %5d%  // is 0F11
2213=1,imgui_bullet_text %1d% // is 0F2B
2214=0,imgui_bullet // is 0F2A
2215=3,%3d% = imgui_checkbox %1d% flags %2d% // is 0F03
#2216 IMGUI_COMBO
2217=1,imgui_set_tooltip %1d% // is 0F2D
2218=3,imgui_button %1d% scale %2d% %3d% // is 0F04
2219=4,imgui_image_button %1d% imagen %2d% scale %3d% %4d% // is 0f43
221A=3,imgui_invisible_button %1d% scale %2d% %3d% 
221B=7,imgui_color_button %1d% rgba_float %2d% %3d% %4d% %5d% scale %6d% %7d%
#221C IMGUI_ARROW_BUTTON
221D=5,%5d% = imgui_slider_int %1d% init %2d% min %3d% max %4d% // init, not working 
221E=5,%5d% = imgui_slider_float %1d% init %2d% min %3d% max %4d% // init, not working 
221F=5,%5d% = imgui_input_int %1d% init %2d% min %3d% max %4d% // init, min, max, not working
2220=5,%5d% = imgui_input_float %1d% init %2d% min %3d% max %4d% // init, min, max, not working
2221=2,%2d% = imgui_input_text %1d%
2222=4,%4d% = imgui_radio_button %1d% selected %2d% number %3d% // selected, not working 
2223=5,imgui_color_picker %1d% rgba_int %2d% %3d% %4d% %5d%
2224=4,%4d% = imgui_menu_item %1d% selected %2d% enabled %3d%
2225=3,%3d% = imgui_selectable %1d% selected %2d%
2226=2,imgui_dummy_padding %1d% %2d% // is 0F15
2227=0,imgui_sameline // is 0F16
2228=0,imgui_newline // is 0F2C
2229=1,imgui_columns %1d% 
222A=0,imgui_next_column // is 0F13
222B=0,imgui_spacing // is 0F14
222C=0,imgui_separator // is 0F25
222D=1,imgui_push_item_width %1d% // is 0F5C
222E=0,imgui_pop_item_width // is 0F5D
222F=2,%2d% = imgui_is_item_active %1d% id // id, not working 
2230=2,%2d% = imgui_is_item_clicked %1d% id // id, not working
2231=2,%2d% = imgui_is_item_focused %1d% id // id, not working
2232=2,%2d% = imgui_is_item_hovered %1d% id // id, not working
#2233 IMGUI_SET_ITEM_INT
#2234 IMGUI_SET_ITEM_FLOAT
#2235 IMGUI_SET_ITEM_TEXT
2236=4,imgui_set_image_bg_color %1d% %2d% %3d% %4d% rgba_float
2237=4,imgui_set_image_tint_color %1d% %2d% %3d% %4d% rgba_float
2238=2,%2d% = imgui_load_image %1d% path // is 0F40
2239=1,imgui_free_image %1d%
#223A IMGUI_PUSH_STYLE_VAR
#223B IMGUI_PUSH_STYLE_VAR2
#223C IMGUI_PUSH_STYLE_COLOR
#223D IMGUI_POP_STYLE_VAR
#223E IMGUI_POP_STYLE_COLOR
#223F IMGUI_GET_FOREGROUND_DRAWLIST
#2240 IMGUI_GET_BACKGROUND_DRAWLIST
#2241 IMGUI_GET_WINDOW_DRAWLIST
2242=8,imgui_drawlist_add_text %8d% at %2d% %3d% rgba_int %4d% %5d% %6d% %7d% layer %1d%
#2243 IMGUI_DRAWLIST_ADD_LINE
#2244 GET_FRAMERATE
2246=1,%1d% = imgui_get_cleo_imgui_version // is 0F27
2245=1,%1d% = imgui_get_version // is 0F26
#2247 IMGUI_SET_CURSOR_VISIBLE
2248=1,%1d% = imgui_get_frame_height // is 0F61
2249=3,imgui_get_window_pos flag %1d% xy %2d% %3d% 
#224A IMGUI_GET_WINDOW_SIZE
#224B IMGUI_CALC_TEXT_SIZE
224C=2,%2d% = imgui_get_window_content_region_width %1d% // is 0F60
#224D IMGUI_GET_SCALING_SIZE
#224E IMGUI_GET_DISPLAY_SIZE
#224F IMGUI_SET_NEXT_WINDOW_TRANSPARENCY
#2250 IMGUI_SET_MESSAGE

# AML Exclusives

2300=4,imgui_set_image_uv %1d% %2d% %3d% %4d%
2301=4,imgui_set_image_border_color %1d% %2d% %3d% %4d% rgba_float
2302=1,imgui_set_text_limit %1d% // for imgui_input_text & imgui_keyboard_show
2303=2,imgui_keyboard_show %1d% var %2d%
2304=0,imgui_keyboard_hide
2305=0,imgui_keyboard_is_visible
2306=0,imgui_image_reset_color
2307=1,imgui_keyboard_set_enter_mode %1d% // 1 = "\n" || 0 = close()
```


<h2>CHANGE LOG</h2>

### CLEO ImGui v1.2.0

Added opcodes:
* IMGUI_INPUT_TEXT_MULTILINE
* IMGUI_GET_GAME_PATH
* IMGUI_GET_STYLE
* IMGUI_SET_STYLE
* IMGUI_SET_STYLE_INT
* IMGUI_GET_COLOR
* IMGUI_SET_COLOR
* IMGUI_CALC_TEXT_HEIGHT
* IMGUI_CALC_TEXT_WIDTH
* IMGUI_SET_NEXT_WINDOW_POS
* IMGUI_SET_WINDOW_POS
* IMGUI_GET_FONT_SIZE
* IMGUI_SET_NEXT_WINDOW_SIZE
* IMGUI_SET_WINDOW_SIZE
* IMGUI_SHOW_DEMO_WINDOW
* IMGUI_SHOW_STYLE_EDITOR
* IMGUI_GET_WINDOW_POS
* IMGUI_LOAD_IMAGE ! If an image could not be loaded, it returns 0; otherwise, it keeps the same ID.
* IMGUI_FREE_IMAGE ! It removes the map reference, but doesn't release the texture from the GPU (due to a lack of API). It's used to upload edited images.
* IMGUI_IMAGE
* IMGUI_IMAGE_EX
* IMGUI_IMAGE_BUTTON
* IMGUI_IMAGE_BUTTON_EX
* IMGUI_SET_IMAGE_BG_COLOR
* IMGUI_SET_IMAGE_TINT_COLOR
* IMGUI_SET_IMAGE_UV
* IMGUI_SET_IMAGE_BORDER_COLOR


### CLEO ImGui v1.1.1

Support for virtual keyboard inputs.

Implemented opcodes of "CLEO ImGui Redux".

### CLEO ImGui v1.1.0

Added opcodes:
* IMGUI_IS_ITEM_HOVERED
* IMGUI_IS_ITEM_FOCUSED
* IMGUI_IS_ITEM_ACTIVATED
* IMGUI_IS_ITEM_DEACTIVATED
* IMGUI_IS_ITEM_ACTIVE
* IMGUI_IS_ITEM_CLICKED
* IMGUI_IS_WINDOW_HOVERED
* IMGUI_IS_WINDOW_FOCUSED

### CLEO ImGui v1.0.1

Arranged conditionals:
* IMGUI_CHECKBO
* IMGUI_BUTTON
* IMGUI_SLIDER_INT
* IMGUI_SLIDER_FLOAT
* IMGUI_COLOR_EDIT
* IMGUI_COLOR_PICKER
* IMGUI_INPUT_INT
* IMGUI_INPUT_FLOAT
* IMGUI_MENU_ITEM 
* IMGUI_RADIO_BUTTON
* IMGUI_COLLAPSING_HEADER
* IMGUI_SELECTABLE
* IMGUI_INVISIBLE_BUTTON
* IMGUI_MENU_ITEM

Functions fixed:
* IMGUI_COLOR_TOOLTIP (COLOR)

Fixed returns:
* IMGUI_GET_CLEO_IMGUI_VERSION (STRING)
* IMGUI_GET_VERSION (STRING)

### CLEO ImGui v1.0.0

Added opcodes:
* IMGUI_BEGIN
* IMGUI_END
* IMGUI_CHECKBOX
* IMGUI_BUTTON
* IMGUI_TEXT
* IMGUI_TEXT_WRAPPED
* IMGUI_TEXT_DISABLED
* IMGUI_TEXT_COLORED
* IMGUI_COLUMNS
* IMGUI_NEXT_COLUMN
* IMGUI_SPACING
* IMGUI_DUMMY
* IMGUI_SAMELINE
* IMGUI_SLIDER_INT
* IMGUI_SLIDER_FLOAT
* IMGUI_COLOR_EDIT
* IMGUI_COLOR_PICKER
* IMGUI_BEGIN_CHILD
* IMGUI_END_CHILD
* IMGUI_INPUT_INT
* IMGUI_INPUT_FLOAT
* IMGUI_SEPARATOR
* IMGUI_GET_CLEO_IMGUI_VERSION
* IMGUI_GET_VERSION
* IMGUI_GET_FRAMERATE
* IMGUI_COLOR_BUTTON
* IMGUI_BULLET
* IMGUI_BULLET_TEXT
* IMGUI_NEWLINE
* IMGUI_SET_TOOLTIP
* IMGUI_COLOR_TOOLTIP
* IMGUI_RADIO_BUTTON
* IMGUI_COLLAPSING_HEADER
* IMGUI_PROGRESS_BAR
* IMGUI_GET_WINDOW_POSY
* IMGUI_GET_WINDOW_POSX
* IMGUI_GET_WINDOW_WIDTH
* IMGUI_GET_WINDOW_HEIGHT
* IMGUI_SELECTABLE
* IMGUI_INVISIBLE_BUTTON
* IMGUI_DRAWLIST_ADD_CIRCLE
* IMGUI_DRAWLIST_ADD_CIRCLE_FILLED
* IMGUI_DRAWLIST_ADD_RECT
* IMGUI_DRAWLIST_ADD_RECT_FILLED
* IMGUI_DRAWLIST_ADD_RECT_FILLED_MULTICOLOR
* IMGUI_DRAWLIST_ADD_TEXT
* IMGUI_DRAWLIST_ADD_TRIANGLE
* IMGUI_DRAWLIST_ADD_TRIANGLE_FILLED
* IMGUI_BEGIN_MAIN_MENU_BAR
* IMGUI_END_MAIN_MENU_BAR
* IMGUI_MENU_ITEM
* IMGUI_STYLE_COLORS_CLASSIC
* IMGUI_STYLE_COLORS_DARK
* IMGUI_STYLE_COLORS_DEFAULT
* IMGUI_STYLE_COLORS_LIGHT
* IMGUI_GET_STYLE
* IMGUI_SET_STYLE
* IMGUI_SET_STYLE_INT
* IMGUI_GET_COLOR
* IMGUI_SET_COLOR
* IMGUI_PUSH_ITEM_WIDTH
* IMGUI_POP_ITEM_WIDTH
* IMGUI_PUSH_ITEM_FLAG
* IMGUI_POP_ITEM_FLAG
* IMGUI_GET_WINDOW_CONTENT_REGION_WIDTH
* IMGUI_GET_FRAME_HEIGHT
* IMGUI_GET_FRAME_HEIGHT_WITH_SPACING
* IMGUI_GET_STYLE_INT