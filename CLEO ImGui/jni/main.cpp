#include <mod/amlmod.h>
#include <mod/logger.h>

#include <string>
#include <vector>
#include <functional>

#include "main.h"
#include "arial.h"

#include "cleo.h"
cleo_ifs_t* cleo = nullptr;

#include "cleoaddon.h"
cleo_addon_ifs_t* cleoaddon = nullptr;

MYMOD(net.matidragon.cleo_imgui, CLEO_ImGui, 1.0.1, MatiDragon)
BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.cleolib, 2.0.1.10)
END_DEPLIST()

// ======================== OPCODES CLEO ========================
#define CLEO_RegisterOpcode(x, h) cleo->RegisterOpcode(x, h); cleo->RegisterOpcodeFunction(#h, h)
#define CLEO_Fn(h) void h (void *handle, uint32_t *ip, uint16_t opcode, const char *name)
// ── Macros para reducir código repetitivo ─────────────────
#define READ_STRING(v, s) char v[s]; cleoaddon->ReadString(handle, v, s)
#define READ_INT(name) int name = cleo->ReadParam(handle)->i
#define READ_FLOAT(name) float name = cleo->ReadParam(handle)->f
#define READ_INTPTR(name) int* name = &cleo->GetPointerToScriptVar(handle)->i
#define READ_FLOATPTR(name) float* name = &cleo->GetPointerToScriptVar(handle)->f
#define SET_INT(val) cleo->GetPointerToScriptVar(handle)->i = val
#define SET_FLOAT(val) cleo->GetPointerToScriptVar(handle)->f = val
#define RET_OP(ret) cleoaddon->UpdateCompareFlag(handle, ret);

// ── Macros para widgets con/sin cola ──────────────────────
#define QUEUE_PUSH(body) g_drawQueue.push_back([=]() body)
#define SIMPLE_WIDGET(body) CLEO_Fn(body) { g_drawQueue.push_back([]() { body; }); }
#define DIRECT_GETTER(body) CLEO_Fn(body)


static IM imgui;
IImGui* pImGui = &imgui;
ImGuiContext *imguiCtx = NULL;

static const ImWchar ranges[] = {
    0x0020, 0x0080,
    0x00A0, 0x00C0,
    0x0400, 0x0460,
    0x0490, 0x04A0,
    0x2010, 0x2040,
    0x20A0, 0x20B0,
    0x2110, 0x2130,
    0
};

uintptr_t pGameLib = 0;
void* pGameHandle = nullptr;
bool bImGuiInitialized = false;

RwReal* nearScreenZ;
RwReal* recipNearClip;
void* pTheCamera = nullptr;
void (*SetScissorRect)(float*);
int (*GetScreenFadeStatus)(void*);
void (*GTA_RequestKeyboard)(int);
int* m_bMenuOpened;
bool* m_UserPause;
int* NVtoKK;
char *KKtoChar, *KKtoShiftedChar;

ImVec2 displaySize;
ImVec2 zeroVec(0,0);

static float flScaleX, flScaleY;
static int nDisplayX, nDisplayY;
inline float ScaleX(float x) { return flScaleX * x; } float IM::GetScaledX(float f) { return ScaleX(f); }
inline float ScaleY(float y) { return flScaleY * y; } float IM::GetScaledY(float f) { return ScaleY(f); }
int IM::GetScreenSizeX() { return nDisplayX; }
int IM::GetScreenSizeY() { return nDisplayY; }

void ImGui_ImplRenderWare_RenderDrawData(ImDrawData* draw_data);
bool ImGui_ImplRenderWare_Init();
void ImGui_ImplRenderWare_NewFrame();
void ImGui_ImplRenderWare_ShutDown();

// ── Cola de dibujado para CLEO ──────────────────────
static std::vector<std::function<void()>> g_drawQueue;

#define FRAMES_TO_CLEAR_MOUSE 3
static char nClearMousePos = 0;
ImFont* kbFont;

// ---------- Hooks (sin cambios relevantes) ----------
DECL_HOOK(bool, InitRenderware)
{
    if(!InitRenderware()) return false;
    InitRenderWareFunctions();

    nDisplayX = RsGlobal->maximumWidth;
    nDisplayY = RsGlobal->maximumHeight;
    flScaleX = nDisplayY * 0.00052083333f;
    flScaleY = nDisplayY * 0.00092592592f;
    displaySize.x = nDisplayX;
    displaySize.y = nDisplayY;
    bImGuiInitialized = true;

    imguiCtx = ImGui::CreateContext();
    ImGui_ImplRenderWare_Init();
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScrollbarSize = ScaleY(55.0f);
    style.WindowBorderSize = 0.0f;
    ImGui::StyleColorsDark();
    imgui.m_pFont = io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), ScaleY(34.0f), NULL, ranges);

    return true;
}

DECL_HOOKv(ShutdownRenderware)
{
    ImGui_ImplRenderWare_ShutDown();
    ImGui::DestroyContext();
    ShutdownRenderware();
}

bool bDisplaySpecialImGuiMenu = false;
ImGuiID LastFocus = -1, LastActive = -1;
ImGuiWindow* LastWindow;

DECL_HOOKv(Render2DStuff)
{
    Render2DStuff();

    ImGui_ImplRenderWare_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();

    // ── Ejecutar los comandos encolados por CLEO ─────
    for (auto& cmd : g_drawQueue) cmd();
    g_drawQueue.clear();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());

    if(nClearMousePos > 0)
    {
        if(--nClearMousePos == 0)
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
}

// ---------- Touch y teclado (sin cambios) ----------
static uint8_t fingers = 0;
static uint8_t fingerAsMouse = 0xFF;
static bool g_bIgnoredFingers[10] = {false};

inline bool CanProcessImTouch()
{
    return (bImGuiInitialized && *m_bMenuOpened == 0 && *m_UserPause == false && GetScreenFadeStatus(pTheCamera) != 2);
}

inline bool NeedToIgnore()
{
    return bDisplaySpecialImGuiMenu || ImGui::IsAnyItemHovered();
}

DECL_HOOKv(OnTouchEvent, int type, int fingerId, int x, int y)
{
    ImGuiIO* io = &ImGui::GetIO();
    bool canProc = CanProcessImTouch();
    if(type == TOUCH_PUSH) ++fingers;
    else if(type == TOUCH_RELEASE) --fingers;

    if(!canProc) { OnTouchEvent(type, fingerId, x, y); return; }

    switch(type)
    {
        case TOUCH_PUSH:
        {
            if(fingerAsMouse == 0xFF)
            {
                io->AddMousePosEvent(x, y);
                io->AddMouseButtonEvent(0, true);
                fingerAsMouse = fingerId;
            }
            if(NeedToIgnore()) g_bIgnoredFingers[fingerId] = true;
            else OnTouchEvent(type, fingerId, x, y);
            break;
        }
        case TOUCH_RELEASE:
        {
            if(fingerAsMouse == fingerId)
            {
                nClearMousePos = FRAMES_TO_CLEAR_MOUSE;
                io->AddMouseButtonEvent(0, false);
                fingerAsMouse = 0xFF;
            }
            if(!g_bIgnoredFingers[fingerId]) OnTouchEvent(type, fingerId, x, y);
            g_bIgnoredFingers[fingerId] = false;
            break;
        }
        case TOUCH_MOVE:
        {
            if(fingerAsMouse == fingerId) io->AddMousePosEvent(x, y);
            if(!g_bIgnoredFingers[fingerId]) OnTouchEvent(type, fingerId, x, y);
            break;
        }
        default: OnTouchEvent(type, fingerId, x, y); break;
    }
}

DECL_HOOKv(GTA_KeyboardEvent, bool pushed, int keyNum, int ctrl_or_shift, int alwaysZero)
{
    ImGuiIO& io = ImGui::GetIO();
    if(io.WantTextInput)
    {
        if(keyNum == 4) io.AddKeyEvent(ImGuiKey_Backspace, pushed);
        else if(keyNum == 3) io.AddKeyEvent(ImGuiKey_Enter, pushed);
        else if(pushed)
        {
            char sym = 0;
            if(ctrl_or_shift <= 0) sym = KKtoChar[NVtoKK[keyNum]];
            else sym = KKtoShiftedChar[NVtoKK[keyNum]];
            if(sym != 0) io.AddInputCharacter(sym);
        }
    }
    GTA_KeyboardEvent(pushed, keyNum, ctrl_or_shift, alwaysZero);
}

// ======================== OPCODES CLEO ========================
// ======================== OPCODES CLEO ========================

// 0F01: imgui_begin
CLEO_Fn(IMGUI_BEGIN)
{
    READ_STRING(lamete, 128);
    READ_INT(shown);    // bool
    READ_INT(flags);    // ImGuiWindowFlags
    READ_INT(mouseVis); //  ignorado de momento
    
    std::string windowName = lamete;
    g_drawQueue.push_back([windowName, flags, mouseVis]() {
        // ImGui::SetNextWindowBgAlpha(1.0f); // etc si quieres usar mouseVis
        ImGui::Begin(windowName.c_str(), nullptr, flags);
    });
}

// 0F02: imgui_end
CLEO_Fn(IMGUI_END)
{
    g_drawQueue.push_back([]() { ImGui::End(); });
}

// 0F03: imgui_checkbox
CLEO_Fn(IMGUI_CHECKBOX)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags   = cleo->ReadParam(handle)->i;
    int* pVar   = &cleo->GetPointerToScriptVar(handle)->i;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, pVar]() {
        bool val = (*pVar != 0);
        if (ImGui::Checkbox(lbl.c_str(), &val))
            *pVar = val ? 1 : 0;
    });
}

// 0F04: imgui_button
CLEO_Fn(IMGUI_BUTTON)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    float w = cleo->ReadParam(handle)->f;
    float h = cleo->ReadParam(handle)->f;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h]() {
        ImGui::Button(lbl.c_str(), ImVec2(w, h));
    });
}

// 0F0E: imgui_text
CLEO_Fn(IMGUI_TEXT)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::TextUnformatted(t.c_str()); });
}

// 0F0F: imgui_text_wrapped
CLEO_Fn(IMGUI_TEXT_WRAPPED)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::TextWrapped("%s", t.c_str()); });
}

// 0F10: imgui_text_disabled
CLEO_Fn(IMGUI_TEXT_DISABLED)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::TextDisabled("%s", t.c_str()); });
}

// 0F11: imgui_text_colored
CLEO_Fn(IMGUI_TEXT_COLORED)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    float r = cleo->ReadParam(handle)->f;
    float g = cleo->ReadParam(handle)->f;
    float b = cleo->ReadParam(handle)->f;
    float a = cleo->ReadParam(handle)->f;
    std::string t = text;
    g_drawQueue.push_back([t, r, g, b, a]() {
        ImGui::TextColored(ImVec4(r, g, b, a), "%s", t.c_str());
    });
}

// 0F12: imgui_columns
CLEO_Fn(IMGUI_COLUMNS)
{
    int count  = cleo->ReadParam(handle)->i;
    int border = cleo->ReadParam(handle)->i;
    g_drawQueue.push_back([count, border]() {
        ImGui::Columns(count, nullptr, border != 0);
    });
}

// 0F13: imgui_next_column
CLEO_Fn(IMGUI_NEXT_COLUMN)
{
    g_drawQueue.push_back([]() { ImGui::NextColumn(); });
}

// 0F14: imgui_spacing
CLEO_Fn(IMGUI_SPACING)
{
    g_drawQueue.push_back([]() { ImGui::Spacing(); });
}

// 0F15: imgui_dummy
CLEO_Fn(IMGUI_DUMMY)
{
    float x = cleo->ReadParam(handle)->f;
    float y = cleo->ReadParam(handle)->f;
    g_drawQueue.push_back([x, y]() { ImGui::Dummy(ImVec2(x, y)); });
}

// 0F16: imgui_sameline
CLEO_Fn(IMGUI_SAMELINE)
{
    g_drawQueue.push_back([]() { ImGui::SameLine(); });
}

// 0F17: imgui_slider_int
CLEO_Fn(IMGUI_SLIDER_INT)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags   = cleo->ReadParam(handle)->i;
    int* pVar   = &cleo->GetPointerToScriptVar(handle)->i;
    int minVal  = cleo->ReadParam(handle)->i;
    int maxVal  = cleo->ReadParam(handle)->i;
    int slFlags = cleo->ReadParam(handle)->i;   // ImGuiSliderFlags
    int count   = cleo->ReadParam(handle)->i;   // ignorado (solo para float)

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, minVal, maxVal, slFlags]() {
        ImGui::SliderInt(lbl.c_str(), pVar, minVal, maxVal, "%d", slFlags);
    });
}

// 0F18: imgui_slider_float
CLEO_Fn(IMGUI_SLIDER_FLOAT)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags    = cleo->ReadParam(handle)->i;
    float* pVar  = &cleo->GetPointerToScriptVar(handle)->f;
    float minVal = cleo->ReadParam(handle)->f;
    float maxVal = cleo->ReadParam(handle)->f;
    int slFlags  = cleo->ReadParam(handle)->i;
    int count    = cleo->ReadParam(handle)->i;  // formato "%f" o similar, ignorado

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, minVal, maxVal, slFlags]() {
        ImGui::SliderFloat(lbl.c_str(), pVar, minVal, maxVal, "%.3f", slFlags);
    });
}

// 0F19: imgui_color_edit
CLEO_Fn(IMGUI_COLOR_EDIT)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags      = cleo->ReadParam(handle)->i;
    float* pCol    = &cleo->GetPointerToScriptVar(handle)->f;  // asume 3 floats RGBA? o RGB?
    int editFlags  = cleo->ReadParam(handle)->i;
    int alphaFlag  = cleo->ReadParam(handle)->i;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pCol, editFlags, alphaFlag]() {
        // alphaFlag indica si se usa ColorEdit4 o ColorEdit3
        if (alphaFlag)
            ImGui::ColorEdit4(lbl.c_str(), pCol, editFlags);
        else
            ImGui::ColorEdit3(lbl.c_str(), pCol, editFlags);
    });
}

// 0F1A: imgui_color_picker
CLEO_Fn(IMGUI_COLOR_PICKER)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags      = cleo->ReadParam(handle)->i;
    float* pCol    = &cleo->GetPointerToScriptVar(handle)->f;
    int pickFlags  = cleo->ReadParam(handle)->i;
    int alphaFlag  = cleo->ReadParam(handle)->i;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pCol, pickFlags, alphaFlag]() {
        if (alphaFlag)
            ImGui::ColorPicker4(lbl.c_str(), pCol, pickFlags);
        else
            ImGui::ColorPicker3(lbl.c_str(), pCol, pickFlags);
    });
}

// 0F1B: imgui_begin_child
CLEO_Fn(IMGUI_BEGIN_CHILD)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    float w    = cleo->ReadParam(handle)->f;
    float h    = cleo->ReadParam(handle)->f;
    int border = cleo->ReadParam(handle)->i;
    int flags  = cleo->ReadParam(handle)->i;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h, border, flags]() {
        ImGui::BeginChild(lbl.c_str(), ImVec2(w, h), border != 0, flags);
    });
}

// 0F1C: imgui_end_child
CLEO_Fn(IMGUI_END_CHILD)
{
    g_drawQueue.push_back([]() { ImGui::EndChild(); });
}

// 0F1D: imgui_input_int
CLEO_Fn(IMGUI_INPUT_INT)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags      = cleo->ReadParam(handle)->i;
    int* pVar      = &cleo->GetPointerToScriptVar(handle)->i;
    int inputFlags = cleo->ReadParam(handle)->i;
    int count      = cleo->ReadParam(handle)->i;   // step/step_fast, ignorado

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, inputFlags]() {
        ImGui::InputInt(lbl.c_str(), pVar, 1, 100, inputFlags);
    });
}

// 0F1E: imgui_input_float
CLEO_Fn(IMGUI_INPUT_FLOAT)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags       = cleo->ReadParam(handle)->i;
    float* pVar     = &cleo->GetPointerToScriptVar(handle)->f;
    int inputFlags  = cleo->ReadParam(handle)->i;
    int count       = cleo->ReadParam(handle)->i;   // formato "%f", ignorado

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, inputFlags]() {
        ImGui::InputFloat(lbl.c_str(), pVar, 0.0f, 0.0f, "%.3f", inputFlags);
    });
}

// 0F25: imgui_separator
CLEO_Fn(IMGUI_SEPARATOR)
{
    g_drawQueue.push_back([]() { ImGui::Separator(); });
}

// 0F26: imgui_get_cleo_imgui_version  (directo, sin cola)
CLEO_Fn(IMGUI_GET_CLEO_IMGUI_VERSION)
{
    cleoaddon->WriteString(handle, "1.0.1");
}

// 0F27: imgui_get_version  (directo)
CLEO_Fn(IMGUI_GET_VERSION)
{
    cleoaddon->WriteString(handle, ImGui::GetVersion());
}

// 0F28: imgui_get_framerate  (directo)
CLEO_Fn(IMGUI_GET_FRAMERATE)
{
    cleo->GetPointerToScriptVar(handle)->i = (int)(ImGui::GetIO().Framerate);
}

// 0F29: imgui_color_button
CLEO_Fn(IMGUI_COLOR_BUTTON)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    float r = cleo->ReadParam(handle)->f;
    float g = cleo->ReadParam(handle)->f;
    float b = cleo->ReadParam(handle)->f;
    float a = cleo->ReadParam(handle)->f;
    int flags   = cleo->ReadParam(handle)->i;
    float w     = cleo->ReadParam(handle)->f;
    float h     = cleo->ReadParam(handle)->f;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, r, g, b, a, flags, w, h]() {
        ImGui::ColorButton(lbl.c_str(), ImVec4(r, g, b, a), flags, ImVec2(w, h));
    });
}

// 0F2A: imgui_bullet
CLEO_Fn(IMGUI_BULLET)
{
    g_drawQueue.push_back([]() { ImGui::Bullet(); });
}

// 0F2B: imgui_bullet_text
CLEO_Fn(IMGUI_BULLET_TEXT)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::BulletText("%s", t.c_str()); });
}

// 0F2C: imgui_newline
CLEO_Fn(IMGUI_NEWLINE)
{
    g_drawQueue.push_back([]() { ImGui::NewLine(); });
}

// 0F2D: imgui_set_tooltip
CLEO_Fn(IMGUI_SET_TOOLTIP)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::SetTooltip("%s", t.c_str()); });
}

// 0F2E: imgui_color_tooltip
CLEO_Fn(IMGUI_COLOR_TOOLTIP)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    float r = cleo->ReadParam(handle)->f;
    float g = cleo->ReadParam(handle)->f;
    float b = cleo->ReadParam(handle)->f;
    float a = cleo->ReadParam(handle)->f;
    int flags = cleo->ReadParam(handle)->i;
    std::string t = text;
    g_drawQueue.push_back([t, r, g, b, a, flags]() {
      float col[4] = {r, g, b, a};
      ImGui::ColorTooltip(t.c_str(), col, flags);
    });
}

// 0F2F: imgui_is_item_hovered  (directo, sin cola)
CLEO_Fn(IMGUI_IS_ITEM_HOVERED)
{
    READ_INT(id);   // ignorado
    READ_INT(flags);
    
    bool ret = ImGui::IsItemHovered(flags);
    
    RET_OP(ret);
}

// 0F30: imgui_is_item_focused
CLEO_Fn(IMGUI_IS_ITEM_FOCUSED)
{
    READ_INT(id);   // ignorado
    
    bool ret = ImGui::IsItemFocused();
    
    RET_OP(ret);
}

// 0F31: imgui_is_item_activated
CLEO_Fn(IMGUI_IS_ITEM_ACTIVATED)
{
    READ_INT(id);   // ignorado
    
    bool ret = ImGui::IsItemActivated();
    
    RET_OP(ret);
}

// 0F32: imgui_is_item_deactivated
CLEO_Fn(IMGUI_IS_ITEM_DEACTIVATED)
{
    READ_INT(id);   // ignorado
    
    bool ret = ImGui::IsItemDeactivated();
    
    RET_OP(ret);
}

// 0F33: imgui_is_item_active
CLEO_Fn(IMGUI_IS_ITEM_ACTIVE)
{
    READ_INT(id);   // ignorado
    
    bool ret = ImGui::IsItemActive();
    
    RET_OP(ret);
}

// 0F34: imgui_is_item_clicked
CLEO_Fn(IMGUI_IS_ITEM_CLICKED)
{
    READ_INT(id);   // ignorado
    READ_INT(btn);
    
    bool ret = ImGui::IsItemClicked(btn);
    
    RET_OP(ret);
}

// 0F35: imgui_is_window_hovered
CLEO_Fn(IMGUI_IS_WINDOW_HOVERED)
{
    int id    = cleo->ReadParam(handle)->i;
    int flags = cleo->ReadParam(handle)->i;
}

// 0F36: imgui_is_window_focused
CLEO_Fn(IMGUI_IS_WINDOW_FOCUSED)
{
    int id    = cleo->ReadParam(handle)->i;
    int flags = cleo->ReadParam(handle)->i;
}

// 0F37: imgui_radio_button
CLEO_Fn(IMGUI_RADIO_BUTTON)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags   = cleo->ReadParam(handle)->i;
    int* pVar   = &cleo->GetPointerToScriptVar(handle)->i;
    int value   = cleo->ReadParam(handle)->i;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, value]() {
        ImGui::RadioButton(lbl.c_str(), pVar, value);
    });
}

// 0F38: imgui_collapsing_header
CLEO_Fn(IMGUI_COLLAPSING_HEADER)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int flags = cleo->ReadParam(handle)->i;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags]() {
        ImGui::CollapsingHeader(lbl.c_str(), flags);
    });
}

// 0F39: imgui_progress_bar
CLEO_Fn(IMGUI_PROGRESS_BAR)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    float fraction = cleo->ReadParam(handle)->f;
    float w = cleo->ReadParam(handle)->f;
    float h = cleo->ReadParam(handle)->f;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, fraction, w, h]() {
        ImGui::ProgressBar(fraction, ImVec2(w, h), lbl.c_str());
    });
}

// 0F3A: imgui_get_window_posy  (directo)
CLEO_Fn(IMGUI_GET_WINDOW_POSY)
{
    int flags = cleo->ReadParam(handle)->i;
    cleo->GetPointerToScriptVar(handle)->f = ImGui::GetWindowPos().y;
}

// 0F3B: imgui_get_window_posx
CLEO_Fn(IMGUI_GET_WINDOW_POSX)
{
    int flags = cleo->ReadParam(handle)->i;
    cleo->GetPointerToScriptVar(handle)->f = ImGui::GetWindowPos().x;
}

// 0F3C: imgui_get_window_width
CLEO_Fn(IMGUI_GET_WINDOW_WIDTH)
{
    int flags = cleo->ReadParam(handle)->i;
    cleo->GetPointerToScriptVar(handle)->f = ImGui::GetWindowWidth();
}

// 0F3D: imgui_get_window_height
CLEO_Fn(IMGUI_GET_WINDOW_HEIGHT)
{
    int flags = cleo->ReadParam(handle)->i;
    cleo->GetPointerToScriptVar(handle)->f = ImGui::GetWindowHeight();
}

// 0F3E: imgui_selectable
CLEO_Fn(IMGUI_SELECTABLE)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    int* pSelected = &cleo->GetPointerToScriptVar(handle)->i;
    int flags      = cleo->ReadParam(handle)->i;
    float w        = cleo->ReadParam(handle)->f;
    float h        = cleo->ReadParam(handle)->f;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pSelected, flags, w, h]() {
        bool sel = (*pSelected != 0);
        if (ImGui::Selectable(lbl.c_str(), &sel, flags, ImVec2(w, h)))
            *pSelected = sel ? 1 : 0;
    });
}

// 0F40: imgui_load_texture  (no implementado, se necesitaría backend de texturas)
CLEO_Fn(IMGUI_LOAD_TEXTURE)
{
    // stub
    cleo->GetPointerToScriptVar(handle)->i = 0;
}

// 0F41: imgui_image  (requiere textura, inviable con cola sin backend)
CLEO_Fn(IMGUI_IMAGE)
{
    // stub
}

// 0F42: imgui_image_ex  (stub)
CLEO_Fn(IMGUI_IMAGE_EX)
{
}

// 0F43: imgui_image_button  (stub)
CLEO_Fn(IMGUI_IMAGE_BUTTON)
{
}

// 0F44: imgui_image_button_ex  (stub)
CLEO_Fn(IMGUI_IMAGE_BUTTON_EX)
{
}

// 0F46: imgui_invisible_button
CLEO_Fn(IMGUI_INVISIBLE_BUTTON)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    float w = cleo->ReadParam(handle)->f;
    float h = cleo->ReadParam(handle)->f;
    int flags = cleo->ReadParam(handle)->i;

    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h, flags]() {
        ImGui::InvisibleButton(lbl.c_str(), ImVec2(w, h), flags);
    });
}

// 0F47: imgui_drawlist_add_circle
CLEO_Fn(IMGUI_DRAWLIST_ADD_CIRCLE)
{
    float cx = cleo->ReadParam(handle)->f;
    float cy = cleo->ReadParam(handle)->f;
    float r  = cleo->ReadParam(handle)->f;
    int col  = cleo->ReadParam(handle)->i;
    int seg  = cleo->ReadParam(handle)->i;
    float thick = (float)cleo->ReadParam(handle)->i;

    g_drawQueue.push_back([cx, cy, r, col, seg, thick]() {
        ImGui::GetWindowDrawList()->AddCircle(ImVec2(cx, cy), r, col, seg, thick);
    });
}

// 0F48: imgui_drawlist_add_circle_filled
CLEO_Fn(IMGUI_DRAWLIST_ADD_CIRCLE_FILLED)
{
    float cx = cleo->ReadParam(handle)->f;
    float cy = cleo->ReadParam(handle)->f;
    float r  = cleo->ReadParam(handle)->f;
    int col  = cleo->ReadParam(handle)->i;
    int seg  = cleo->ReadParam(handle)->i;

    g_drawQueue.push_back([cx, cy, r, col, seg]() {
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cx, cy), r, col, seg);
    });
}

// 0F49: imgui_drawlist_add_rect
CLEO_Fn(IMGUI_DRAWLIST_ADD_RECT)
{
    float x1 = cleo->ReadParam(handle)->f;
    float y1 = cleo->ReadParam(handle)->f;
    float x2 = cleo->ReadParam(handle)->f;
    float y2 = cleo->ReadParam(handle)->f;
    int col  = cleo->ReadParam(handle)->i;
    int rounding = cleo->ReadParam(handle)->i;
    int corners  = cleo->ReadParam(handle)->i;
    float thick  = (float)cleo->ReadParam(handle)->i;

    g_drawQueue.push_back([x1,y1,x2,y2,col,rounding,corners,thick]() {
        ImGui::GetWindowDrawList()->AddRect(ImVec2(x1,y1), ImVec2(x2,y2), col, (float)rounding, corners, thick);
    });
}

// 0F4A: imgui_drawlist_add_rect_filled
CLEO_Fn(IMGUI_DRAWLIST_ADD_RECT_FILLED)
{
    float x1 = cleo->ReadParam(handle)->f;
    float y1 = cleo->ReadParam(handle)->f;
    float x2 = cleo->ReadParam(handle)->f;
    float y2 = cleo->ReadParam(handle)->f;
    int col  = cleo->ReadParam(handle)->i;
    int rounding = cleo->ReadParam(handle)->i;
    int corners  = cleo->ReadParam(handle)->i;

    g_drawQueue.push_back([x1,y1,x2,y2,col,rounding,corners]() {
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x1,y1), ImVec2(x2,y2), col, (float)rounding, corners);
    });
}

// 0F4B: imgui_drawlist_add_rect_filled_multicolor
CLEO_Fn(IMGUI_DRAWLIST_ADD_RECT_FILLED_MULTICOLOR)
{
    float x1 = cleo->ReadParam(handle)->f;
    float y1 = cleo->ReadParam(handle)->f;
    float x2 = cleo->ReadParam(handle)->f;
    float y2 = cleo->ReadParam(handle)->f;
    int ul = cleo->ReadParam(handle)->i;
    int ur = cleo->ReadParam(handle)->i;
    int dl = cleo->ReadParam(handle)->i;
    int dr = cleo->ReadParam(handle)->i;

    g_drawQueue.push_back([x1,y1,x2,y2,ul,ur,dl,dr]() {
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(ImVec2(x1,y1), ImVec2(x2,y2), ul, ur, dl, dr);
    });
}

// 0F4C: imgui_drawlist_add_text
CLEO_Fn(IMGUI_DRAWLIST_ADD_TEXT)
{
    char text[256];
    cleoaddon->ReadString(handle, text, sizeof(text));
    float x = cleo->ReadParam(handle)->f;
    float y = cleo->ReadParam(handle)->f;
    float size = cleo->ReadParam(handle)->f;
    int col = cleo->ReadParam(handle)->i;
    std::string t = text;
    g_drawQueue.push_back([t, x, y, size, col]() {
        ImGui::GetWindowDrawList()->AddText(ImVec2(x, y), col, t.c_str());
    });
}

// 0F4D: imgui_drawlist_add_triangle
CLEO_Fn(IMGUI_DRAWLIST_ADD_TRIANGLE)
{
    float x1 = cleo->ReadParam(handle)->f, y1 = cleo->ReadParam(handle)->f;
    float x2 = cleo->ReadParam(handle)->f, y2 = cleo->ReadParam(handle)->f;
    float x3 = cleo->ReadParam(handle)->f, y3 = cleo->ReadParam(handle)->f;
    int col = cleo->ReadParam(handle)->i;
    float thick = (float)cleo->ReadParam(handle)->i;
    g_drawQueue.push_back([=]() {
        ImGui::GetWindowDrawList()->AddTriangle(ImVec2(x1,y1), ImVec2(x2,y2), ImVec2(x3,y3), col, thick);
    });
}

// 0F4E: imgui_drawlist_add_triangle_filled
CLEO_Fn(IMGUI_DRAWLIST_ADD_TRIANGLE_FILLED)
{
    float x1 = cleo->ReadParam(handle)->f, y1 = cleo->ReadParam(handle)->f;
    float x2 = cleo->ReadParam(handle)->f, y2 = cleo->ReadParam(handle)->f;
    float x3 = cleo->ReadParam(handle)->f, y3 = cleo->ReadParam(handle)->f;
    int col = cleo->ReadParam(handle)->i;
    g_drawQueue.push_back([=]() {
        ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(x1,y1), ImVec2(x2,y2), ImVec2(x3,y3), col);
    });
}

// 0F4F: imgui_begin_main_menu_bar
CLEO_Fn(IMGUI_BEGIN_MAIN_MENU_BAR)
{
    g_drawQueue.push_back([]() { ImGui::BeginMainMenuBar(); });
}

// 0F50: imgui_end_main_menu_bar
CLEO_Fn(IMGUI_END_MAIN_MENU_BAR)
{
    g_drawQueue.push_back([]() { ImGui::EndMainMenuBar(); });
}

// 0F51: imgui_menu_item
CLEO_Fn(IMGUI_MENU_ITEM)
{
    char label[128];
    cleoaddon->ReadString(handle, label, sizeof(label));
    std::string lbl = label;
    g_drawQueue.push_back([lbl]() { ImGui::MenuItem(lbl.c_str()); });
}

// 0F52-0F55: estilos (directo, sin cola)
CLEO_Fn(IMGUI_STYLE_COLORS_CLASSIC) { ImGui::StyleColorsClassic(); }
CLEO_Fn(IMGUI_STYLE_COLORS_DARK)    { ImGui::StyleColorsDark(); }
CLEO_Fn(IMGUI_STYLE_COLORS_DEFAULT) { ImGui::StyleColorsLight(); /* o Default */ }
CLEO_Fn(IMGUI_STYLE_COLORS_LIGHT)   { ImGui::StyleColorsLight(); }

// 0F57: imgui_get_style  (directo)
CLEO_Fn(IMGUI_GET_STYLE)
{
    int off = cleo->ReadParam(handle)->i;
    // Los offsets están definidos en ImGuiStyleVar, mapearlos es complejo; stub.
    cleo->GetPointerToScriptVar(handle)->f = 0.0f;
}

// 0F58: imgui_set_style  (directo)
CLEO_Fn(IMGUI_SET_STYLE)
{
    int off = cleo->ReadParam(handle)->i;
    float val = cleo->ReadParam(handle)->f;
    // stub
}

// 0F59: imgui_set_style_int
CLEO_Fn(IMGUI_SET_STYLE_INT)
{
    int off = cleo->ReadParam(handle)->i;
    int val = cleo->ReadParam(handle)->i;
}

// 0F5A: imgui_get_color
CLEO_Fn(IMGUI_GET_COLOR)
{
    int off = cleo->ReadParam(handle)->i;
    float r,g,b,a;
    // stub
    cleo->GetPointerToScriptVar(handle)->f = 0; // r
    cleo->GetPointerToScriptVar(handle)->f = 0; // g
    cleo->GetPointerToScriptVar(handle)->f = 0; // b
    cleo->GetPointerToScriptVar(handle)->f = 0; // a
}

// 0F5B: imgui_set_color
CLEO_Fn(IMGUI_SET_COLOR)
{
    int off = cleo->ReadParam(handle)->i;
    float r = cleo->ReadParam(handle)->f;
    float g = cleo->ReadParam(handle)->f;
    float b = cleo->ReadParam(handle)->f;
    float a = cleo->ReadParam(handle)->f;
    // stub
}

// 0F5C: imgui_push_item_width
CLEO_Fn(IMGUI_PUSH_ITEM_WIDTH)
{
    float w = cleo->ReadParam(handle)->f;
    g_drawQueue.push_back([w]() { ImGui::PushItemWidth(w); });
}

// 0F5D: imgui_pop_item_width
CLEO_Fn(IMGUI_POP_ITEM_WIDTH)
{
    g_drawQueue.push_back([]() { ImGui::PopItemWidth(); });
}

// 0F5E: imgui_push_item_flag
CLEO_Fn(IMGUI_PUSH_ITEM_FLAG)
{
    int flag = cleo->ReadParam(handle)->i;
    int en   = cleo->ReadParam(handle)->i;
    g_drawQueue.push_back([flag, en]() { ImGui::PushItemFlag(flag, en != 0); });
}

// 0F5F: imgui_pop_item_flag
CLEO_Fn(IMGUI_POP_ITEM_FLAG)
{
    g_drawQueue.push_back([]() { ImGui::PopItemFlag(); });
}

// 0F60: imgui_get_window_content_region_width  (directo)
CLEO_Fn(IMGUI_GET_WINDOW_CONTENT_REGION_WIDTH)
{
    int flags = cleo->ReadParam(handle)->i;
    cleo->GetPointerToScriptVar(handle)->f;//= ImGui::GetWindowContentRegionWidth();
}

// 0F61: imgui_get_frame_height
CLEO_Fn(IMGUI_GET_FRAME_HEIGHT)
{
    int flags = cleo->ReadParam(handle)->i;
    cleo->GetPointerToScriptVar(handle)->f = ImGui::GetFrameHeight();
}

// 0F62: imgui_get_frame_height_with_spacing
CLEO_Fn(IMGUI_GET_FRAME_HEIGHT_WITH_SPACING)
{
    cleo->GetPointerToScriptVar(handle)->f = ImGui::GetFrameHeightWithSpacing();
}

// 0F63: imgui_get_style_int  (directo)
CLEO_Fn(IMGUI_GET_STYLE_INT)
{
    int off = cleo->ReadParam(handle)->i;
    cleo->GetPointerToScriptVar(handle)->i = 0;
}

// ---------- Entradas del módulo ----------
extern "C" void OnModPreLoad()
{
    logger->SetTag("CLEO ImGui");
    pGameLib = aml->GetLib("libGTASA.so");
    if (!pGameLib) { logger->Error("Cannot find libGTASA.so"); return; }
    pGameHandle = aml->GetLibHandle("libGTASA.so");

    SET_TO(nearScreenZ, aml->GetSym(pGameHandle, "_ZN9CSprite2d11NearScreenZE"));
    SET_TO(recipNearClip, aml->GetSym(pGameHandle, "_ZN9CSprite2d13RecipNearClipE"));
    SET_TO(SetScissorRect, aml->GetSym(pGameHandle, "_ZN7CWidget10SetScissorER5CRect"));
    SET_TO(GetScreenFadeStatus, aml->GetSym(pGameHandle, "_ZN7CCamera19GetScreenFadeStatusEv"));
    SET_TO(pTheCamera, aml->GetSym(pGameHandle, "TheCamera"));
    RegisterInterface("ImGui", pImGui);
}

extern "C" void OnModLoad()
{
    if (!pGameLib) return;

    HOOKPLT(InitRenderware, pGameLib + 0x66F2D0);
    HOOKPLT(OnTouchEvent, pGameLib + 0x675DE4);
    HOOKPLT(GTA_KeyboardEvent, pGameLib + 0x6709B8);
    HOOK(Render2DStuff, aml->GetSym(pGameHandle, "_Z13Render2dStuffv"));

    SET_TO(m_bMenuOpened, pGameLib + 0x6E0098);
    SET_TO(m_UserPause, aml->GetSym(pGameHandle, "_ZN6CTimer11m_UserPauseE"));
    SET_TO(GTA_RequestKeyboard, aml->GetSym(pGameHandle, "_Z18OS_KeyboardRequesti"));
    SET_TO(NVtoKK, aml->GetSym(pGameHandle, "NVtoKK"));
    SET_TO(KKtoChar, aml->GetSym(pGameHandle, "KKtoChar"));
    SET_TO(KKtoShiftedChar, aml->GetSym(pGameHandle, "KKtoShiftedChar"));

    cleo = (cleo_ifs_t*)GetInterface("CLEO");
    if (!cleo) { logger->Error("CLEO interface not found"); return; }
    cleoaddon = (cleo_addon_ifs_t*)GetInterface("CLEOAddon");
    if (!cleoaddon) { logger->Error("CLEOAddon interface not found"); return; }

    CLEO_RegisterOpcode(0x0F01, IMGUI_BEGIN);
CLEO_RegisterOpcode(0x0F02, IMGUI_END);
CLEO_RegisterOpcode(0x0F03, IMGUI_CHECKBOX);
CLEO_RegisterOpcode(0x0F04, IMGUI_BUTTON);
CLEO_RegisterOpcode(0x0F0E, IMGUI_TEXT);
CLEO_RegisterOpcode(0x0F0F, IMGUI_TEXT_WRAPPED);
CLEO_RegisterOpcode(0x0F10, IMGUI_TEXT_DISABLED);
CLEO_RegisterOpcode(0x0F11, IMGUI_TEXT_COLORED);
CLEO_RegisterOpcode(0x0F12, IMGUI_COLUMNS);
CLEO_RegisterOpcode(0x0F13, IMGUI_NEXT_COLUMN);
CLEO_RegisterOpcode(0x0F14, IMGUI_SPACING);
CLEO_RegisterOpcode(0x0F15, IMGUI_DUMMY);
CLEO_RegisterOpcode(0x0F16, IMGUI_SAMELINE);
CLEO_RegisterOpcode(0x0F17, IMGUI_SLIDER_INT);
CLEO_RegisterOpcode(0x0F18, IMGUI_SLIDER_FLOAT);
CLEO_RegisterOpcode(0x0F19, IMGUI_COLOR_EDIT);
CLEO_RegisterOpcode(0x0F1A, IMGUI_COLOR_PICKER);
CLEO_RegisterOpcode(0x0F1B, IMGUI_BEGIN_CHILD);
CLEO_RegisterOpcode(0x0F1C, IMGUI_END_CHILD);
CLEO_RegisterOpcode(0x0F1D, IMGUI_INPUT_INT);
CLEO_RegisterOpcode(0x0F1E, IMGUI_INPUT_FLOAT);
CLEO_RegisterOpcode(0x0F25, IMGUI_SEPARATOR);
CLEO_RegisterOpcode(0x0F26, IMGUI_GET_CLEO_IMGUI_VERSION);
CLEO_RegisterOpcode(0x0F27, IMGUI_GET_VERSION);
CLEO_RegisterOpcode(0x0F28, IMGUI_GET_FRAMERATE);
CLEO_RegisterOpcode(0x0F29, IMGUI_COLOR_BUTTON);
CLEO_RegisterOpcode(0x0F2A, IMGUI_BULLET);
CLEO_RegisterOpcode(0x0F2B, IMGUI_BULLET_TEXT);
CLEO_RegisterOpcode(0x0F2C, IMGUI_NEWLINE);
CLEO_RegisterOpcode(0x0F2D, IMGUI_SET_TOOLTIP);
CLEO_RegisterOpcode(0x0F2E, IMGUI_COLOR_TOOLTIP);
CLEO_RegisterOpcode(0x0F2F, IMGUI_IS_ITEM_HOVERED);
CLEO_RegisterOpcode(0x0F30, IMGUI_IS_ITEM_FOCUSED);
CLEO_RegisterOpcode(0x0F31, IMGUI_IS_ITEM_ACTIVATED);
CLEO_RegisterOpcode(0x0F32, IMGUI_IS_ITEM_DEACTIVATED);
CLEO_RegisterOpcode(0x0F33, IMGUI_IS_ITEM_ACTIVE);
CLEO_RegisterOpcode(0x0F34, IMGUI_IS_ITEM_CLICKED);
CLEO_RegisterOpcode(0x0F35, IMGUI_IS_WINDOW_HOVERED);
CLEO_RegisterOpcode(0x0F36, IMGUI_IS_WINDOW_FOCUSED);
CLEO_RegisterOpcode(0x0F37, IMGUI_RADIO_BUTTON);
CLEO_RegisterOpcode(0x0F38, IMGUI_COLLAPSING_HEADER);
CLEO_RegisterOpcode(0x0F39, IMGUI_PROGRESS_BAR);
CLEO_RegisterOpcode(0x0F3A, IMGUI_GET_WINDOW_POSY);
CLEO_RegisterOpcode(0x0F3B, IMGUI_GET_WINDOW_POSX);
CLEO_RegisterOpcode(0x0F3C, IMGUI_GET_WINDOW_WIDTH);
CLEO_RegisterOpcode(0x0F3D, IMGUI_GET_WINDOW_HEIGHT);
CLEO_RegisterOpcode(0x0F3E, IMGUI_SELECTABLE);
CLEO_RegisterOpcode(0x0F40, IMGUI_LOAD_TEXTURE);
CLEO_RegisterOpcode(0x0F41, IMGUI_IMAGE);
CLEO_RegisterOpcode(0x0F42, IMGUI_IMAGE_EX);
CLEO_RegisterOpcode(0x0F43, IMGUI_IMAGE_BUTTON);
CLEO_RegisterOpcode(0x0F44, IMGUI_IMAGE_BUTTON_EX);
CLEO_RegisterOpcode(0x0F46, IMGUI_INVISIBLE_BUTTON);
CLEO_RegisterOpcode(0x0F47, IMGUI_DRAWLIST_ADD_CIRCLE);
CLEO_RegisterOpcode(0x0F48, IMGUI_DRAWLIST_ADD_CIRCLE_FILLED);
CLEO_RegisterOpcode(0x0F49, IMGUI_DRAWLIST_ADD_RECT);
CLEO_RegisterOpcode(0x0F4A, IMGUI_DRAWLIST_ADD_RECT_FILLED);
CLEO_RegisterOpcode(0x0F4B, IMGUI_DRAWLIST_ADD_RECT_FILLED_MULTICOLOR);
CLEO_RegisterOpcode(0x0F4C, IMGUI_DRAWLIST_ADD_TEXT);
CLEO_RegisterOpcode(0x0F4D, IMGUI_DRAWLIST_ADD_TRIANGLE);
CLEO_RegisterOpcode(0x0F4E, IMGUI_DRAWLIST_ADD_TRIANGLE_FILLED);
CLEO_RegisterOpcode(0x0F4F, IMGUI_BEGIN_MAIN_MENU_BAR);
CLEO_RegisterOpcode(0x0F50, IMGUI_END_MAIN_MENU_BAR);
CLEO_RegisterOpcode(0x0F51, IMGUI_MENU_ITEM);
CLEO_RegisterOpcode(0x0F52, IMGUI_STYLE_COLORS_CLASSIC);
CLEO_RegisterOpcode(0x0F53, IMGUI_STYLE_COLORS_DARK);
CLEO_RegisterOpcode(0x0F54, IMGUI_STYLE_COLORS_DEFAULT);
CLEO_RegisterOpcode(0x0F55, IMGUI_STYLE_COLORS_LIGHT);
CLEO_RegisterOpcode(0x0F57, IMGUI_GET_STYLE);
CLEO_RegisterOpcode(0x0F58, IMGUI_SET_STYLE);
CLEO_RegisterOpcode(0x0F59, IMGUI_SET_STYLE_INT);
CLEO_RegisterOpcode(0x0F5A, IMGUI_GET_COLOR);
CLEO_RegisterOpcode(0x0F5B, IMGUI_SET_COLOR);
CLEO_RegisterOpcode(0x0F5C, IMGUI_PUSH_ITEM_WIDTH);
CLEO_RegisterOpcode(0x0F5D, IMGUI_POP_ITEM_WIDTH);
CLEO_RegisterOpcode(0x0F5E, IMGUI_PUSH_ITEM_FLAG);
CLEO_RegisterOpcode(0x0F5F, IMGUI_POP_ITEM_FLAG);
CLEO_RegisterOpcode(0x0F60, IMGUI_GET_WINDOW_CONTENT_REGION_WIDTH);
CLEO_RegisterOpcode(0x0F61, IMGUI_GET_FRAME_HEIGHT);
CLEO_RegisterOpcode(0x0F62, IMGUI_GET_FRAME_HEIGHT_WITH_SPACING);
CLEO_RegisterOpcode(0x0F63, IMGUI_GET_STYLE_INT);

    logger->Info("CLEO opcodes for ImGui registered.");
}