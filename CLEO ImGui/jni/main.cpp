#include <mod/amlmod.h>
#include <mod/logger.h>

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <fstream>

#include "main.h"
#include "arial.h"

#include "cleo.h"
cleo_ifs_t* cleo = nullptr;

#include "cleoaddon.h"
cleo_addon_ifs_t* cleoaddon = nullptr;

#include "isautils.h"
ISAUtils* sautils = nullptr;
char szCLEOImGuiVer[64] { 0 };
void NoneFunctionLogic(uintptr_t) { return; }

#include "RW/rwcore.h" 

MYMOD(net.matidragon.cleo_imgui, CLEO_ImGui, 1.3.0, MatiDragon)
BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.cleolib, 2.0.1.10)
END_DEPLIST()

// ======================== OPCODES CLEO ========================
#define CLEO_RegisterOpcode(x, h) cleo->RegisterOpcode(x, h); cleo->RegisterOpcodeFunction(#h, h)
#define CLEO_Fn(h) void h (void *handle, uint32_t *ip, uint16_t opcode, const char *name)
#define MAX_STR_LEN 0xFF

// ── Macros para reducir código repetitivo ─────────────────
#define READ_STRING(v, s) char v[s]; cleoaddon->ReadString(handle, v, s)
#define READ_INT(__name) int __name = cleo->ReadParam(handle)->i
#define READ_FLOAT(__name) float __name = cleo->ReadParam(handle)->f
#define READ_INT_PTR(__name) int* __name = &cleo->GetPointerToScriptVar(handle)->i
#define READ_FLOAT_PTR(__name) float* __name = &cleo->GetPointerToScriptVar(handle)->f
#define WRITE_INT(val) cleo->GetPointerToScriptVar(handle)->i = val
#define WRITE_FLOAT(val) cleo->GetPointerToScriptVar(handle)->f = val
#define WRITE_STRING(val) cleoaddon->WriteString(handle, val);
#define RET_COMPARE(ret) cleoaddon->UpdateCompareFlag(handle, ret);

// ── Macros para widgets con/sin cola ──────────────────────
#define QUEUE_PUSH(body) g_drawQueue.push_back([=]() body)
#define SIMPLE_WIDGET(body) CLEO_Fn(body) { g_drawQueue.push_back([]() { body; }); }
#define DIRECT_GETTER(body) CLEO_Fn(body)

// Consume el primer bool pendiente del script 'handle' y actualiza la bandera de condición.
// Úsala al inicio de cada opcode condicional que use el sistema de cola.
#define APPLY_DEFERRED_COND()                          \
    auto& _pending_ = g_pendingResults[handle];        \
    bool _ret_ = false;                                \
    if (!_pending_.empty()) {                          \
        _ret_ = _pending_.front();                     \
        _pending_.erase(_pending_.begin());            \
    }                                                  \
    RET_COMPARE(_ret_)
#define PUSH_DEFERRED_BOOL(expr)  g_pendingResults[handle].push_back(expr)


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
extern RwRaster* (*RwRasterRead)(const RwChar*);
extern RwImage* (*RwImageRead)(const RwChar*);
extern RwRaster* (*RwRasterSetFromImage)(RwRaster*, RwImage*);
extern RwBool (*RwImageDestroy)(RwImage * image);
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
// Cola de resultados por script (para condiciones con retardo)
static std::map<void*, std::vector<bool>> g_pendingResults;
// Buffers persistentes para InputText (asociados a la variable del script)
static std::map<int*, std::string> g_inputTextBuffers;

#define FRAMES_TO_CLEAR_MOUSE 3
static char nClearMousePos = 0;
ImFont* kbFont;

// ── Imágenes para ImGui ──────────────────────────
static std::map<int, void*> g_images;
static int g_nextImageId = 1;
static ImVec4 g_imageBgColor   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // transparente por defecto
static ImVec4 g_imageTintColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // blanco por defecto
static ImVec4 g_imageBorderColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // transparente por defecto
static ImVec2 g_imageUV0 = ImVec2(0.0f, 0.0f);
static ImVec2 g_imageUV1 = ImVec2(1.0f, 1.0f);
static std::map<std::string, int> g_imagePathToId;   // ruta absoluta -> id de imagen


static int AddImage(void* texture) {
    if (!texture) return 0;
    int id = g_nextImageId++;
    g_images[id] = texture;
    return id;
}

static void* GetImage(int id) {
    auto it = g_images.find(id);
    if (it == g_images.end()) return nullptr;
    return it->second;
}

// ── Sistema de teclado virtual (edición en vivo) ─────
enum VirtualKeyboardType {
    VK_NONE = 0,
    VK_TEXT,
    VK_NUMERIC
};

struct VirtualKeyboard {
    bool open = false;
    VirtualKeyboardType type = VK_NONE;

    std::string buffer;          // texto que se muestra en el teclado

    // Punteros a la variable original
    int*   pInt   = nullptr;
    float* pFloat = nullptr;
    char*  pText  = nullptr;
    int    textMaxLen = 0;

    ImVec2 windowPos = ImVec2(200, 200);
    ImVec2 windowSize = ImVec2(380, 280);
};
static VirtualKeyboard g_vk;
static bool shift = false;   // estado del shift
static bool g_vkSymbols = false;   // true = layout de símbolos, false = letras
static int g_textInputLimit = 256;   // límite por defecto
static bool g_vkEnterMode = false;   // 0 = cerrar teclado, 1 = insertar \n

// Abrir para editar un int (convierte el valor actual a texto)
static void VK_OpenForInt(int* p) {
    g_vk.open = true;
    g_vk.type = VK_NUMERIC;
    g_vk.pInt = p;
    g_vk.pFloat = nullptr;
    g_vk.pText = nullptr;
    g_vk.buffer = std::to_string(*p);
}

// Abrir para editar un float
static void VK_OpenForFloat(float* p) {
    g_vk.open = true;
    g_vk.type = VK_NUMERIC;
    g_vk.pInt = nullptr;
    g_vk.pFloat = p;
    g_vk.pText = nullptr;
    g_vk.buffer = std::to_string(*p);
}

// Abrir para editar texto (cuando implementes InputText)
static void VK_OpenForText(char* p, int maxLen) {
    g_vk.open = true;
    g_vk.type = VK_TEXT;
    g_vk.pInt = nullptr;
    g_vk.pFloat = nullptr;
    g_vk.pText = p;
    g_vk.textMaxLen = maxLen;
    g_vk.buffer = p ? p : "";
}

// Cierra el teclado (los cambios ya se aplicaron en vivo)
static void VK_Close() {
    g_vk.open = false;
    g_vk.type = VK_NONE;
    g_vk.pInt = nullptr;
    g_vk.pFloat = nullptr;
    g_vk.pText = nullptr;
    g_vk.buffer.clear();
}

// Aplica el buffer actual a la variable y fuerza el teclado al frente
static void VK_ApplyAndFocus() {
    if (g_vk.pInt) {
        *g_vk.pInt = std::atoi(g_vk.buffer.c_str());
    }
    else if (g_vk.pFloat) {
        *g_vk.pFloat = (float)std::atof(g_vk.buffer.c_str());
    }
    else if (g_vk.pText && g_vk.textMaxLen > 0) {
        std::strncpy(g_vk.pText, g_vk.buffer.c_str(), g_vk.textMaxLen - 1);
        g_vk.pText[g_vk.textMaxLen - 1] = '\0';
    }

    // Forzar que la ventana del teclado sea la más al frente
    ImGui::SetWindowFocus("Teclado##VK");
}



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

DECL_HOOKv(Render2DStuff)
{
    Render2DStuff();

    ImGui_ImplRenderWare_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();

    // ── Ejecutar los comandos encolados por CLEO ─────
    for (auto& cmd : g_drawQueue) cmd();
    g_drawQueue.clear();
    
// ── Teclado virtual (edición en vivo, siempre al frente) ──
if (g_vk.open) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.32f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.42f, 0.42f, 0.42f, 1.00f));

    // ImGui::SetNextWindowSize(g_vk.windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowPos(g_vk.windowPos, ImGuiCond_Once);

    // No usamos la variable 'open' para cerrar; solo con la X
    if (ImGui::Begin("Teclado##VK", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {

        // Mostrar el texto actual
        ImGui::Text(" > ");
        ImGui::SameLine();
        ImGui::TextUnformatted(g_vk.buffer.c_str());

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
        if (ImGui::Button("X", ImVec2(40, 40))) {
            VK_Close();
        }
        
        ImGui::Separator();

        // ── Layout según tipo ─────────────────
        if (g_vk.type == VK_NUMERIC) {
            const char* rows[] = {"789", "456", "123", "0.-"};
            for (int r = 0; r < 4; r++) {
                for (int i = 0; rows[r][i]; i++) {
                    char lbl[2] = { rows[r][i], 0 };
                    if (ImGui::Button(lbl, ImVec2(60, 60))) {
                        g_vk.buffer += rows[r][i];
                        VK_ApplyAndFocus();
                    }
                    ImGui::SameLine(0, 4);
                }
                ImGui::NewLine();
            }
            if (ImGui::Button("<<", ImVec2(180, 60))) {
                if (!g_vk.buffer.empty()) {
                    g_vk.buffer.pop_back();
                    VK_ApplyAndFocus();
                }
            }
        }
        else if (g_vk.type == VK_TEXT) {
    const char* rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (int r = 0; r < 4; r++) {
        for (int i = 0; rows[r][i]; i++) {
            char c = rows[r][i];
            if (shift && (c >= 'a' && c <= 'z'))
                c = toupper(c);
            char lbl[2] = { c, 0 };
            if (ImGui::Button(lbl, ImVec2(60, 60))) {
                g_vk.buffer += c;
                VK_ApplyAndFocus();
            }
            ImGui::SameLine(0, 2);
            
            if (c == 'l' || c == 'L')
            {
              if (ImGui::Button("[", ImVec2(60, 60))) {
                g_vk.buffer += '[';
                VK_ApplyAndFocus();
              }
              ImGui::SameLine(0, 2);
            }
            
            if (c == 'm' || c == 'M')
            {
              if (ImGui::Button("<", ImVec2(60, 60))) {
                g_vk.buffer += '<';
                VK_ApplyAndFocus();
              }
              ImGui::SameLine(0, 2);
              if (ImGui::Button(">", ImVec2(60, 60))) {
                g_vk.buffer += '>';
                VK_ApplyAndFocus();
              }
              ImGui::SameLine(0, 2);
              if (ImGui::Button("]", ImVec2(60, 60))) {
                g_vk.buffer += ']';
                VK_ApplyAndFocus();
              }
              ImGui::SameLine(0, 2);
            }
        }
        ImGui::NewLine();
    }
            if (ImGui::Button(shift ? "ABC" : "abc", ImVec2(60, 60))) {
              shift = !shift;
            }
            ImGui::SameLine(0, 2);
            if (ImGui::Button("@", ImVec2(60, 60))) {
                g_vk.buffer += '@';
                VK_ApplyAndFocus();
            }
            ImGui::SameLine(0, 2);
            if (ImGui::Button("!", ImVec2(60, 60))) {
                g_vk.buffer += '!';
                VK_ApplyAndFocus();
            }
            ImGui::SameLine(0, 2);
            if (ImGui::Button("?", ImVec2(60, 60))) {
                g_vk.buffer += '?';
                VK_ApplyAndFocus();
            }
            ImGui::SameLine(0, 2);
            if (ImGui::Button("", ImVec2(164, 60))) {
                g_vk.buffer += ' ';
                VK_ApplyAndFocus();
            }
            ImGui::SameLine(0, 2);
            if (ImGui::Button(",", ImVec2(60, 60))) {
                g_vk.buffer += ',';
                VK_ApplyAndFocus();
            }
            ImGui::SameLine(0, 2);
            if (ImGui::Button(".", ImVec2(60, 60))) {
                g_vk.buffer += '.';
                VK_ApplyAndFocus();
            }
            ImGui::SameLine(0, 2);
            if (ImGui::Button("<<", ImVec2(80, 60))) {
                if (!g_vk.buffer.empty()) {
                    g_vk.buffer.pop_back();
                    VK_ApplyAndFocus();
                }
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    // Guardar posición para la próxima apertura
    g_vk.windowPos = ImGui::GetWindowPos();
    g_vk.windowSize = ImGui::GetWindowSize();
}
    
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
    return ImGui::IsAnyItemHovered();
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

// 0F01: imgui_begin
CLEO_Fn(IMGUI_BEGIN_A)
{
    READ_STRING(label, 128);
    READ_INT(shown);    // bool
    READ_INT(flags);    // ImGuiWindowFlags
    READ_INT(mouseVis); //  ignorado de momento
    
    std::string windowName = label;
    g_drawQueue.push_back([windowName, flags, mouseVis]() {
        // ImGui::SetNextWindowBgAlpha(1.0f); // etc si quieres usar mouseVis
        ImGui::Begin(windowName.c_str(), nullptr, flags);
    });
}
/*
// 2202: imgui_begin
CLEO_Fn(IMGUI_BEGIN_B)
{
    READ_STRING(label, 128);
    READ_INT(flags);    // ImGuiWindowFlags
    READ_INT(e1);    // bool
    READ_INT(d1); //  ignorado de momento
    READ_INT(a1); //  ignorado de momento
    READ_INT(b1); //  ignorado de momento
    READ_INT(c1); //  ignorado de momento
    
    std::string windowName = label;
    g_drawQueue.push_back([windowName, flags]() {
        // ImGui::SetNextWindowBgAlpha(1.0f); // etc si quieres usar mouseVis
        ImGui::Begin(windowName.c_str(), nullptr, flags);
    });

}
*/
// 2202: imgui_begin
// 2202: imgui_begin (sin state, usa variable de salida como estado)
CLEO_Fn(IMGUI_BEGIN_B)
{
    READ_STRING(label, 128);
    READ_INT(state); // not working 
    READ_INT(noTitleBar);
    READ_INT(noResize);
    READ_INT(noMove);
    READ_INT(autoResize);
    READ_INT_PTR(pState);        // variable de salida/estado (al final)

    std::string windowName = label;

    g_drawQueue.push_back([windowName, noTitleBar, noResize, noMove, autoResize, pState]() {
        // Leer estado inicial desde la variable
        bool open = (*pState != 0);

        ImGuiWindowFlags flags = 0;
        if (noTitleBar) flags |= ImGuiWindowFlags_NoTitleBar;
        if (noResize)   flags |= ImGuiWindowFlags_NoResize;
        if (noMove)     flags |= ImGuiWindowFlags_NoMove;
        if (autoResize) flags |= ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::Begin(windowName.c_str(), &open, flags);

        // Actualizar la variable con el nuevo estado (por si cambió)
        *pState = open ? 1 : 0;
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
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_INT_PTR(pVar);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, pVar, handle]() {
        bool val = (*pVar != 0);
        bool changed = ImGui::Checkbox(lbl.c_str(), &val);
        *pVar = val ? 1 : 0;
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F04: imgui_button
CLEO_Fn(IMGUI_BUTTON)
{
    READ_STRING(label, 128);
    READ_FLOAT(w);
    READ_FLOAT(h);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h, handle]() {
        bool pressed = ImGui::Button(lbl.c_str(), ImVec2(w, h));
        PUSH_DEFERRED_BOOL(pressed);
    });
}

// 0F05: imgui_calc_text_height
CLEO_Fn(IMGUI_CALC_TEXT_HEIGHT)
{
    READ_STRING(text, MAX_STR_LEN);
    READ_INT(hide_after);       // bool
    READ_FLOAT(wrap_width);
    READ_INT(flags);            // imguitypeflags (ignorado)
    READ_FLOAT_PTR(pOut);       // salida

    std::string t = text;
    g_drawQueue.push_back([t, hide_after, wrap_width, pOut]() {
        float height = ImGui::CalcTextSize(t.c_str(), NULL, hide_after != 0, wrap_width).y;
        *pOut = height;
    });
}

// 0F06: imgui_calc_text_width
CLEO_Fn(IMGUI_CALC_TEXT_WIDTH)
{
    READ_STRING(text, MAX_STR_LEN);
    READ_INT(hide_after);
    READ_FLOAT(wrap_width);
    READ_INT(flags);
    READ_FLOAT_PTR(pOut);

    std::string t = text;
    g_drawQueue.push_back([t, hide_after, wrap_width, pOut]() {
        float width = ImGui::CalcTextSize(t.c_str(), NULL, hide_after != 0, wrap_width).x;
        *pOut = width;
    });
}

// 0F07: imgui_set_next_window_pos
CLEO_Fn(IMGUI_SET_NEXT_WINDOW_POS)
{
    READ_FLOAT(x);
    READ_FLOAT(y);
    READ_INT(cond);

    g_drawQueue.push_back([x, y, cond]() {
        ImGui::SetNextWindowPos(ImVec2(x, y), cond);
    });
}

// 0F08: imgui_set_window_pos
CLEO_Fn(IMGUI_SET_WINDOW_POS)
{
    READ_FLOAT(x);
    READ_FLOAT(y);
    READ_INT(cond);

    g_drawQueue.push_back([x, y, cond]() {
        ImGui::SetWindowPos(ImVec2(x, y), cond);
    });
}

// 0F09: imgui_get_font_size
CLEO_Fn(IMGUI_GET_FONT_SIZE)
{
    READ_FLOAT_PTR(pOut);

    g_drawQueue.push_back([pOut]() {
        *pOut = ImGui::GetFontSize();
    });
}

// 0F0A: imgui_set_next_window_size
CLEO_Fn(IMGUI_SET_NEXT_WINDOW_SIZE)
{
    READ_FLOAT(width);
    READ_FLOAT(height);
    READ_INT(cond);

    g_drawQueue.push_back([width, height, cond]() {
        ImGui::SetNextWindowSize(ImVec2(width, height), cond);
    });
}

// 0F0B: imgui_set_window_size
CLEO_Fn(IMGUI_SET_WINDOW_SIZE)
{
    READ_FLOAT(width);
    READ_FLOAT(height);
    READ_INT(cond);

    g_drawQueue.push_back([width, height, cond]() {
        ImGui::SetWindowSize(ImVec2(width, height), cond);
    });
}

// 0F0E: imgui_text
CLEO_Fn(IMGUI_TEXT)
{
    READ_STRING(text, MAX_STR_LEN);

    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::TextUnformatted(t.c_str()); });
}

// 0F0F: imgui_text_wrapped
CLEO_Fn(IMGUI_TEXT_WRAPPED)
{
    READ_STRING(text, MAX_STR_LEN);
    
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::TextWrapped("%s", t.c_str()); });
}

// 0F10: imgui_text_disabled
CLEO_Fn(IMGUI_TEXT_DISABLED)
{
    READ_STRING(text, MAX_STR_LEN);
    
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::TextDisabled("%s", t.c_str()); });
}

// 0F11: imgui_text_colored
CLEO_Fn(IMGUI_TEXT_COLORED)
{
    READ_STRING(text, MAX_STR_LEN);
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);

    std::string t = text;
    g_drawQueue.push_back([t, r, g, b, a]() {
        ImGui::TextColored(ImVec4(r, g, b, a), "%s", t.c_str());
    });
}

// 0F12: imgui_columns
CLEO_Fn(IMGUI_COLUMNS)
{
    READ_INT(count);
    READ_INT(border);
    
    g_drawQueue.push_back([count, border]() {
        ImGui::Columns(count, nullptr, border != 0);
    });
}
CLEO_Fn(IMGUI_COLUMNS_B)
{
    READ_INT(count);
    
    g_drawQueue.push_back([count]() {
        ImGui::Columns(count, nullptr, 0);
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
    READ_FLOAT(x);
    READ_FLOAT(y);

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
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_INT_PTR(pVar);
    READ_INT(minVal);
    READ_INT(maxVal);
    READ_INT(slFlags);
    READ_INT(count);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, minVal, maxVal, slFlags, handle]() {
        bool changed = ImGui::SliderInt(lbl.c_str(), pVar, minVal, maxVal, "%d", slFlags);
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F18: imgui_slider_float
CLEO_Fn(IMGUI_SLIDER_FLOAT)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_FLOAT_PTR(pVar);
    READ_FLOAT(minVal);
    READ_FLOAT(maxVal);
    READ_INT(slFlags);
    READ_INT(count);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, minVal, maxVal, slFlags, handle]() {
        bool changed = ImGui::SliderFloat(lbl.c_str(), pVar, minVal, maxVal, "%.3f", slFlags);
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 221D: imgui_slider_int (compatible con PC, init ignorado)
CLEO_Fn(IMGUI_SLIDER_INT_B)
{
    READ_STRING(label, 128);
    READ_INT(init);        // ignorado, solo se consume de la pila
    READ_INT(minVal);
    READ_INT(maxVal);
    READ_INT_PTR(pVar);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, minVal, maxVal, handle,pVar]() {
        // Obtenemos la variable de salida en cada frame de forma segura
        bool changed = ImGui::SliderInt(lbl.c_str(), pVar, minVal, maxVal, "%d", 0);
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 221E: imgui_slider_float (compatible con PC, init ignorado)
CLEO_Fn(IMGUI_SLIDER_FLOAT_B)
{
    READ_STRING(label, 128);
    READ_FLOAT(init);      // ignorado
    READ_FLOAT(minVal);
    READ_FLOAT(maxVal);
    READ_FLOAT_PTR(pVar);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, minVal, maxVal, handle, pVar]() {
        
        bool changed = ImGui::SliderFloat(lbl.c_str(), pVar, minVal, maxVal, "%.3f", 0);
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F19: imgui_color_edit
CLEO_Fn(IMGUI_COLOR_EDIT)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_FLOAT_PTR(pCol);
    READ_INT(editFlags);
    READ_INT(alphaFlag);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pCol, editFlags, alphaFlag, handle]() {
        bool changed = false;

        if (alphaFlag)
            changed = ImGui::ColorEdit4(lbl.c_str(), pCol, editFlags);
        else
            changed = ImGui::ColorEdit3(lbl.c_str(), pCol, editFlags);
        
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F1A: imgui_color_picker
CLEO_Fn(IMGUI_COLOR_PICKER)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_FLOAT_PTR(pCol);
    READ_INT(pickFlags);
    READ_INT(alphaFlag);

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pCol, pickFlags, alphaFlag, handle]() {
        bool changed = false;

        if (alphaFlag)
            changed = ImGui::ColorPicker4(lbl.c_str(), pCol, pickFlags);
        else
            changed = ImGui::ColorPicker3(lbl.c_str(), pCol, pickFlags);
        
        PUSH_DEFERRED_BOOL(changed);
    });
}

CLEO_Fn(IMGUI_COLOR_PICKER_B)
{
    READ_STRING(label, 128);
    READ_INT_PTR(pR);
    READ_INT_PTR(pG);
    READ_INT_PTR(pB);
    READ_INT_PTR(pA);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pR, pG, pB, pA, handle]() {
        // Convertir de entero (0-255) a float (0.0-1.0) para ImGui
        float col[4] = {
            *pR / 255.0f,
            *pG / 255.0f,
            *pB / 255.0f,
            *pA / 255.0f
        };

        bool changed = ImGui::ColorPicker4(lbl.c_str(), col, 0);

        if (changed) {
            // Convertir de vuelta a entero y guardar en las variables del script
            *pR = (int)(col[0] * 255.0f + 0.5f);
            *pG = (int)(col[1] * 255.0f + 0.5f);
            *pB = (int)(col[2] * 255.0f + 0.5f);
            *pA = (int)(col[3] * 255.0f + 0.5f);

            // Asegurar que estén en el rango 0-255
            if (*pR < 0) *pR = 0; if (*pR > 255) *pR = 255;
            if (*pG < 0) *pG = 0; if (*pG > 255) *pG = 255;
            if (*pB < 0) *pB = 0; if (*pB > 255) *pB = 255;
            if (*pA < 0) *pA = 0; if (*pA > 255) *pA = 255;
        }

        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F1B: imgui_begin_child
CLEO_Fn(IMGUI_BEGIN_CHILD_A)
{
    READ_STRING(label, 128);
    READ_FLOAT(w);
    READ_FLOAT(h);
    READ_INT(border);
    READ_INT(flags);

    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h, border, flags]() {
        ImGui::BeginChild(lbl.c_str(), ImVec2(w, h), border != 0, flags);
    });
}

// 22xx: imgui_begin_child
CLEO_Fn(IMGUI_BEGIN_CHILD_B)
{
    READ_STRING(label, 128);   // único parámetro

    std::string lbl = label;
    g_drawQueue.push_back([lbl]() {
        // Tamaño (0,0) hace que ocupe el espacio restante del área de contenido
        // borde = false, flags = 0 (comportamiento normal)
        ImGui::BeginChild(lbl.c_str(), ImVec2(0, 0), false, 0);
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
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_INT_PTR(pVar);
    READ_INT(inputFlags);
    READ_INT(count);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, inputFlags, handle]() {
        bool changed = ImGui::InputInt(lbl.c_str(), pVar, 1, 100, inputFlags);

        if (ImGui::IsItemClicked()) {
            VK_OpenForInt(pVar);
        }

        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F1E: imgui_input_float
CLEO_Fn(IMGUI_INPUT_FLOAT)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_FLOAT_PTR(pVar);
    READ_INT(inputFlags);
    READ_INT(count);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, inputFlags, handle]() {
        bool changed = ImGui::InputFloat(lbl.c_str(), pVar, 0.0f, 0.0f, "%.3f", inputFlags);

        if (ImGui::IsItemClicked()) {
            VK_OpenForFloat(pVar);
        }

        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F1F: imgui_input_text
CLEO_Fn(IMGUI_INPUT_TEXT)
{
    READ_STRING(label, 128);
    READ_STRING(hint, 128);
    int bufPtr = cleo->ReadParam(handle)->i;   // número que apunta al buffer
    READ_INT(bufSize);
    READ_INT(flags);

    APPLY_DEFERRED_COND();

    char* buf = (char*)bufPtr;                 // convertir a puntero real
    std::string lbl = label;
    std::string h = hint;

    g_drawQueue.push_back([lbl, h, buf, bufSize, flags, handle]() {
        bool changed = ImGui::InputTextWithHint(lbl.c_str(), h.c_str(), buf, bufSize, flags);

        if (ImGui::IsItemClicked()) {
            VK_OpenForText(buf, bufSize);      // ← usa el buffer y su tamaño
        }

        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F20: imgui_input_text_multiline
CLEO_Fn(IMGUI_INPUT_TEXT_MULTILINE)
{
    READ_STRING(label, 128);
    int bufPtr = cleo->ReadParam(handle)->i;   // número que apunta al buffer
    READ_INT(bufSize);
    READ_FLOAT(width);
    READ_FLOAT(height);
    READ_INT(flags);

    APPLY_DEFERRED_COND();

    char* buf = (char*)bufPtr;
    std::string lbl = label;

    g_drawQueue.push_back([lbl, buf, bufSize, width, height, flags, handle]() {
        bool changed = ImGui::InputTextMultiline(lbl.c_str(), buf, bufSize, ImVec2(width, height), flags);

        if (ImGui::IsItemClicked()) {
            VK_OpenForText(buf, bufSize);      // ← pasa el buffer y su tamaño
        }

        PUSH_DEFERRED_BOOL(changed);
    });
}

// 221F: imgui_input_int (con min/max, sin init)
CLEO_Fn(IMGUI_INPUT_INT_B)
{
    // Orden exacto: variable de salida, etiqueta, min, max
    READ_STRING(label, 128);
    READ_INT(init); // de momento sin efecto
    READ_INT(min);
    READ_INT(max);
    READ_INT_PTR(pOut);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pOut, min, max, handle]() {
        bool changed = ImGui::InputInt(lbl.c_str(), pOut, 1, 100, 0);

        // Aplicar límites después de la edición
        //if (*pOut < min) *pOut = min;
        //if (*pOut > max) *pOut = max;

        if (ImGui::IsItemClicked()) {
            VK_OpenForInt(pOut);
        }

        PUSH_DEFERRED_BOOL(changed);
    });
}

// 2220: imgui_input_float (con min/max, sin init)
CLEO_Fn(IMGUI_INPUT_FLOAT_B)
{
    READ_STRING(label, 128);
    READ_FLOAT(init); // de momento sin efecto
    READ_FLOAT(min);
    READ_FLOAT(max);
    READ_FLOAT_PTR(pOut);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pOut, min, max, handle]() {
        bool changed = ImGui::InputFloat(lbl.c_str(), pOut, 0.1f, 1.0f, "%.3f", 0);

        //if (*pOut < min) *pOut = min;
        //if (*pOut > max) *pOut = max;

        if (ImGui::IsItemClicked()) {
            VK_OpenForFloat(pOut);
        }

        PUSH_DEFERRED_BOOL(changed);
    });
}

// 2221: imgui_input_text (sin flags, devuelve el texto al script)
CLEO_Fn(IMGUI_INPUT_TEXT_B)
{
    READ_STRING(label, 128);
    int bufPtr = cleo->ReadParam(handle)->i;   // número que apunta al buffer

    APPLY_DEFERRED_COND();

    char* buf = (char*)bufPtr;                 // convertir a puntero real
    std::string lbl = label;

    g_drawQueue.push_back([lbl, buf, handle]() {
        bool changed = ImGui::InputTextWithHint(lbl.c_str(), "", buf, g_textInputLimit, 0);

        if (ImGui::IsItemClicked()) {
            VK_OpenForText(buf, g_textInputLimit);      // ← usa el buffer y su tamaño
        }

        PUSH_DEFERRED_BOOL(changed);
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
    cleoaddon->WriteString(handle, "1.3.0");
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
    READ_STRING(label, 128);
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);
    READ_INT(flags);
    READ_FLOAT(w);
    READ_FLOAT(h);

    std::string lbl = label;
    g_drawQueue.push_back([lbl, r, g, b, a, flags, w, h]() {
        ImGui::ColorButton(lbl.c_str(), ImVec4(r, g, b, a), flags, ImVec2(w, h));
    });
}
CLEO_Fn(IMGUI_COLOR_BUTTON_B)
{
    READ_STRING(label, 128);
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);
    READ_FLOAT(w);
    READ_FLOAT(h);

    std::string lbl = label;
    g_drawQueue.push_back([lbl, r, g, b, a, w, h]() {
        ImGui::ColorButton(lbl.c_str(), ImVec4(r, g, b, a), 0, ImVec2(w, h));
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
    READ_STRING(text, MAX_STR_LEN);
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
    READ_STRING(text, MAX_STR_LEN);
    std::string t = text;
    g_drawQueue.push_back([t]() { ImGui::SetTooltip("%s", t.c_str()); });
}

// 0F2E: imgui_color_tooltip
CLEO_Fn(IMGUI_COLOR_TOOLTIP)
{
    READ_STRING(text, MAX_STR_LEN);
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);
    READ_INT(flags);
    
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
    
    RET_COMPARE(ret);
}
CLEO_Fn(IMGUI_IS_ITEM_HOVERED_B)
{
    READ_INT(id);   // ignorado
    bool ret = ImGui::IsItemHovered(0);
    WRITE_INT(ret);
    RET_COMPARE(ret);
}

// 0F30: imgui_is_item_focused
CLEO_Fn(IMGUI_IS_ITEM_FOCUSED)
{
    READ_INT(id);   // ignorado
    
    bool ret = ImGui::IsItemFocused();
    
    RET_COMPARE(ret);
}
CLEO_Fn(IMGUI_IS_ITEM_FOCUSED_B)
{
    READ_INT(id);   // ignorado
    bool ret = ImGui::IsItemFocused();
    WRITE_INT(ret);
    RET_COMPARE(ret);
}

// 0F31: imgui_is_item_activated
CLEO_Fn(IMGUI_IS_ITEM_ACTIVATED)
{
    READ_INT(id);   // ignorado
    
    bool ret = ImGui::IsItemActivated();
    
    RET_COMPARE(ret);
}

// 0F32: imgui_is_item_deactivated
CLEO_Fn(IMGUI_IS_ITEM_DEACTIVATED)
{
    READ_INT(id);   // ignorado
    
    bool ret = ImGui::IsItemDeactivated();
    
    RET_COMPARE(ret);
}

// 0F33: imgui_is_item_active
CLEO_Fn(IMGUI_IS_ITEM_ACTIVE)
{
    READ_INT(id);   // ignorado
    bool ret = ImGui::IsItemActive();
    RET_COMPARE(ret);
}
CLEO_Fn(IMGUI_IS_ITEM_ACTIVE_B)
{
    READ_INT(id);     // ignorado
    bool ret = ImGui::IsItemActive();
    WRITE_INT(ret);
    RET_COMPARE(ret);
}

// 0F34: imgui_is_item_clicked
CLEO_Fn(IMGUI_IS_ITEM_CLICKED)
{
    READ_INT(id);   // ignorado
    READ_INT(btn);
    
    bool ret = ImGui::IsItemClicked(btn);
    
    RET_COMPARE(ret);
}
CLEO_Fn(IMGUI_IS_ITEM_CLICKED_B)
{
    READ_INT(id);   // ignorado
    bool ret = ImGui::IsItemClicked(0);
    WRITE_INT(ret);
    RET_COMPARE(ret);
}

// 0F35: imgui_is_window_hovered
CLEO_Fn(IMGUI_IS_WINDOW_HOVERED)
{
    READ_INT(id);
    READ_INT(flags);
    
    bool ret = ImGui::IsWindowHovered(flags);
    
    RET_COMPARE(ret);
}

// 0F36: imgui_is_window_focused
CLEO_Fn(IMGUI_IS_WINDOW_FOCUSED)
{
    READ_INT(id);
    READ_INT(flags);
    
    bool ret = ImGui::IsWindowFocused(flags);
    
    RET_COMPARE(ret);
}

// 0F37: imgui_radio_button
CLEO_Fn(IMGUI_RADIO_BUTTON)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_INT_PTR(pVar);
    READ_INT(value);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, value, handle]() {
        bool changed = ImGui::RadioButton(lbl.c_str(), pVar, value);
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_RADIO_BUTTON_B)
{
    READ_STRING(label, 128);
    READ_INT(activated);
    READ_INT(value);
    READ_INT_PTR(pVar);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, value, handle]() {
        bool changed = ImGui::RadioButton(lbl.c_str(), pVar, value);
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F38: imgui_collapsing_header
CLEO_Fn(IMGUI_COLLAPSING_HEADER)
{
    READ_STRING(label, 128);
    READ_INT(flags);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, handle]() {
        bool changed = ImGui::CollapsingHeader(lbl.c_str(), flags);
        PUSH_DEFERRED_BOOL(changed);
    });
}

// 0F39: imgui_progress_bar
CLEO_Fn(IMGUI_PROGRESS_BAR)
{
    READ_STRING(label, 128);
    READ_FLOAT(fraction);
    READ_FLOAT(w);
    READ_FLOAT(h);

    std::string lbl = label;
    g_drawQueue.push_back([lbl, fraction, w, h]() {
        ImGui::ProgressBar(fraction, ImVec2(w, h), lbl.c_str());
    });
}

// 0F3A: imgui_get_window_posy  (directo)
CLEO_Fn(IMGUI_GET_WINDOW_POSY)
{
    READ_INT(flags);
    WRITE_FLOAT(ImGui::GetWindowPos().y);
}

// 0F3B: imgui_get_window_posx
CLEO_Fn(IMGUI_GET_WINDOW_POSX)
{
    READ_INT(flags);
    WRITE_FLOAT(ImGui::GetWindowPos().x);
}
// 2xxx: imgui_get_window_pos
CLEO_Fn(IMGUI_GET_WINDOW_POS)
{
    READ_INT(flags);
    WRITE_FLOAT(ImGui::GetWindowPos().x);
    WRITE_FLOAT(ImGui::GetWindowPos().y);
}

// 0F3C: imgui_get_window_width
CLEO_Fn(IMGUI_GET_WINDOW_WIDTH)
{
    READ_INT(flags);
    WRITE_FLOAT(ImGui::GetWindowWidth());
}

// 0F3D: imgui_get_window_height
CLEO_Fn(IMGUI_GET_WINDOW_HEIGHT)
{
    READ_INT(flags);
    WRITE_FLOAT(ImGui::GetWindowHeight());
}

// 0F3E: imgui_selectable
CLEO_Fn(IMGUI_SELECTABLE)
{
    READ_STRING(label, 128);
    READ_INT_PTR(pSelected);
    READ_INT(flags);
    READ_FLOAT(w);
    READ_FLOAT(h);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pSelected, flags, w, h, handle]() {
        bool sel = (*pSelected != 0);
        bool activated = ImGui::Selectable(lbl.c_str(), &sel, flags, ImVec2(w, h));
        *pSelected = sel ? 1 : 0;

        PUSH_DEFERRED_BOOL(activated);  // encolar el resultado
    });
}

// 2225: imgui_selectable
CLEO_Fn(IMGUI_SELECTABLE_B)
{
    READ_STRING(label, 128);
    READ_INT_PTR(pSelected);    // variable que guarda 0 o 1 (estado de selección)
    READ_INT_PTR(pClicked);  

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pSelected, pClicked, handle]() {
        bool sel = (*pSelected != 0);
        bool activated = ImGui::Selectable(lbl.c_str(), &sel, 0, ImVec2(0, 0));
        *pSelected = sel ? 1 : 0;

        // Devolver "clicked" en la variable de salida del script
        *pClicked = activated ? 1 : 0;

        PUSH_DEFERRED_BOOL(activated);
    });
}

// 0F40 / 2238: imgui_load_image (con búsqueda automática y std::ifstream)
CLEO_Fn(IMGUI_LOAD_IMAGE)
{
    READ_STRING(path, 256);

    int imageId = -1;
    if (path[0] != '\0' && sautils != nullptr) {
        std::string fullPath;
        std::string input = path;
        bool found = false;

        // Resolver prefijos virtuales
        if (input.rfind("cleo:", 0) == 0) {
            const char* dir = cleo->GetCleoStorageDir();
            fullPath = std::string(dir ? dir : "") + "/" + input.substr(5);
            found = true;
        }
        else if (input.rfind("root:", 0) == 0) {
            const char* dir = aml->GetAndroidDataPath();
            fullPath = std::string(dir ? dir : "") + "/" + input.substr(5);
            found = true;
        }
        else if (input.rfind("userfiles:", 0) == 0) {
            const char* dir = aml->GetDataPath();
            fullPath = std::string(dir ? dir : "") + "/" + input.substr(10);
            found = true;
        }
        else if (input.rfind("modules:", 0) == 0) {
            const char* dir = cleo->GetCleoPluginLoadDir();
            fullPath = std::string(dir ? dir : "") + "/" + input.substr(8);
            found = true;
        }
        else if (input[0] == '/') {
            fullPath = input;
            found = true;
        }

        // Si no se especificó prefijo, buscar en las carpetas
        if (!found) {
            std::vector<std::string> bases;
            if (cleo->GetCleoStorageDir()) bases.push_back(cleo->GetCleoStorageDir());
            if (aml->GetAndroidDataPath()) bases.push_back(aml->GetAndroidDataPath());
            if (aml->GetDataPath()) bases.push_back(aml->GetDataPath());
            if (cleo->GetCleoPluginLoadDir()) bases.push_back(cleo->GetCleoPluginLoadDir());

            for (const auto& base : bases) {
                std::string candidate = base + "/" + input;
                std::ifstream testFile(candidate, std::ios::binary);
                if (testFile.is_open()) {
                    testFile.close();
                    fullPath = candidate;
                    found = true;
                    break;
                } else {
                    logger->Info("IMGUI_LOAD_IMAGE: not found at %s", candidate.c_str());
                }
            }
        }

        if (!found || fullPath.empty()) {
            logger->Error("IMGUI_LOAD_IMAGE: path not found: %s", path);
            WRITE_INT(imageId);
            return;
        }

        logger->Info("IMGUI_LOAD_IMAGE: resolved path = %s", fullPath.c_str());

        // Verificar caché
        auto it = g_imagePathToId.find(fullPath);
        if (it != g_imagePathToId.end()) {
            logger->Info("IMGUI_LOAD_IMAGE: already loaded id = %d", it->second);
            WRITE_INT(it->second);
            return;
        }

        void* texture = nullptr;

        // Intentar cargar con sautils según extensión
        if (strstr(fullPath.c_str(), ".png") || strstr(fullPath.c_str(), ".PNG")) {
            texture = sautils->LoadRwTextureFromPNG(fullPath.c_str());
        } else if (strstr(fullPath.c_str(), ".bmp") || strstr(fullPath.c_str(), ".BMP")) {
            texture = sautils->LoadRwTextureFromBMP(fullPath.c_str());
        } else {
            texture = sautils->LoadRwTextureFromBMP(fullPath.c_str());
            if (!texture) texture = sautils->LoadRwTextureFromPNG(fullPath.c_str());
        }
        
        // Intento 2: RenderWare directo (fallback)
if (!texture) {
    RwImage* image = RwImageRead(fullPath.c_str());
    if (image) {
        RwRaster* raster = RwRasterCreate(image->width, image->height, image->depth, 2);
        if (raster) {
            raster = RwRasterSetFromImage(raster, image);
            if (raster) {
                texture = (void*)raster;
                logger->Info("IMGUI_LOAD_IMAGE: loaded via RwImageRead/RwRasterSetFromImage");
            }
        }
        RwImageDestroy(image);
    }
}

        if (texture) {
            imageId = AddImage(texture);
            g_imagePathToId[fullPath] = imageId;
            logger->Info("IMGUI_LOAD_IMAGE: new image id = %d", imageId);
        } else {
            logger->Error("IMGUI_LOAD_IMAGE: sautils failed to load texture from %s", fullPath.c_str());
        }
    }
    WRITE_INT(imageId);   // devuelve -1 si no se cargó
}

// 0F41: imgui_image
CLEO_Fn(IMGUI_IMAGE)
{
    READ_STRING(nope, 16);
    READ_INT(imageId);
    READ_FLOAT(width);
    READ_FLOAT(height);

    g_drawQueue.push_back([imageId, width, height]() {
        void* tex = GetImage(imageId);
        if (tex) {
            ImGui::Image(
                (ImTextureID)tex,
                ImVec2(width, height),
                g_imageUV0,
                g_imageUV1,
                g_imageTintColor,
                g_imageBorderColor
            );
        }
    });
}

// 0f42: imgui_image_ex
CLEO_Fn(IMGUI_IMAGE_EX)
{
    READ_INT(imageId);
    READ_FLOAT(width);  READ_FLOAT(height);
    READ_FLOAT(uv0x);   READ_FLOAT(uv0y);
    READ_FLOAT(uv1x);   READ_FLOAT(uv1y);
    READ_INT(r); READ_INT(g); READ_INT(b); READ_INT(a);

    g_drawQueue.push_back([imageId, width, height, uv0x, uv0y, uv1x, uv1y, r, g, b, a]() {
        void* raster = GetImage(imageId);
        if (raster) {
            ImGui::Image(
                (ImTextureID)raster,
                ImVec2(width, height),
                ImVec2(uv0x, uv0y),
                ImVec2(uv1x, uv1y),
                ImVec4(r/255.0f, g/255.0f, b/255.0f, a/255.0f),
                ImVec4(0,0,0,0)  // borde transparente
            );
        }
    });
}

// 0f43: imgui_image_button
CLEO_Fn(IMGUI_IMAGE_BUTTON)
{
    READ_STRING(label, 128);
    READ_INT(imageId);
    READ_FLOAT(width);
    READ_FLOAT(height);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, imageId, width, height, handle]() {
        bool clicked = false;
        void* raster = GetImage(imageId);
        if (raster) {
            clicked = ImGui::ImageButton(
                lbl.c_str(),
                (ImTextureID)raster,
                ImVec2(width, height),
                g_imageUV0,
                g_imageUV1,
                g_imageBgColor,
                g_imageTintColor
            );
        }
        PUSH_DEFERRED_BOOL(clicked);
    });
}

// 2236: imgui_set_image_bg_color
CLEO_Fn(IMGUI_SET_IMAGE_BG_COLOR)
{
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);

    g_imageBgColor = ImVec4(r, g, b, a);
}
// 2237: imgui_set_image_tint_color
CLEO_Fn(IMGUI_SET_IMAGE_TINT_COLOR)
{
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);

    g_imageTintColor = ImVec4(r, g, b, a);
}

// 223A: imgui_set_image_uv
CLEO_Fn(IMGUI_SET_IMAGE_UV)
{
    READ_FLOAT(uv0x);
    READ_FLOAT(uv0y);
    READ_FLOAT(uv1x);
    READ_FLOAT(uv1y);

    g_imageUV0 = ImVec2(uv0x, uv0y);
    g_imageUV1 = ImVec2(uv1x, uv1y);
}

// 223B: imgui_set_image_border_color
CLEO_Fn(IMGUI_SET_IMAGE_BORDER_COLOR)
{
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);

    g_imageBorderColor = ImVec4(r, g, b, a);
}

// 0f44: imgui_image_button_ex
CLEO_Fn(IMGUI_IMAGE_BUTTON_EX)
{
    READ_STRING(label, 128);
    READ_INT(imageId);
    READ_FLOAT(width);  READ_FLOAT(height);
    READ_FLOAT(uv0x);   READ_FLOAT(uv0y);
    READ_FLOAT(uv1x);   READ_FLOAT(uv1y);
    READ_INT(r); READ_INT(g); READ_INT(b); READ_INT(a);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, imageId, width, height, uv0x, uv0y, uv1x, uv1y, r, g, b, a, handle]() {
        bool clicked = false;
        void* raster = GetImage(imageId);
        if (raster) {
            clicked = ImGui::ImageButton(
                lbl.c_str(),
                (ImTextureID)raster,
                ImVec2(width, height),
                ImVec2(uv0x, uv0y),
                ImVec2(uv1x, uv1y),
                ImVec4(0,0,0,0),
                ImVec4(r/255.0f, g/255.0f, b/255.0f, a/255.0f)
            );
        }
        PUSH_DEFERRED_BOOL(clicked);
    });
}


// 0F46: imgui_invisible_button
CLEO_Fn(IMGUI_INVISIBLE_BUTTON)
{
    READ_STRING(label, 128);
    READ_FLOAT(w);
    READ_FLOAT(h);
    READ_INT(flags);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h, flags, handle]() {
        bool clicked = ImGui::InvisibleButton(lbl.c_str(), ImVec2(w, h), flags);
        PUSH_DEFERRED_BOOL(clicked);
    });
}
CLEO_Fn(IMGUI_INVISIBLE_BUTTON_B)
{
    READ_STRING(label, 128);
    READ_FLOAT(w);
    READ_FLOAT(h);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h, handle]() {
        bool clicked = ImGui::InvisibleButton(lbl.c_str(), ImVec2(w, h), 0);
        PUSH_DEFERRED_BOOL(clicked);
    });
}

// 0F47: imgui_drawlist_add_circle
CLEO_Fn(IMGUI_DRAWLIST_ADD_CIRCLE)
{
    READ_FLOAT(cx);
    READ_FLOAT(cy);
    READ_FLOAT(r);
    READ_INT(col);
    READ_INT(seg);
    READ_FLOAT(thick);

    g_drawQueue.push_back([cx, cy, r, col, seg, thick]() {
        ImGui::GetWindowDrawList()->AddCircle(ImVec2(cx, cy), r, col, seg, thick);
    });
}

// 0F48: imgui_drawlist_add_circle_filled
CLEO_Fn(IMGUI_DRAWLIST_ADD_CIRCLE_FILLED)
{
    READ_FLOAT(cx);
    READ_FLOAT(cy);
    READ_FLOAT(r);
    READ_INT(col);
    READ_INT(seg);

    g_drawQueue.push_back([cx, cy, r, col, seg]() {
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cx, cy), r, col, seg);
    });
}

// 0F49: imgui_drawlist_add_rect
CLEO_Fn(IMGUI_DRAWLIST_ADD_RECT)
{
    READ_FLOAT(x1);
    READ_FLOAT(y1);
    READ_FLOAT(x2);
    READ_FLOAT(y2);
    READ_INT(col);
    READ_INT(rounding);
    READ_INT(corners);
    READ_FLOAT(thick);

    g_drawQueue.push_back([x1,y1,x2,y2,col,rounding,corners,thick]() {
        ImGui::GetWindowDrawList()->AddRect(ImVec2(x1,y1), ImVec2(x2,y2), col, (float)rounding, corners, thick);
    });
}

// 0F4A: imgui_drawlist_add_rect_filled
CLEO_Fn(IMGUI_DRAWLIST_ADD_RECT_FILLED)
{
    READ_FLOAT(x1);
    READ_FLOAT(y1);
    READ_FLOAT(x2);
    READ_FLOAT(y2);
    READ_INT(col);
    READ_INT(rounding);
    READ_INT(corners);

    g_drawQueue.push_back([x1,y1,x2,y2,col,rounding,corners]() {
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x1,y1), ImVec2(x2,y2), col, (float)rounding, corners);
    });
}

// 0F4B: imgui_drawlist_add_rect_filled_multicolor
CLEO_Fn(IMGUI_DRAWLIST_ADD_RECT_FILLED_MULTICOLOR)
{
    READ_FLOAT(x1);
    READ_FLOAT(y1);
    READ_FLOAT(x2);
    READ_FLOAT(y2);
    READ_INT(ul);
    READ_INT(ur);
    READ_INT(dl);
    READ_INT(dr);

    g_drawQueue.push_back([x1,y1,x2,y2,ul,ur,dl,dr]() {
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(ImVec2(x1,y1), ImVec2(x2,y2), ul, ur, dl, dr);
    });
}

// 0F4C: imgui_drawlist_add_text
CLEO_Fn(IMGUI_DRAWLIST_ADD_TEXT)
{
    READ_STRING(text, MAX_STR_LEN);
    READ_FLOAT(x);
    READ_FLOAT(y);
    READ_FLOAT(size);
    READ_INT(col);

    std::string t = text;
    g_drawQueue.push_back([t, x, y, size, col]() {
        ImGui::GetWindowDrawList()->AddText(ImVec2(x, y), col, t.c_str());
    });
}
// 2242: imgui_drawlist_add_text
CLEO_Fn(IMGUI_DRAWLIST_ADD_TEXT_B)
{
    READ_INT(drawList);
    READ_FLOAT(x);
    READ_FLOAT(y);
    READ_INT(r);
    READ_INT(g);
    READ_INT(b);
    READ_INT(a);
    READ_STRING(text, MAX_STR_LEN);

    std::string t = text;
    g_drawQueue.push_back([drawList, x, y, r, g, b, a, t]() {
        ImDrawList* dl = nullptr;
        switch (drawList) {
            case 0: dl = ImGui::GetWindowDrawList(); break;
            case 1: dl = ImGui::GetForegroundDrawList(); break;
            case 2: dl = ImGui::GetBackgroundDrawList(); break;
            default: dl = ImGui::GetWindowDrawList(); break;
        }
        if (dl) {
            ImU32 color = IM_COL32(r, g, b, a);
            dl->AddText(ImVec2(x, y), color, t.c_str());
        }
    });
}

// 0F4D: imgui_drawlist_add_triangle
CLEO_Fn(IMGUI_DRAWLIST_ADD_TRIANGLE)
{
    READ_FLOAT(x1);
    READ_FLOAT(y1);
    READ_FLOAT(x2);
    READ_FLOAT(y2);
    READ_FLOAT(x3);
    READ_FLOAT(y3);
    READ_INT(col);
    
    float thick = (float)cleo->ReadParam(handle)->i;
    g_drawQueue.push_back([=]() {
        ImGui::GetWindowDrawList()->AddTriangle(ImVec2(x1,y1), ImVec2(x2,y2), ImVec2(x3,y3), col, thick);
    });
}

// 0F4E: imgui_drawlist_add_triangle_filled
CLEO_Fn(IMGUI_DRAWLIST_ADD_TRIANGLE_FILLED)
{
    READ_FLOAT(x1);
    READ_FLOAT(y1);
    READ_FLOAT(x2);
    READ_FLOAT(y2);
    READ_FLOAT(x3);
    READ_FLOAT(y3);
    READ_INT(col);

    g_drawQueue.push_back([=]() {
        ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(x1,y1), ImVec2(x2,y2), ImVec2(x3,y3), col);
    });
}

// 0F4F: imgui_begin_main_menu_bar
CLEO_Fn(IMGUI_BEGIN_MAIN_MENU_BAR)
{
    g_drawQueue.push_back([]() { ImGui::BeginMainMenuBar(); });
}

CLEO_Fn(IMGUI_BEGIN_MAIN_MENU_BAR_B)
{
    READ_STRING(identify, 128);
    READ_INT(visible);   // 1 = visible, 0 = oculto

    std::string id = identify;   // podrías usarlo para depuración o futuras referencias
    g_drawQueue.push_back([id, visible]() {
        if (visible) {
            ImGui::BeginMainMenuBar();
        }
    });
}

// 0F50: imgui_end_main_menu_bar
CLEO_Fn(IMGUI_END_MAIN_MENU_BAR)
{
    g_drawQueue.push_back([]() { ImGui::EndMainMenuBar(); });
}

// 0F51: imgui_menu_item
CLEO_Fn(IMGUI_MENU_ITEM)
{
    READ_STRING(label, 128);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, handle]() {
        bool activated = ImGui::MenuItem(lbl.c_str());
        PUSH_DEFERRED_BOOL(activated);
    });
}

// 0F52-0F55: estilos (directo, sin cola)
CLEO_Fn(IMGUI_STYLE_COLORS_CLASSIC) { ImGui::StyleColorsClassic(); }
CLEO_Fn(IMGUI_STYLE_COLORS_DARK)    { ImGui::StyleColorsDark(); }
CLEO_Fn(IMGUI_STYLE_COLORS_DEFAULT) { ImGui::StyleColorsLight(); /* o Default */ }
CLEO_Fn(IMGUI_STYLE_COLORS_LIGHT)   { ImGui::StyleColorsLight(); }

// 0F57: imgui_get_style  (directo)
CLEO_Fn(IMGUI_GET_STYLE)
{
    READ_INT(offset);          // valor de ImGuiStyleVar
    READ_FLOAT_PTR(pOut);      // variable de salida

    ImGuiStyle& style = ImGui::GetStyle();
    float value = 0.0f;

    switch (offset) {
        case ImGuiStyleVar_Alpha:               value = style.Alpha; break;
        case ImGuiStyleVar_WindowPadding:       value = style.WindowPadding.x; break; // devolvemos x
        case ImGuiStyleVar_WindowRounding:      value = style.WindowRounding; break;
        case ImGuiStyleVar_WindowBorderSize:    value = style.WindowBorderSize; break;
        case ImGuiStyleVar_WindowMinSize:       value = style.WindowMinSize.x; break;
        case ImGuiStyleVar_WindowTitleAlign:    value = style.WindowTitleAlign.x; break;
        case ImGuiStyleVar_ChildRounding:       value = style.ChildRounding; break;
        case ImGuiStyleVar_ChildBorderSize:     value = style.ChildBorderSize; break;
        case ImGuiStyleVar_PopupRounding:       value = style.PopupRounding; break;
        case ImGuiStyleVar_PopupBorderSize:     value = style.PopupBorderSize; break;
        case ImGuiStyleVar_FramePadding:        value = style.FramePadding.x; break;
        case ImGuiStyleVar_FrameRounding:       value = style.FrameRounding; break;
        case ImGuiStyleVar_FrameBorderSize:     value = style.FrameBorderSize; break;
        case ImGuiStyleVar_ItemSpacing:         value = style.ItemSpacing.x; break;
        case ImGuiStyleVar_ItemInnerSpacing:    value = style.ItemInnerSpacing.x; break;
        case ImGuiStyleVar_IndentSpacing:       value = style.IndentSpacing; break;
        case ImGuiStyleVar_ScrollbarSize:       value = style.ScrollbarSize; break;
        case ImGuiStyleVar_ScrollbarRounding:   value = style.ScrollbarRounding; break;
        case ImGuiStyleVar_GrabMinSize:         value = style.GrabMinSize; break;
        case ImGuiStyleVar_GrabRounding:        value = style.GrabRounding; break;
//        case ImGuiStyleVar_TabRounding:         value = style.TabRounding; break;
//        case ImGuiStyleVar_TabBorderSize:       value = style.TabBorderSize; break;
        case ImGuiStyleVar_ButtonTextAlign:     value = style.ButtonTextAlign.x; break;
        case ImGuiStyleVar_SelectableTextAlign: value = style.SelectableTextAlign.x; break;
        default: break; // offset no soportado
    }

    *pOut = value;
}

// 0F58: imgui_set_style  (directo)
CLEO_Fn(IMGUI_SET_STYLE)
{
    READ_INT(offset);
    READ_FLOAT(value);

    ImGuiStyle& style = ImGui::GetStyle();

    switch (offset) {
        case ImGuiStyleVar_Alpha:               style.Alpha = value; break;
        case ImGuiStyleVar_WindowPadding:       style.WindowPadding = ImVec2(value, value); break;
        case ImGuiStyleVar_WindowRounding:      style.WindowRounding = value; break;
        case ImGuiStyleVar_WindowBorderSize:    style.WindowBorderSize = value; break;
        case ImGuiStyleVar_WindowMinSize:       style.WindowMinSize = ImVec2(value, value); break;
        case ImGuiStyleVar_WindowTitleAlign:    style.WindowTitleAlign = ImVec2(value, 0.5f); break;
        case ImGuiStyleVar_ChildRounding:       style.ChildRounding = value; break;
        case ImGuiStyleVar_ChildBorderSize:     style.ChildBorderSize = value; break;
        case ImGuiStyleVar_PopupRounding:       style.PopupRounding = value; break;
        case ImGuiStyleVar_PopupBorderSize:     style.PopupBorderSize = value; break;
        case ImGuiStyleVar_FramePadding:        style.FramePadding = ImVec2(value, value); break;
        case ImGuiStyleVar_FrameRounding:       style.FrameRounding = value; break;
        case ImGuiStyleVar_FrameBorderSize:     style.FrameBorderSize = value; break;
        case ImGuiStyleVar_ItemSpacing:         style.ItemSpacing = ImVec2(value, value); break;
        case ImGuiStyleVar_ItemInnerSpacing:    style.ItemInnerSpacing = ImVec2(value, value); break;
        case ImGuiStyleVar_IndentSpacing:       style.IndentSpacing = value; break;
        case ImGuiStyleVar_ScrollbarSize:       style.ScrollbarSize = value; break;
        case ImGuiStyleVar_ScrollbarRounding:   style.ScrollbarRounding = value; break;
        case ImGuiStyleVar_GrabMinSize:         style.GrabMinSize = value; break;
        case ImGuiStyleVar_GrabRounding:        style.GrabRounding = value; break;
//        case ImGuiStyleVar_TabRounding:         style.TabRounding = value; break;
//        case ImGuiStyleVar_TabBorderSize:       style.TabBorderSize = value; break;
        default: break;
    }
}

// 0F59: imgui_set_style_int
CLEO_Fn(IMGUI_SET_STYLE_INT)
{
    READ_INT(offset);
    READ_INT(valueInt);

    float value = (float)valueInt;
    ImGuiStyle& style = ImGui::GetStyle();

    // Reutilizar el mismo switch que SET_STYLE, sustituyendo value por valueInt convertido
    switch (offset) {
        case ImGuiStyleVar_Alpha:               style.Alpha = value; break;
        case ImGuiStyleVar_WindowPadding:       style.WindowPadding = ImVec2(value, value); break;
        case ImGuiStyleVar_WindowRounding:      style.WindowRounding = value; break;
        case ImGuiStyleVar_WindowBorderSize:    style.WindowBorderSize = value; break;
        case ImGuiStyleVar_WindowMinSize:       style.WindowMinSize = ImVec2(value, value); break;
        case ImGuiStyleVar_WindowTitleAlign:    style.WindowTitleAlign = ImVec2(value, 0.5f); break;
        case ImGuiStyleVar_ChildRounding:       style.ChildRounding = value; break;
        case ImGuiStyleVar_ChildBorderSize:     style.ChildBorderSize = value; break;
        case ImGuiStyleVar_PopupRounding:       style.PopupRounding = value; break;
        case ImGuiStyleVar_PopupBorderSize:     style.PopupBorderSize = value; break;
        case ImGuiStyleVar_FramePadding:        style.FramePadding = ImVec2(value, value); break;
        case ImGuiStyleVar_FrameRounding:       style.FrameRounding = value; break;
        case ImGuiStyleVar_FrameBorderSize:     style.FrameBorderSize = value; break;
        case ImGuiStyleVar_ItemSpacing:         style.ItemSpacing = ImVec2(value, value); break;
        case ImGuiStyleVar_ItemInnerSpacing:    style.ItemInnerSpacing = ImVec2(value, value); break;
        case ImGuiStyleVar_IndentSpacing:       style.IndentSpacing = value; break;
        case ImGuiStyleVar_ScrollbarSize:       style.ScrollbarSize = value; break;
        case ImGuiStyleVar_ScrollbarRounding:   style.ScrollbarRounding = value; break;
        case ImGuiStyleVar_GrabMinSize:         style.GrabMinSize = value; break;
        case ImGuiStyleVar_GrabRounding:        style.GrabRounding = value; break;
//        case ImGuiStyleVar_TabRounding:         style.TabRounding = value; break;
//        case ImGuiStyleVar_TabBorderSize:       style.TabBorderSize = value; break;
        default: break;
    }
}

// 0F5A: imgui_get_color
CLEO_Fn(IMGUI_GET_COLOR)
{
    READ_INT(offset);            // valor de ImGuiCol
    READ_FLOAT_PTR(pR);
    READ_FLOAT_PTR(pG);
    READ_FLOAT_PTR(pB);
    READ_FLOAT_PTR(pA);

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 col = style.Colors[offset];

    *pR = col.x;
    *pG = col.y;
    *pB = col.z;
    *pA = col.w;
}

// 0F5B: imgui_set_color
CLEO_Fn(IMGUI_SET_COLOR)
{
    READ_INT(offset);
    READ_FLOAT(r);
    READ_FLOAT(g);
    READ_FLOAT(b);
    READ_FLOAT(a);

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[offset] = ImVec4(r, g, b, a);
}

// 0F5C: imgui_push_item_width
CLEO_Fn(IMGUI_PUSH_ITEM_WIDTH)
{
    READ_FLOAT(w);
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
    READ_INT(flag);
    READ_INT(en);
    g_drawQueue.push_back([flag, en]() { ImGui::PushItemFlag(flag, en != 0); });
}

// 0F5F: imgui_pop_item_flag
CLEO_Fn(IMGUI_POP_ITEM_FLAG)
{
    g_drawQueue.push_back([]() { ImGui::PopItemFlag(); });
}

// 0F60: imgui_get_window_content_region_width  (directo)
// 224C:
CLEO_Fn(IMGUI_GET_WINDOW_CONTENT_REGION_WIDTH)
{
    READ_STRING(uniqueId, 128);   // se ignora, solo se lee para la interfaz

    float width = 0.0f;

    // Solo si hay una ventana activa (Begin sin End)
    if (ImGui::GetCurrentWindow() != nullptr) {
        float cursorStartX = ImGui::GetCursorStartPos().x;
        float paddingX = ImGui::GetStyle().WindowPadding.x;
        width = ImGui::GetWindowWidth() - cursorStartX - paddingX;
        if (width < 0.0f) width = 0.0f;
    }

    WRITE_FLOAT(width);
}

// 0F61: imgui_get_frame_height
CLEO_Fn(IMGUI_GET_FRAME_HEIGHT)
{
    READ_INT(flags);
    WRITE_FLOAT(ImGui::GetFrameHeight());
}

// 0F62: imgui_get_frame_height_with_spacing
CLEO_Fn(IMGUI_GET_FRAME_HEIGHT_WITH_SPACING)
{
    READ_INT(flags);
    WRITE_FLOAT(ImGui::GetFrameHeightWithSpacing());
}

// 0F63: imgui_get_style_int
CLEO_Fn(IMGUI_GET_STYLE_INT)
{
    READ_INT(offset);          // ImGuiStyleVar
    READ_INT_PTR(pOut);        // variable de salida (int)

    ImGuiStyle& style = ImGui::GetStyle();
    float value = 0.0f;

    switch (offset) {
        case ImGuiStyleVar_Alpha:               value = style.Alpha; break;
        case ImGuiStyleVar_WindowPadding:       value = style.WindowPadding.x; break;
        case ImGuiStyleVar_WindowRounding:      value = style.WindowRounding; break;
        case ImGuiStyleVar_WindowBorderSize:    value = style.WindowBorderSize; break;
        case ImGuiStyleVar_WindowMinSize:       value = style.WindowMinSize.x; break;
        case ImGuiStyleVar_WindowTitleAlign:    value = style.WindowTitleAlign.x; break;
        case ImGuiStyleVar_ChildRounding:       value = style.ChildRounding; break;
        case ImGuiStyleVar_ChildBorderSize:     value = style.ChildBorderSize; break;
        case ImGuiStyleVar_PopupRounding:       value = style.PopupRounding; break;
        case ImGuiStyleVar_PopupBorderSize:     value = style.PopupBorderSize; break;
        case ImGuiStyleVar_FramePadding:        value = style.FramePadding.x; break;
        case ImGuiStyleVar_FrameRounding:       value = style.FrameRounding; break;
        case ImGuiStyleVar_FrameBorderSize:     value = style.FrameBorderSize; break;
        case ImGuiStyleVar_ItemSpacing:         value = style.ItemSpacing.x; break;
        case ImGuiStyleVar_ItemInnerSpacing:    value = style.ItemInnerSpacing.x; break;
        case ImGuiStyleVar_IndentSpacing:       value = style.IndentSpacing; break;
        case ImGuiStyleVar_ScrollbarSize:       value = style.ScrollbarSize; break;
        case ImGuiStyleVar_ScrollbarRounding:   value = style.ScrollbarRounding; break;
        case ImGuiStyleVar_GrabMinSize:         value = style.GrabMinSize; break;
        case ImGuiStyleVar_GrabRounding:        value = style.GrabRounding; break;
//        case ImGuiStyleVar_TabRounding:         value = style.TabRounding; break;
//        case ImGuiStyleVar_TabBorderSize:       value = style.TabBorderSize; break;
        case ImGuiStyleVar_ButtonTextAlign:     value = style.ButtonTextAlign.x; break;
        case ImGuiStyleVar_SelectableTextAlign: value = style.SelectableTextAlign.x; break;
        default: break;
    }

    *pOut = (int)value;
}

// 2239: imgui_free_image
CLEO_Fn(IMGUI_FREE_IMAGE)
{
    READ_INT(imageId);

    if (imageId > 0) {
        // 1. Quitar la imagen del mapa principal
        auto it = g_images.find(imageId);
        if (it != g_images.end()) {
            g_images.erase(it);
        }

        // 2. Eliminar TODAS las entradas de caché que apunten a este id
        for (auto itPath = g_imagePathToId.begin(); itPath != g_imagePathToId.end(); ) {
            if (itPath->second == imageId) {
                itPath = g_imagePathToId.erase(itPath);
            } else {
                ++itPath;
            }
        }
    }
}
// 0F45: imgui_get_game_path
CLEO_Fn(IMGUI_GET_GAME_PATH)
{
    const char* cleoDir = cleo->GetCleoStorageDir();
    std::string gamePath = cleoDir ? cleoDir : "";

    cleoaddon->WriteString(handle, gamePath.c_str());
}
// 0F69: imgui_input_text_limit
CLEO_Fn(IMGUI_SET_TEXT_LIMIT)
{
    READ_INT(limit);
    if (limit < 1) limit = 1;
    if (limit > 4096) limit = 4096;   // seguridad
    g_textInputLimit = limit;
}

// 0F67: imgui_keyboard_show (modo mejorado, sin size en texto)
CLEO_Fn(IMGUI_KEYBOARD_SHOW)
{
    READ_INT(mode); // 0 = texto, 1 = int, 2 = float

    if (mode == 0) {
        // Modo texto: siguiente parámetro = puntero a buffer
        int bufPtr = cleo->ReadParam(handle)->i;
        char* buf = (char*)bufPtr;
        VK_OpenForText(buf, g_textInputLimit);
    }
    else if (mode == 1) {
        READ_INT_PTR(pInt);
        VK_OpenForInt(pInt);
    }
    else if (mode == 2) {
        READ_FLOAT_PTR(pFloat);
        VK_OpenForFloat(pFloat);
    }
    else {
        READ_INT_PTR(pInt);
        VK_Close();
    }
}
// 0F68: imgui_keyboard_hide
CLEO_Fn(IMGUI_KEYBOARD_HIDE)
{
    VK_Close();
}
// 0F69: imgui_keyboard_is_visible
CLEO_Fn(IMGUI_KEYBOARD_IS_VISIBLE)
{
    RET_COMPARE(g_vk.open);
}
// 0F6A: imgui_keyboard_set_enter_mode
CLEO_Fn(IMGUI_KEYBOARD_SET_ENTER_MODE)
{
    READ_INT(mode);
    g_vkEnterMode = (mode == 0) ? 0 : 1;   // solo 0 o 1
}
CLEO_Fn(IMGUI_IMAGE_RESET_COLOR)
{
    g_imageBgColor   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // transparente por defecto
    g_imageTintColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // blanco por defecto
    g_imageBorderColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // transparente por defecto
}

CLEO_Fn(IMGUI_TABS)
{
    READ_INT_PTR(pOut);
    READ_STRING(label, 128);
    READ_STRING(tabNames, 512);

    std::string id = label;
    std::string tabs = tabNames;

    g_drawQueue.push_back([id, tabs, pOut]() {
        int activeIndex = -1;

        if (ImGui::BeginTabBar(id.c_str())) {
            int idx = 0;
            char* copy = new char[tabs.size() + 1];
            strcpy(copy, tabs.c_str());
            char* token = strtok(copy, ",");
            while (token) {
                if (ImGui::BeginTabItem(token)) {
                    activeIndex = idx;
                    ImGui::EndTabItem();
                }
                token = strtok(nullptr, ",");
                idx++;
            }
            delete[] copy;
            ImGui::EndTabBar();
        }

        if (pOut) *pOut = activeIndex;
    });
}
CLEO_Fn(IMGUI_TEXT_CENTERED)
{
    READ_STRING(text, MAX_STR_LEN);
    std::string t = text;

    g_drawQueue.push_back([t]() {
        float windowWidth = ImGui::GetWindowWidth();
        float textWidth = ImGui::CalcTextSize(t.c_str()).x;
        if (windowWidth > textWidth)
            ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
        ImGui::TextUnformatted(t.c_str());
    });
}
CLEO_Fn(IMGUI_COMBO)
{
    READ_INT_PTR(pOut);
    READ_STRING(label, 128);
    READ_STRING(options, 512);
    READ_INT(initialSelection);

    std::string id = label;
    std::string opts = options;

    g_drawQueue.push_back([id, opts, initialSelection, pOut]() {
        static std::map<std::string, int> selections;
        static std::map<std::string, bool> initialized;

        if (!initialized[id]) {
            selections[id] = initialSelection;
            initialized[id] = true;
        }

        // Separar opciones por comas
        std::vector<std::string> items;
        std::string current = "";
        for (size_t i = 0; i <= opts.size(); ++i) {
            if (i == opts.size() || opts[i] == ',') {
                items.push_back(current);
                current.clear();
            } else {
                current += opts[i];
            }
        }

        int& selected = selections[id];
        const char* preview = (selected >= 0 && selected < (int)items.size()) ? items[selected].c_str() : "";

        if (ImGui::BeginCombo(id.c_str(), preview)) {
            for (size_t i = 0; i < items.size(); ++i) {
                bool isSelected = ((int)i == selected);
                if (ImGui::Selectable(items[i].c_str(), isSelected)) {
                    selected = (int)i;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (pOut) *pOut = selected;
    });
}
CLEO_Fn(IMGUI_ARROW_BUTTON)
{
    READ_STRING(label, 128);
    READ_INT(dir);

    APPLY_DEFERRED_COND();

    std::string id = label;
    g_drawQueue.push_back([id, dir, handle]() {
        bool clicked = ImGui::ArrowButton(id.c_str(), dir);
        PUSH_DEFERRED_BOOL(clicked);
    });
}
CLEO_Fn(IMGUI_SET_ITEM_INT)
{
    READ_STRING(id, 128);
    READ_INT(val);
}
CLEO_Fn(IMGUI_SET_ITEM_FLOAT)
{
    READ_STRING(id, 128);
    READ_FLOAT(val);
}
CLEO_Fn(IMGUI_SET_ITEM_TEXT)
{
    READ_STRING(id, 128);
    READ_STRING(val, 256);
}
CLEO_Fn(IMGUI_PUSH_STYLE_VAR)
{
    READ_INT(styleVar);
    READ_FLOAT(val);

    g_drawQueue.push_back([styleVar, val]() {
        ImGui::PushStyleVar((ImGuiStyleVar)styleVar, val);
    });
}
CLEO_Fn(IMGUI_PUSH_STYLE_VAR2)
{
    READ_INT(styleVar);
    READ_FLOAT(x);
    READ_FLOAT(y);

    g_drawQueue.push_back([styleVar, x, y]() {
        ImGui::PushStyleVar((ImGuiStyleVar)styleVar, ImVec2(x, y));
    });
}
CLEO_Fn(IMGUI_PUSH_STYLE_COLOR)
{
    READ_INT(col);
    READ_INT(r);
    READ_INT(g);
    READ_INT(b);
    READ_INT(a);

    g_drawQueue.push_back([col, r, g, b, a]() {
        ImGui::PushStyleColor((ImGuiCol)col, ImVec4(r/255.0f, g/255.0f, b/255.0f, a/255.0f));
    });
}
CLEO_Fn(IMGUI_POP_STYLE_VAR)
{
    READ_INT(count);
    g_drawQueue.push_back([count]() {
        ImGui::PopStyleVar(count);
    });
}
CLEO_Fn(IMGUI_POP_STYLE_COLOR)
{
    READ_INT(count);
    g_drawQueue.push_back([count]() {
        ImGui::PopStyleColor(count);
    });
}
CLEO_Fn(IMGUI_GET_FOREGROUND_DRAWLIST)
{
    int dl = (int)(uintptr_t)ImGui::GetForegroundDrawList();
    WRITE_INT(dl);
}
CLEO_Fn(IMGUI_GET_BACKGROUND_DRAWLIST)
{
    int dl = (int)(uintptr_t)ImGui::GetBackgroundDrawList();
    WRITE_INT(dl);
}
CLEO_Fn(IMGUI_DRAWLIST_ADD_LINE)
{
    READ_INT(drawList);
    READ_FLOAT(x1);
    READ_FLOAT(y1);
    READ_FLOAT(x2);
    READ_FLOAT(y2);
    READ_INT(r);
    READ_INT(g);
    READ_INT(b);
    READ_INT(a);
    READ_FLOAT(thickness);

    g_drawQueue.push_back([drawList, x1, y1, x2, y2, r, g, b, a, thickness]() {
        ImDrawList* dl = nullptr;
        switch (drawList) {
            case 0: dl = ImGui::GetWindowDrawList(); break;
            case 1: dl = ImGui::GetForegroundDrawList(); break;
            case 2: dl = ImGui::GetBackgroundDrawList(); break;
            default: dl = ImGui::GetWindowDrawList(); break;
        }
        if (dl) {
            dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(r, g, b, a), thickness);
        }
    });
}
CLEO_Fn(IMGUI_SET_CURSOR_VISIBLE)
{
    READ_INT(show);
    ImGui::GetIO().MouseDrawCursor = (show != 0);
}
CLEO_Fn(IMGUI_GET_WINDOW_SIZE)
{
    READ_FLOAT_PTR(pW);
    READ_FLOAT_PTR(pH);
    READ_STRING(uniqueId, 128);   // se ignora

    g_drawQueue.push_back([pW, pH]() {
        if (pW) *pW = ImGui::GetWindowWidth();
        if (pH) *pH = ImGui::GetWindowHeight();
    });
}
CLEO_Fn(IMGUI_CALC_TEXT_SIZE)
{
    READ_FLOAT_PTR(pW);
    READ_FLOAT_PTR(pH);
    READ_STRING(text, MAX_STR_LEN);

    std::string t = text;
    g_drawQueue.push_back([t, pW, pH]() {
        ImVec2 size = ImGui::CalcTextSize(t.c_str());
        if (pW) *pW = size.x;
        if (pH) *pH = size.y;
    });
}
CLEO_Fn(IMGUI_GET_SCALING_SIZE)
{
    READ_FLOAT_PTR(pX);
    READ_FLOAT_PTR(pY);
    READ_STRING(uniqueId, 128);
    READ_INT(count);
    READ_INT(spacing);

    // Se ignoran count y spacing; devuelve los factores de escala
    if (pX) *pX = flScaleX;
    if (pY) *pY = flScaleY;
}
CLEO_Fn(IMGUI_SET_NEXT_WINDOW_TRANSPARENCY)
{
    READ_FLOAT(alpha);
    g_drawQueue.push_back([alpha]() {
        ImGui::SetNextWindowBgAlpha(alpha);
    });
}
CLEO_Fn(IMGUI_SET_MESSAGE)
{
    READ_STRING(text, MAX_STR_LEN);
    std::string msg = text;

    g_drawQueue.push_back([msg]() {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        ImGui::SetNextWindowBgAlpha(0.8f);
        if (ImGui::Begin("##IMGUI_MESSAGE", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted(msg.c_str());
        }
        ImGui::End();
    });
}
CLEO_Fn(IMGUI_GET_DISPLAY_SIZE)
{
    READ_FLOAT_PTR(pW);
    READ_FLOAT_PTR(pH);

    if (pW) *pW = (float)nDisplayX;
    if (pH) *pH = (float)nDisplayY;
}
CLEO_Fn(IMGUI_GET_WINDOW_DRAWLIST)
{
    int dl = (int)(uintptr_t)ImGui::GetWindowDrawList();
    WRITE_INT(dl);
}


CLEO_Fn(IMGUI_INDENT)
{
    READ_FLOAT(indent_w);
    g_drawQueue.push_back([indent_w]() {
        if (indent_w <= 0.0f) ImGui::Indent();
        else ImGui::Indent(indent_w);
    });
}
CLEO_Fn(IMGUI_UNINDENT)
{
    READ_FLOAT(indent_w);
    g_drawQueue.push_back([indent_w]() {
        if (indent_w <= 0.0f) ImGui::Unindent();
        else ImGui::Unindent(indent_w);
    });
}
CLEO_Fn(IMGUI_BEGIN_GROUP)
{
    g_drawQueue.push_back([]() { ImGui::BeginGroup(); });
}
CLEO_Fn(IMGUI_END_GROUP)
{
    g_drawQueue.push_back([]() { ImGui::EndGroup(); });
}
CLEO_Fn(IMGUI_ALIGN_TEXT_TO_FRAME_PADDING)
{
    g_drawQueue.push_back([]() { ImGui::AlignTextToFramePadding(); });
}
CLEO_Fn(IMGUI_GET_TEXT_LINE_HEIGHT)
{
    READ_FLOAT_PTR(pOut);
    g_drawQueue.push_back([pOut]() {
        *pOut = ImGui::GetTextLineHeight();
    });
}
CLEO_Fn(IMGUI_GET_TEXT_LINE_HEIGHT_WITH_SPACING)
{
    READ_FLOAT_PTR(pOut);
    g_drawQueue.push_back([pOut]() {
        *pOut = ImGui::GetTextLineHeightWithSpacing();
    });
}
CLEO_Fn(IMGUI_SMALL_BUTTON)
{
    READ_STRING(label, 128);
    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, handle]() {
        bool clicked = ImGui::SmallButton(lbl.c_str());
        PUSH_DEFERRED_BOOL(clicked);
    });
}
CLEO_Fn(IMGUI_CHECKBOX_FLAGS_INT)
{
    READ_STRING(label, 128);
    READ_INT_PTR(pFlags);
    READ_INT(flagValue);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pFlags, flagValue, handle]() {
        bool changed = ImGui::CheckboxFlags(lbl.c_str(), pFlags, flagValue);
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_RADIO_BUTTON_ACTIVE)
{
    READ_STRING(label, 128);
    READ_INT(active);       // 0 o 1

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, active, handle]() {
        bool clicked = ImGui::RadioButton(lbl.c_str(), active != 0);
        PUSH_DEFERRED_BOOL(clicked);
    });
}
CLEO_Fn(IMGUI_INPUT_INT_X)
{
    READ_STRING(label, 128);
    READ_INT(count); if (count < 1) count = 1; if (count > 4) count = 4;
    READ_INT_PTR(p0);
    READ_INT_PTR(p1);
    READ_INT_PTR(p2);
    READ_INT_PTR(p3);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, count, p0, p1, p2, p3, handle]() {
        int values[4] = {0,0,0,0};
        values[0] = *p0; values[1] = *p1; values[2] = *p2; values[3] = *p3;

        bool changed = false;
        switch (count) {
            case 1: changed = ImGui::InputInt(lbl.c_str(), &values[0], 1, 100, 0); break;
            case 2: changed = ImGui::InputInt2(lbl.c_str(), values, 0); break;
            case 3: changed = ImGui::InputInt3(lbl.c_str(), values, 0); break;
            case 4: changed = ImGui::InputInt4(lbl.c_str(), values, 0); break;
        }

        *p0 = values[0]; *p1 = values[1]; *p2 = values[2]; *p3 = values[3];
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_INPUT_FLOAT_X)
{
    READ_STRING(label, 128);
    READ_INT(count); if (count < 1) count = 1; if (count > 4) count = 4;
    READ_FLOAT_PTR(p0);
    READ_FLOAT_PTR(p1);
    READ_FLOAT_PTR(p2);
    READ_FLOAT_PTR(p3);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, count, p0, p1, p2, p3, handle]() {
        float values[4] = {0.0f,0.0f,0.0f,0.0f};
        values[0] = *p0; values[1] = *p1; values[2] = *p2; values[3] = *p3;

        bool changed = false;
        switch (count) {
            case 1: changed = ImGui::InputFloat(lbl.c_str(), &values[0], 0.0f, 0.0f, "%.3f", 0); break;
            case 2: changed = ImGui::InputFloat2(lbl.c_str(), values, "%.3f", 0); break;
            case 3: changed = ImGui::InputFloat3(lbl.c_str(), values, "%.3f", 0); break;
            case 4: changed = ImGui::InputFloat4(lbl.c_str(), values, "%.3f", 0); break;
        }

        *p0 = values[0]; *p1 = values[1]; *p2 = values[2]; *p3 = values[3];
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_DRAG_INT)
{
    READ_STRING(label, 128);
    READ_INT_PTR(pVar);
    READ_FLOAT(speed);
    READ_INT(minVal);
    READ_INT(maxVal);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, speed, minVal, maxVal, handle]() {
        bool changed = ImGui::DragInt(lbl.c_str(), pVar, speed, minVal, maxVal, "%d", 0);
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_DRAG_INT_X)
{
    READ_STRING(label, 128);
    READ_INT(count); if (count < 1) count = 1; if (count > 4) count = 4;
    READ_FLOAT(speed);
    READ_INT(minVal); READ_INT(maxVal);
    READ_INT_PTR(p0);
    READ_INT_PTR(p1);
    READ_INT_PTR(p2);
    READ_INT_PTR(p3);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, count, speed, minVal, maxVal, p0, p1, p2, p3, handle]() {
        int values[4] = {0,0,0,0};
        values[0] = *p0; values[1] = *p1; values[2] = *p2; values[3] = *p3;

        bool changed = false;
        switch (count) {
            case 1: changed = ImGui::DragInt(lbl.c_str(), &values[0], speed, minVal, maxVal, "%d", 0); break;
            case 2: changed = ImGui::DragInt2(lbl.c_str(), values, speed, minVal, maxVal, "%d", 0); break;
            case 3: changed = ImGui::DragInt3(lbl.c_str(), values, speed, minVal, maxVal, "%d", 0); break;
            case 4: changed = ImGui::DragInt4(lbl.c_str(), values, speed, minVal, maxVal, "%d", 0); break;
        }

        *p0 = values[0]; *p1 = values[1]; *p2 = values[2]; *p3 = values[3];
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_DRAG_FLOAT)
{
    READ_STRING(label, 128);
    READ_FLOAT_PTR(pVar);
    READ_FLOAT(speed);
    READ_FLOAT(minVal);
    READ_FLOAT(maxVal);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, speed, minVal, maxVal, handle]() {
        bool changed = ImGui::DragFloat(lbl.c_str(), pVar, speed, minVal, maxVal, "%.3f", 0);
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_DRAG_FLOAT_X)
{
    READ_STRING(label, 128);
    READ_INT(count); if (count < 1) count = 1; if (count > 4) count = 4;
    READ_FLOAT(speed);
    READ_FLOAT(minVal); READ_FLOAT(maxVal);
    READ_FLOAT_PTR(p0);
    READ_FLOAT_PTR(p1);
    READ_FLOAT_PTR(p2);
    READ_FLOAT_PTR(p3);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, count, speed, minVal, maxVal, p0, p1, p2, p3, handle]() {
        float values[4] = {0.0f,0.0f,0.0f,0.0f};
        values[0] = *p0; values[1] = *p1; values[2] = *p2; values[3] = *p3;

        bool changed = false;
        switch (count) {
            case 1: changed = ImGui::DragFloat(lbl.c_str(), &values[0], speed, minVal, maxVal, "%.3f", 0); break;
            case 2: changed = ImGui::DragFloat2(lbl.c_str(), values, speed, minVal, maxVal, "%.3f", 0); break;
            case 3: changed = ImGui::DragFloat3(lbl.c_str(), values, speed, minVal, maxVal, "%.3f", 0); break;
            case 4: changed = ImGui::DragFloat4(lbl.c_str(), values, speed, minVal, maxVal, "%.3f", 0); break;
        }

        *p0 = values[0]; *p1 = values[1]; *p2 = values[2]; *p3 = values[3];
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_SLIDER_INT_X)
{
    READ_STRING(label, 128);
    READ_INT(count); if (count < 1) count = 1; if (count > 4) count = 4;
    READ_INT(minVal); READ_INT(maxVal);
    READ_INT_PTR(p0);
    READ_INT_PTR(p1);
    READ_INT_PTR(p2);
    READ_INT_PTR(p3);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, count, minVal, maxVal, p0, p1, p2, p3, handle]() {
        int values[4] = {0,0,0,0};
        values[0] = *p0; values[1] = *p1; values[2] = *p2; values[3] = *p3;

        bool changed = false;
        switch (count) {
            case 1: changed = ImGui::SliderInt(lbl.c_str(), &values[0], minVal, maxVal, "%d", 0); break;
            case 2: changed = ImGui::SliderInt2(lbl.c_str(), values, minVal, maxVal, "%d", 0); break;
            case 3: changed = ImGui::SliderInt3(lbl.c_str(), values, minVal, maxVal, "%d", 0); break;
            case 4: changed = ImGui::SliderInt4(lbl.c_str(), values, minVal, maxVal, "%d", 0); break;
        }

        *p0 = values[0]; *p1 = values[1]; *p2 = values[2]; *p3 = values[3];
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_SLIDER_FLOAT_X)
{
    READ_STRING(label, 128);
    READ_INT(count); if (count < 1) count = 1; if (count > 4) count = 4;
    READ_FLOAT(minVal); READ_FLOAT(maxVal);
    READ_FLOAT_PTR(p0);
    READ_FLOAT_PTR(p1);
    READ_FLOAT_PTR(p2);
    READ_FLOAT_PTR(p3);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, count, minVal, maxVal, p0, p1, p2, p3, handle]() {
        float values[4] = {0.0f,0.0f,0.0f,0.0f};
        values[0] = *p0; values[1] = *p1; values[2] = *p2; values[3] = *p3;

        bool changed = false;
        switch (count) {
            case 1: changed = ImGui::SliderFloat(lbl.c_str(), &values[0], minVal, maxVal, "%.3f", 0); break;
            case 2: changed = ImGui::SliderFloat2(lbl.c_str(), values, minVal, maxVal, "%.3f", 0); break;
            case 3: changed = ImGui::SliderFloat3(lbl.c_str(), values, minVal, maxVal, "%.3f", 0); break;
            case 4: changed = ImGui::SliderFloat4(lbl.c_str(), values, minVal, maxVal, "%.3f", 0); break;
        }

        *p0 = values[0]; *p1 = values[1]; *p2 = values[2]; *p3 = values[3];
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_SLIDER_ANGLE)
{
    READ_STRING(label, 128);
    READ_FLOAT_PTR(pAngleRad);
    READ_FLOAT(degMin);
    READ_FLOAT(degMax);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pAngleRad, degMin, degMax, handle]() {
        bool changed = ImGui::SliderAngle(lbl.c_str(), pAngleRad, degMin, degMax, "%.0f deg", 0);
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_VALUE_BOOL)
{
    READ_STRING(prefix, 128);
    READ_INT(value);
    std::string p = prefix;
    g_drawQueue.push_back([p, value]() {
        ImGui::Value(p.c_str(), value != 0);
    });
}
CLEO_Fn(IMGUI_VALUE_INT)
{
    READ_STRING(prefix, 128);
    READ_INT(value);
    std::string p = prefix;
    g_drawQueue.push_back([p, value]() {
        ImGui::Value(p.c_str(), value);
    });
}
CLEO_Fn(IMGUI_VALUE_FLOAT)
{
    READ_STRING(prefix, 128);
    READ_FLOAT(value);
    std::string p = prefix;
    g_drawQueue.push_back([p, value]() {
        ImGui::Value(p.c_str(), value, "%.3f");
    });
}

// 2308: imgui_tree_node
CLEO_Fn(IMGUI_TREE_NODE)
{
    READ_STRING(label, 128);
    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, handle]() {
        bool open = ImGui::TreeNode(lbl.c_str());
        PUSH_DEFERRED_BOOL(open);
    });
}

// 2309: imgui_tree_node_ex
CLEO_Fn(IMGUI_TREE_NODE_EX)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, handle]() {
        bool open = ImGui::TreeNodeEx(lbl.c_str(), flags);
        PUSH_DEFERRED_BOOL(open);
    });
}

// 230A: imgui_tree_push
CLEO_Fn(IMGUI_TREE_PUSH)
{
    READ_STRING(str_id, 128);
    std::string id = str_id;
    g_drawQueue.push_back([id]() {
        ImGui::TreePush(id.c_str());
    });
}

// 230B: imgui_tree_pop
CLEO_Fn(IMGUI_TREE_POP)
{
    g_drawQueue.push_back([]() { ImGui::TreePop(); });
}

// 230C: imgui_collapsing_header_visible (versión con bool* p_visible)
CLEO_Fn(IMGUI_COLLAPSING_HEADER_EX)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_INT_PTR(pVisible);
    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, pVisible, handle]() {
        bool visible = (*pVisible != 0);
        bool open = ImGui::CollapsingHeader(lbl.c_str(), &visible, flags);
        *pVisible = visible ? 1 : 0;
        PUSH_DEFERRED_BOOL(open);
    });
}

// 230D: imgui_set_next_item_open
CLEO_Fn(IMGUI_SET_NEXT_ITEM_OPEN)
{
    READ_INT(isOpen);
    READ_INT(cond);
    g_drawQueue.push_back([isOpen, cond]() {
        ImGui::SetNextItemOpen(isOpen != 0, cond);
    });
}
// 230E: imgui_open_popup
CLEO_Fn(IMGUI_OPEN_POPUP)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);
    std::string id = str_id;
    g_drawQueue.push_back([id, flags]() {
        ImGui::OpenPopup(id.c_str(), flags);
    });
}

// 230F: imgui_begin_popup
CLEO_Fn(IMGUI_BEGIN_POPUP)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string id = str_id;
    g_drawQueue.push_back([id, flags, handle]() {
        bool opened = ImGui::BeginPopup(id.c_str(), flags);
        PUSH_DEFERRED_BOOL(opened);
    });
}

// 2310: imgui_end_popup
CLEO_Fn(IMGUI_END_POPUP)
{
    g_drawQueue.push_back([]() { ImGui::EndPopup(); });
}

// 2311: imgui_close_current_popup
CLEO_Fn(IMGUI_CLOSE_CURRENT_POPUP)
{
    g_drawQueue.push_back([]() { ImGui::CloseCurrentPopup(); });
}

// 2312: imgui_open_popup_on_item_click
CLEO_Fn(IMGUI_OPEN_POPUP_ON_ITEM_CLICK)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);
    std::string id = str_id;
    g_drawQueue.push_back([id, flags]() {
        ImGui::OpenPopupOnItemClick(id.c_str(), flags);
    });
}

// 2313: imgui_begin_popup_context_item
CLEO_Fn(IMGUI_BEGIN_POPUP_CONTEXT_ITEM)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string id = str_id;
    g_drawQueue.push_back([id, flags, handle]() {
        bool opened = ImGui::BeginPopupContextItem(id.c_str(), flags);
        PUSH_DEFERRED_BOOL(opened);
    });
}

// 2314: imgui_begin_popup_context_window
CLEO_Fn(IMGUI_BEGIN_POPUP_CONTEXT_WINDOW)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string id = str_id;
    g_drawQueue.push_back([id, flags, handle]() {
        bool opened = ImGui::BeginPopupContextWindow(id.c_str(), flags);
        PUSH_DEFERRED_BOOL(opened);
    });
}

// 2315: imgui_begin_popup_context_void
CLEO_Fn(IMGUI_BEGIN_POPUP_CONTEXT_VOID)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string id = str_id;
    g_drawQueue.push_back([id, flags, handle]() {
        bool opened = ImGui::BeginPopupContextVoid(id.c_str(), flags);
        PUSH_DEFERRED_BOOL(opened);
    });
}

// 2316: imgui_is_popup_open (directo)
CLEO_Fn(IMGUI_IS_POPUP_OPEN)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);

    bool ret = ImGui::IsPopupOpen(str_id, flags);
    RET_COMPARE(ret);
}
// 2317: imgui_begin_tab_bar
CLEO_Fn(IMGUI_BEGIN_TAB_BAR)
{
    READ_STRING(str_id, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string id = str_id;
    g_drawQueue.push_back([id, flags, handle]() {
        bool opened = ImGui::BeginTabBar(id.c_str(), flags);
        PUSH_DEFERRED_BOOL(opened);
    });
}

// 2318: imgui_end_tab_bar
CLEO_Fn(IMGUI_END_TAB_BAR)
{
    g_drawQueue.push_back([]() { ImGui::EndTabBar(); });
}

// 2319: imgui_begin_tab_item
CLEO_Fn(IMGUI_BEGIN_TAB_ITEM)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, handle]() {
        bool opened = ImGui::BeginTabItem(lbl.c_str(), nullptr, flags);
        PUSH_DEFERRED_BOOL(opened);
    });
}

// 231A: imgui_end_tab_item
CLEO_Fn(IMGUI_END_TAB_ITEM)
{
    g_drawQueue.push_back([]() { ImGui::EndTabItem(); });
}

// 231B: imgui_tab_item_button
CLEO_Fn(IMGUI_TAB_ITEM_BUTTON)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, handle]() {
        bool clicked = ImGui::TabItemButton(lbl.c_str(), flags);
        PUSH_DEFERRED_BOOL(clicked);
    });
}

// 231C: imgui_set_tab_item_closed
CLEO_Fn(IMGUI_SET_TAB_ITEM_CLOSED)
{
    READ_STRING(tab_label, 128);
    std::string lbl = tab_label;
    g_drawQueue.push_back([lbl]() {
        ImGui::SetTabItemClosed(lbl.c_str());
    });
}
// 231D: imgui_begin_table
CLEO_Fn(IMGUI_BEGIN_TABLE)
{
    READ_STRING(str_id, 128);
    READ_INT(column);
    READ_INT(flags);
    READ_FLOAT(outer_size_x);
    READ_FLOAT(outer_size_y);
    READ_FLOAT(inner_width);
    APPLY_DEFERRED_COND();

    std::string id = str_id;
    g_drawQueue.push_back([id, column, flags, outer_size_x, outer_size_y, inner_width, handle]() {
        bool opened = ImGui::BeginTable(id.c_str(), column, flags, ImVec2(outer_size_x, outer_size_y), inner_width);
        PUSH_DEFERRED_BOOL(opened);
    });
}

// 231E: imgui_end_table
CLEO_Fn(IMGUI_END_TABLE)
{
    g_drawQueue.push_back([]() { ImGui::EndTable(); });
}

// 231F: imgui_table_next_row
CLEO_Fn(IMGUI_TABLE_NEXT_ROW)
{
    READ_INT(row_flags);
    READ_FLOAT(min_row_height);
    g_drawQueue.push_back([row_flags, min_row_height]() {
        ImGui::TableNextRow(row_flags, min_row_height);
    });
}

// 2320: imgui_table_next_column
CLEO_Fn(IMGUI_TABLE_NEXT_COLUMN)
{
    APPLY_DEFERRED_COND();
    g_drawQueue.push_back([handle]() {
        bool visible = ImGui::TableNextColumn();
        PUSH_DEFERRED_BOOL(visible);
    });
}

// 2321: imgui_table_set_column_index
CLEO_Fn(IMGUI_TABLE_SET_COLUMN_INDEX)
{
    READ_INT(column_n);
    APPLY_DEFERRED_COND();
    g_drawQueue.push_back([column_n, handle]() {
        bool ok = ImGui::TableSetColumnIndex(column_n);
        PUSH_DEFERRED_BOOL(ok);
    });
}

// 2322: imgui_table_setup_column
CLEO_Fn(IMGUI_TABLE_SETUP_COLUMN)
{
    READ_STRING(label, 128);
    READ_INT(flags);
    READ_FLOAT(init_width_or_weight);
    READ_INT(user_id);
    std::string lbl = label;
    g_drawQueue.push_back([lbl, flags, init_width_or_weight, user_id]() {
        ImGui::TableSetupColumn(lbl.c_str(), flags, init_width_or_weight, user_id);
    });
}

// 2323: imgui_table_setup_scroll_freeze
CLEO_Fn(IMGUI_TABLE_SETUP_SCROLL_FREEZE)
{
    READ_INT(cols);
    READ_INT(rows);
    g_drawQueue.push_back([cols, rows]() {
        ImGui::TableSetupScrollFreeze(cols, rows);
    });
}

// 2324: imgui_table_headers_row
CLEO_Fn(IMGUI_TABLE_HEADERS_ROW)
{
    g_drawQueue.push_back([]() { ImGui::TableHeadersRow(); });
}

// 2325: imgui_table_header
CLEO_Fn(IMGUI_TABLE_HEADER)
{
    READ_STRING(label, 128);
    std::string lbl = label;
    g_drawQueue.push_back([lbl]() {
        ImGui::TableHeader(lbl.c_str());
    });
}

// 2326: imgui_table_get_column_count
CLEO_Fn(IMGUI_TABLE_GET_COLUMN_COUNT)
{
    READ_INT_PTR(pOut);
    g_drawQueue.push_back([pOut]() {
        *pOut = ImGui::TableGetColumnCount();
    });
}

// 2327: imgui_table_get_column_index
CLEO_Fn(IMGUI_TABLE_GET_COLUMN_INDEX)
{
    READ_INT_PTR(pOut);
    g_drawQueue.push_back([pOut]() {
        *pOut = ImGui::TableGetColumnIndex();
    });
}

// 2328: imgui_table_get_row_index
CLEO_Fn(IMGUI_TABLE_GET_ROW_INDEX)
{
    READ_INT_PTR(pOut);
    g_drawQueue.push_back([pOut]() {
        *pOut = ImGui::TableGetRowIndex();
    });
}

// 2329: imgui_table_get_column_name
CLEO_Fn(IMGUI_TABLE_GET_COLUMN_NAME)
{
    READ_INT(column_n);
    g_drawQueue.push_back([column_n, handle]() {
        const char* name = ImGui::TableGetColumnName(column_n);
        cleoaddon->WriteString(handle, name ? name : "");
    });
}

// 232A: imgui_table_get_column_flags
CLEO_Fn(IMGUI_TABLE_GET_COLUMN_FLAGS)
{
    READ_INT(column_n);
    READ_INT_PTR(pOut);
    g_drawQueue.push_back([column_n, pOut]() {
        *pOut = ImGui::TableGetColumnFlags(column_n);
    });
}

// 232B: imgui_table_set_column_enabled
CLEO_Fn(IMGUI_TABLE_SET_COLUMN_ENABLED)
{
    READ_INT(column_n);
    READ_INT(enabled);
    g_drawQueue.push_back([column_n, enabled]() {
        ImGui::TableSetColumnEnabled(column_n, enabled != 0);
    });
}

// 232C: imgui_table_set_bg_color
CLEO_Fn(IMGUI_TABLE_SET_BG_COLOR)
{
    READ_INT(target);
    READ_INT(color);
    READ_INT(column_n);
    g_drawQueue.push_back([target, color, column_n]() {
        ImGui::TableSetBgColor(target, color, column_n);
    });
}

CLEO_Fn(IMGUI_LABEL_TEXT)
{
    READ_STRING(label, 128);
    READ_STRING(text, 256);
    std::string lbl = label;
    std::string txt = text;
    g_drawQueue.push_back([lbl, txt]() {
        ImGui::LabelText(lbl.c_str(), "%s", txt.c_str());
    });
}
CLEO_Fn(IMGUI_BEGIN_TOOLTIP)
{
    g_drawQueue.push_back([]() {
        ImGui::BeginTooltip();
    });
}
CLEO_Fn(IMGUI_END_TOOLTIP)
{
    g_drawQueue.push_back([]() {
        ImGui::EndTooltip();
    });
}
CLEO_Fn(IMGUI_BEGIN_MENU)
{
    READ_STRING(label, 128);
    READ_INT(enabled); // 1 = habilitado, 0 = deshabilitado
    std::string lbl = label;
    g_drawQueue.push_back([lbl, enabled]() {
        ImGui::BeginMenu(lbl.c_str(), enabled != 0);
    });
}
CLEO_Fn(IMGUI_END_MENU)
{
    g_drawQueue.push_back([]() {
        ImGui::EndMenu();
    });
}
CLEO_Fn(IMGUI_MENU_ITEM_EX)
{
    READ_STRING(label, 128);
    READ_STRING(shortcut, 64);
    READ_INT(selected);   // 0/1
    READ_INT(enabled);    // 0/1

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    std::string scut = shortcut;
    g_drawQueue.push_back([lbl, scut, selected, enabled, handle]() {
        bool activated = ImGui::MenuItem(lbl.c_str(), scut.c_str(), selected != 0, enabled != 0);
        PUSH_DEFERRED_BOOL(activated);
    });
}
CLEO_Fn(IMGUI_BEGIN_LISTBOX)
{
    READ_STRING(label, 128);
    READ_FLOAT(w);
    READ_FLOAT(h);
    std::string lbl = label;
    g_drawQueue.push_back([lbl, w, h]() {
        ImGui::BeginListBox(lbl.c_str(), ImVec2(w, h));
    });
}
CLEO_Fn(IMGUI_END_LISTBOX)
{
    g_drawQueue.push_back([]() {
        ImGui::EndListBox();
    });
}
CLEO_Fn(IMGUI_LISTBOX)
{
    READ_STRING(label, 128);
    READ_STRING(itemsStr, 1024);
    READ_INT_PTR(pCurrent);
    READ_INT(heightItems);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    std::string opts = itemsStr;
    g_drawQueue.push_back([lbl, opts, pCurrent, heightItems, handle]() {
        // Separar items por comas
        std::vector<std::string> items;
        std::string cur;
        for (size_t i = 0; i <= opts.size(); ++i) {
            if (i == opts.size() || opts[i] == ',') {
                items.push_back(cur);
                cur.clear();
            } else {
                cur += opts[i];
            }
        }
        int& current = *pCurrent;
        bool changed = false;
        if (ImGui::BeginListBox(lbl.c_str(), ImVec2(0, 0))) {
            for (size_t i = 0; i < items.size(); ++i) {
                bool isSelected = (current == (int)i);
                if (ImGui::Selectable(items[i].c_str(), isSelected)) {
                    current = (int)i;
                    changed = true;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }
        PUSH_DEFERRED_BOOL(changed);
    });
}
CLEO_Fn(IMGUI_PLOT_LINES)
{
    READ_STRING(label, 128);
    int valuesAddr = cleo->ReadParam(handle)->i;   // dirección del array de floats
    READ_INT(count);
    READ_STRING(overlay, 64);
    READ_FLOAT(scaleMin);
    READ_FLOAT(scaleMax);
    READ_FLOAT(graphW);
    READ_FLOAT(graphH);

    std::string lbl = label;
    std::string ov = overlay;
    g_drawQueue.push_back([lbl, valuesAddr, count, ov, scaleMin, scaleMax, graphW, graphH]() {
        const float* values = reinterpret_cast<const float*>(valuesAddr);
        ImGui::PlotLines(lbl.c_str(), values, count, 0, ov.empty() ? nullptr : ov.c_str(),
                         scaleMin, scaleMax, ImVec2(graphW, graphH));
    });
}
CLEO_Fn(IMGUI_PLOT_HISTOGRAM)
{
    READ_STRING(label, 128);
    int valuesAddr = cleo->ReadParam(handle)->i;   // dirección del array de floats
    READ_INT(count);
    READ_STRING(overlay, 64);
    READ_FLOAT(scaleMin);
    READ_FLOAT(scaleMax);
    READ_FLOAT(graphW);
    READ_FLOAT(graphH);

    std::string lbl = label;
    std::string ov = overlay;
    g_drawQueue.push_back([lbl, valuesAddr, count, ov, scaleMin, scaleMax, graphW, graphH]() {
        const float* values = reinterpret_cast<const float*>(valuesAddr);
        ImGui::PlotHistogram(lbl.c_str(), values, count, 0, ov.empty() ? nullptr : ov.c_str(),
                             scaleMin, scaleMax, ImVec2(graphW, graphH));
    });
}
CLEO_Fn(IMGUI_GET_TIME)
{
    READ_FLOAT_PTR(pOut);
    if (pOut) *pOut = (float)ImGui::GetTime();
}
CLEO_Fn(IMGUI_GET_FRAME_COUNT)
{
    READ_INT_PTR(pOut);
    if (pOut) *pOut = ImGui::GetFrameCount();
}

CLEO_Fn(IMGUI_VSLIDER_INT)
{
    READ_STRING(label, 128);
    READ_INT(minVal);
    READ_INT(maxVal);
    READ_FLOAT(width);
    READ_FLOAT(height);
    READ_INT_PTR(pVar);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, minVal, maxVal, width, height, handle]() {
        bool changed = ImGui::VSliderInt(lbl.c_str(), ImVec2(width, height), pVar, minVal, maxVal, "%d", 0);
        PUSH_DEFERRED_BOOL(changed);
    });
}

CLEO_Fn(IMGUI_VSLIDER_FLOAT)
{
    READ_STRING(label, 128);
    READ_FLOAT(minVal);
    READ_FLOAT(maxVal);
    READ_FLOAT(width);
    READ_FLOAT(height);
    READ_FLOAT_PTR(pVar);

    APPLY_DEFERRED_COND();

    std::string lbl = label;
    g_drawQueue.push_back([lbl, pVar, minVal, maxVal, width, height, handle]() {
        bool changed = ImGui::VSliderFloat(lbl.c_str(), ImVec2(width, height), pVar, minVal, maxVal, "%.2f", 0);
        PUSH_DEFERRED_BOOL(changed);
    });
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

void SAUtilsStarted()
{
    snprintf(szCLEOImGuiVer, sizeof(szCLEOImGuiVer), "ImGui v.1.3.0");
    sautils->AddButton(SetType_Mods, szCLEOImGuiVer, NoneFunctionLogic);
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

    sautils = (ISAUtils*)GetInterface("SAUtils");
    if (sautils) SAUtilsStarted();
    cleo = (cleo_ifs_t*)GetInterface("CLEO");
    if (!cleo) { logger->Error("CLEO interface not found"); return; }
    cleoaddon = (cleo_addon_ifs_t*)GetInterface("CLEOAddon");
    if (!cleoaddon) { logger->Error("CLEOAddon interface not found"); return; }

    CLEO_RegisterOpcode(0x0F18, IMGUI_SLIDER_FLOAT);
    CLEO_RegisterOpcode(0x0F19, IMGUI_COLOR_EDIT);
    CLEO_RegisterOpcode(0x0F1A, IMGUI_COLOR_PICKER);
    CLEO_RegisterOpcode(0x0F1B, IMGUI_BEGIN_CHILD_A);
    CLEO_RegisterOpcode(0x0F1C, IMGUI_END_CHILD);
    CLEO_RegisterOpcode(0x0F1D, IMGUI_INPUT_INT);
    CLEO_RegisterOpcode(0x0F1E, IMGUI_INPUT_FLOAT);
    CLEO_RegisterOpcode(0x0F1F, IMGUI_INPUT_TEXT);
    CLEO_RegisterOpcode(0x0F20, IMGUI_INPUT_TEXT_MULTILINE);
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
    CLEO_RegisterOpcode(0x0F40, IMGUI_LOAD_IMAGE);
    CLEO_RegisterOpcode(0x0F41, IMGUI_IMAGE);
    CLEO_RegisterOpcode(0x0F42, IMGUI_IMAGE_EX);
    CLEO_RegisterOpcode(0x0F43, IMGUI_IMAGE_BUTTON);
    CLEO_RegisterOpcode(0x0F44, IMGUI_IMAGE_BUTTON_EX);
    CLEO_RegisterOpcode(0x0F45, IMGUI_GET_GAME_PATH);
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
    
    // CLEO Redux
    // ────────────────────────────────────

    //CLEO_RegisterOpcode(0x2200, IMGUI_BEGIN_FRAME);
    //CLEO_RegisterOpcode(0x2201, IMGUI_END_FRAME);
    CLEO_RegisterOpcode(0x2202, IMGUI_BEGIN_B);
    CLEO_RegisterOpcode(0x2203, IMGUI_END);
    CLEO_RegisterOpcode(0x2204, IMGUI_BEGIN_MAIN_MENU_BAR_B);
    CLEO_RegisterOpcode(0x2205, IMGUI_END_MAIN_MENU_BAR);
    CLEO_RegisterOpcode(0x2206, IMGUI_BEGIN_CHILD_B);
    CLEO_RegisterOpcode(0x2207, IMGUI_END_CHILD);
    CLEO_RegisterOpcode(0x2208, IMGUI_TABS);
    CLEO_RegisterOpcode(0x2209, IMGUI_COLLAPSING_HEADER);
    CLEO_RegisterOpcode(0x220A, IMGUI_SET_WINDOW_POS);
    CLEO_RegisterOpcode(0x220B, IMGUI_SET_WINDOW_SIZE);
    CLEO_RegisterOpcode(0x220C, IMGUI_SET_NEXT_WINDOW_POS);
    CLEO_RegisterOpcode(0x220D, IMGUI_SET_NEXT_WINDOW_SIZE);
    CLEO_RegisterOpcode(0x220E, IMGUI_TEXT);
    CLEO_RegisterOpcode(0x220F, IMGUI_TEXT_CENTERED);
    CLEO_RegisterOpcode(0x2210, IMGUI_TEXT_DISABLED);
    CLEO_RegisterOpcode(0x2211, IMGUI_TEXT_WRAPPED);
    CLEO_RegisterOpcode(0x2212, IMGUI_TEXT_COLORED);
    CLEO_RegisterOpcode(0x2213, IMGUI_BULLET_TEXT);
    CLEO_RegisterOpcode(0x2214, IMGUI_BULLET);
    CLEO_RegisterOpcode(0x2215, IMGUI_CHECKBOX);
    CLEO_RegisterOpcode(0x2216, IMGUI_COMBO);
    CLEO_RegisterOpcode(0x2217, IMGUI_SET_TOOLTIP);
    CLEO_RegisterOpcode(0x2218, IMGUI_BUTTON);
    CLEO_RegisterOpcode(0x2219, IMGUI_IMAGE_BUTTON);
    CLEO_RegisterOpcode(0x221A, IMGUI_INVISIBLE_BUTTON_B);
    CLEO_RegisterOpcode(0x221B, IMGUI_COLOR_BUTTON_B);
    CLEO_RegisterOpcode(0x221C, IMGUI_ARROW_BUTTON);
    CLEO_RegisterOpcode(0x221D, IMGUI_SLIDER_INT_B);
    CLEO_RegisterOpcode(0x221E, IMGUI_SLIDER_FLOAT_B);
    CLEO_RegisterOpcode(0x221F, IMGUI_INPUT_INT_B);
    CLEO_RegisterOpcode(0x2220, IMGUI_INPUT_FLOAT_B);
    CLEO_RegisterOpcode(0x2221, IMGUI_INPUT_TEXT_B);
    CLEO_RegisterOpcode(0x2222, IMGUI_RADIO_BUTTON_B);
    CLEO_RegisterOpcode(0x2223, IMGUI_COLOR_PICKER);
    CLEO_RegisterOpcode(0x2224, IMGUI_MENU_ITEM);
    CLEO_RegisterOpcode(0x2225, IMGUI_SELECTABLE_B);
    CLEO_RegisterOpcode(0x2226, IMGUI_DUMMY);
    CLEO_RegisterOpcode(0x2227, IMGUI_SAMELINE);
    CLEO_RegisterOpcode(0x2228, IMGUI_NEWLINE);
    CLEO_RegisterOpcode(0x2229, IMGUI_COLUMNS_B);
    CLEO_RegisterOpcode(0x222A, IMGUI_NEXT_COLUMN);
    CLEO_RegisterOpcode(0x222B, IMGUI_SPACING);
    CLEO_RegisterOpcode(0x222C, IMGUI_SEPARATOR);
    CLEO_RegisterOpcode(0x222D, IMGUI_PUSH_ITEM_WIDTH);
    CLEO_RegisterOpcode(0x222E, IMGUI_POP_ITEM_WIDTH);
    CLEO_RegisterOpcode(0x222F, IMGUI_IS_ITEM_ACTIVE_B);
    CLEO_RegisterOpcode(0x2230, IMGUI_IS_ITEM_CLICKED_B);
    CLEO_RegisterOpcode(0x2231, IMGUI_IS_ITEM_FOCUSED_B);
    CLEO_RegisterOpcode(0x2232, IMGUI_IS_ITEM_HOVERED_B);
    CLEO_RegisterOpcode(0x2233, IMGUI_SET_ITEM_INT);
    CLEO_RegisterOpcode(0x2234, IMGUI_SET_ITEM_FLOAT);
    CLEO_RegisterOpcode(0x2235, IMGUI_SET_ITEM_TEXT);
    CLEO_RegisterOpcode(0x2236, IMGUI_SET_IMAGE_BG_COLOR);
    CLEO_RegisterOpcode(0x2237, IMGUI_SET_IMAGE_TINT_COLOR);
    CLEO_RegisterOpcode(0x2238, IMGUI_LOAD_IMAGE);
    CLEO_RegisterOpcode(0x2239, IMGUI_FREE_IMAGE);
    CLEO_RegisterOpcode(0x223A, IMGUI_PUSH_STYLE_VAR);
    CLEO_RegisterOpcode(0x223B, IMGUI_PUSH_STYLE_VAR2);
    CLEO_RegisterOpcode(0x223C, IMGUI_PUSH_STYLE_COLOR);
    CLEO_RegisterOpcode(0x223D, IMGUI_POP_STYLE_VAR);
    CLEO_RegisterOpcode(0x223E, IMGUI_POP_STYLE_COLOR);
    CLEO_RegisterOpcode(0x223F, IMGUI_GET_FOREGROUND_DRAWLIST);
    CLEO_RegisterOpcode(0x2240, IMGUI_GET_BACKGROUND_DRAWLIST);
    CLEO_RegisterOpcode(0x2241, IMGUI_GET_WINDOW_DRAWLIST);
    CLEO_RegisterOpcode(0x2242, IMGUI_DRAWLIST_ADD_TEXT_B);
    CLEO_RegisterOpcode(0x2243, IMGUI_DRAWLIST_ADD_LINE);
    CLEO_RegisterOpcode(0x2244, IMGUI_GET_FRAMERATE);
    CLEO_RegisterOpcode(0x2245, IMGUI_GET_VERSION);
    CLEO_RegisterOpcode(0x2246, IMGUI_GET_CLEO_IMGUI_VERSION);
    CLEO_RegisterOpcode(0x2247, IMGUI_SET_CURSOR_VISIBLE);
    CLEO_RegisterOpcode(0x2248, IMGUI_GET_FRAME_HEIGHT);
    CLEO_RegisterOpcode(0x2249, IMGUI_GET_WINDOW_POS);
    CLEO_RegisterOpcode(0x224A, IMGUI_GET_WINDOW_SIZE);
    CLEO_RegisterOpcode(0x224B, IMGUI_CALC_TEXT_SIZE);
    CLEO_RegisterOpcode(0x224C, IMGUI_GET_WINDOW_CONTENT_REGION_WIDTH);
    CLEO_RegisterOpcode(0x224D, IMGUI_GET_SCALING_SIZE);
    CLEO_RegisterOpcode(0x224E, IMGUI_GET_DISPLAY_SIZE);
    CLEO_RegisterOpcode(0x224F, IMGUI_SET_NEXT_WINDOW_TRANSPARENCY);
    CLEO_RegisterOpcode(0x2250, IMGUI_SET_MESSAGE);


    CLEO_RegisterOpcode(0x7100, IMGUI_SET_TEXT_LIMIT);
    CLEO_RegisterOpcode(0x7101, IMGUI_KEYBOARD_SHOW);
    CLEO_RegisterOpcode(0x7102, IMGUI_KEYBOARD_HIDE);
    CLEO_RegisterOpcode(0x7103, IMGUI_KEYBOARD_IS_VISIBLE);
    CLEO_RegisterOpcode(0x7104, IMGUI_IMAGE_RESET_COLOR);
    CLEO_RegisterOpcode(0x7105, IMGUI_KEYBOARD_SET_ENTER_MODE);

    CLEO_RegisterOpcode(0x7110, IMGUI_INDENT);
    CLEO_RegisterOpcode(0x7111, IMGUI_UNINDENT);
    CLEO_RegisterOpcode(0x7112, IMGUI_BEGIN_GROUP);
    CLEO_RegisterOpcode(0x7113, IMGUI_END_GROUP);
    CLEO_RegisterOpcode(0x7114, IMGUI_ALIGN_TEXT_TO_FRAME_PADDING);
    CLEO_RegisterOpcode(0x7115, IMGUI_GET_TEXT_LINE_HEIGHT);
    CLEO_RegisterOpcode(0x7116, IMGUI_GET_TEXT_LINE_HEIGHT_WITH_SPACING);
    CLEO_RegisterOpcode(0x7117, IMGUI_SMALL_BUTTON);
    CLEO_RegisterOpcode(0x7118, IMGUI_CHECKBOX_FLAGS_INT);
    CLEO_RegisterOpcode(0x7119, IMGUI_RADIO_BUTTON_ACTIVE);
    CLEO_RegisterOpcode(0x711A, IMGUI_INPUT_INT_X);
    CLEO_RegisterOpcode(0x711B, IMGUI_INPUT_FLOAT_X);
    CLEO_RegisterOpcode(0x711C, IMGUI_DRAG_INT);
    CLEO_RegisterOpcode(0x711D, IMGUI_DRAG_INT_X);
    CLEO_RegisterOpcode(0x711E, IMGUI_DRAG_FLOAT);
    CLEO_RegisterOpcode(0x711F, IMGUI_DRAG_FLOAT_X);
    CLEO_RegisterOpcode(0x7120, IMGUI_SLIDER_INT_X);
    CLEO_RegisterOpcode(0x7121, IMGUI_SLIDER_FLOAT_X);
    CLEO_RegisterOpcode(0x7122, IMGUI_SLIDER_ANGLE);
    CLEO_RegisterOpcode(0x7123, IMGUI_VALUE_BOOL);
    CLEO_RegisterOpcode(0x7124, IMGUI_VALUE_INT);
    CLEO_RegisterOpcode(0x7125, IMGUI_VALUE_FLOAT);

    // Trees
    CLEO_RegisterOpcode(0x7126, IMGUI_TREE_NODE);
    CLEO_RegisterOpcode(0x7127, IMGUI_TREE_NODE_EX);
    CLEO_RegisterOpcode(0x7128, IMGUI_TREE_PUSH);
    CLEO_RegisterOpcode(0x7129, IMGUI_TREE_POP);
    CLEO_RegisterOpcode(0x712A, IMGUI_COLLAPSING_HEADER_EX);
    CLEO_RegisterOpcode(0x712B, IMGUI_SET_NEXT_ITEM_OPEN);

    // Popups
    CLEO_RegisterOpcode(0x712C, IMGUI_OPEN_POPUP);
    CLEO_RegisterOpcode(0x712D, IMGUI_BEGIN_POPUP);
    CLEO_RegisterOpcode(0x712E, IMGUI_END_POPUP);
    CLEO_RegisterOpcode(0x712F, IMGUI_CLOSE_CURRENT_POPUP);
    CLEO_RegisterOpcode(0x7130, IMGUI_OPEN_POPUP_ON_ITEM_CLICK);
    CLEO_RegisterOpcode(0x7131, IMGUI_BEGIN_POPUP_CONTEXT_ITEM);
    CLEO_RegisterOpcode(0x7132, IMGUI_BEGIN_POPUP_CONTEXT_WINDOW);
    CLEO_RegisterOpcode(0x7133, IMGUI_BEGIN_POPUP_CONTEXT_VOID);
    CLEO_RegisterOpcode(0x7134, IMGUI_IS_POPUP_OPEN);

    // Tab bars
    CLEO_RegisterOpcode(0x7135, IMGUI_BEGIN_TAB_BAR);
    CLEO_RegisterOpcode(0x7136, IMGUI_END_TAB_BAR);
    CLEO_RegisterOpcode(0x7137, IMGUI_BEGIN_TAB_ITEM);
    CLEO_RegisterOpcode(0x7138, IMGUI_END_TAB_ITEM);
    CLEO_RegisterOpcode(0x7139, IMGUI_TAB_ITEM_BUTTON);
    CLEO_RegisterOpcode(0x713A, IMGUI_SET_TAB_ITEM_CLOSED);

    // Tables
    CLEO_RegisterOpcode(0x713B, IMGUI_BEGIN_TABLE);
    CLEO_RegisterOpcode(0x713C, IMGUI_END_TABLE);
    CLEO_RegisterOpcode(0x713D, IMGUI_TABLE_NEXT_ROW);
    CLEO_RegisterOpcode(0x713E, IMGUI_TABLE_NEXT_COLUMN);
    CLEO_RegisterOpcode(0x713F, IMGUI_TABLE_SET_COLUMN_INDEX);
    CLEO_RegisterOpcode(0x7140, IMGUI_TABLE_SETUP_COLUMN);
    CLEO_RegisterOpcode(0x7141, IMGUI_TABLE_SETUP_SCROLL_FREEZE);
    CLEO_RegisterOpcode(0x7142, IMGUI_TABLE_HEADERS_ROW);
    CLEO_RegisterOpcode(0x7143, IMGUI_TABLE_HEADER);
    CLEO_RegisterOpcode(0x7144, IMGUI_TABLE_GET_COLUMN_COUNT);
    CLEO_RegisterOpcode(0x7145, IMGUI_TABLE_GET_COLUMN_INDEX);
    CLEO_RegisterOpcode(0x7146, IMGUI_TABLE_GET_ROW_INDEX);
    CLEO_RegisterOpcode(0x7147, IMGUI_TABLE_GET_COLUMN_NAME);
    CLEO_RegisterOpcode(0x7148, IMGUI_TABLE_GET_COLUMN_FLAGS);
    CLEO_RegisterOpcode(0x7149, IMGUI_TABLE_SET_COLUMN_ENABLED);
    CLEO_RegisterOpcode(0x714A, IMGUI_TABLE_SET_BG_COLOR);

    CLEO_RegisterOpcode(0x714B, IMGUI_LABEL_TEXT);
    CLEO_RegisterOpcode(0x714C, IMGUI_BEGIN_TOOLTIP);
    CLEO_RegisterOpcode(0x714D, IMGUI_END_TOOLTIP);
    CLEO_RegisterOpcode(0x714E, IMGUI_BEGIN_MENU);
    CLEO_RegisterOpcode(0x714F, IMGUI_END_MENU);
    CLEO_RegisterOpcode(0x7150, IMGUI_MENU_ITEM_EX);
    CLEO_RegisterOpcode(0x7152, IMGUI_BEGIN_LISTBOX);
    CLEO_RegisterOpcode(0x7153, IMGUI_END_LISTBOX);
    CLEO_RegisterOpcode(0x7154, IMGUI_LISTBOX);
    CLEO_RegisterOpcode(0x7155, IMGUI_PLOT_LINES);
    CLEO_RegisterOpcode(0x7156, IMGUI_PLOT_HISTOGRAM);
    CLEO_RegisterOpcode(0x7157, IMGUI_GET_TIME);
    CLEO_RegisterOpcode(0x7158, IMGUI_GET_FRAME_COUNT);

    CLEO_RegisterOpcode(0x7159, IMGUI_SET_IMAGE_UV);
    CLEO_RegisterOpcode(0x715A, IMGUI_SET_IMAGE_BORDER_COLOR);

    CLEO_RegisterOpcode(0x715B, IMGUI_BEGIN_A);
    CLEO_RegisterOpcode(0x715C, IMGUI_END);
    CLEO_RegisterOpcode(0x715D, IMGUI_CHECKBOX);
    CLEO_RegisterOpcode(0x715E, IMGUI_BUTTON);
    CLEO_RegisterOpcode(0x715F, IMGUI_CALC_TEXT_HEIGHT);
    CLEO_RegisterOpcode(0x7160, IMGUI_CALC_TEXT_WIDTH);
    CLEO_RegisterOpcode(0x7161, IMGUI_SET_NEXT_WINDOW_POS);
    CLEO_RegisterOpcode(0x7162, IMGUI_SET_WINDOW_POS);
    CLEO_RegisterOpcode(0x7163, IMGUI_GET_FONT_SIZE);
    CLEO_RegisterOpcode(0x7164, IMGUI_SET_NEXT_WINDOW_SIZE);
    CLEO_RegisterOpcode(0x7165, IMGUI_SET_WINDOW_SIZE);
    //CLEO_RegisterOpcode(0x7166, IMGUI_SHOW_DEMO_WINDOW);
    //CLEO_RegisterOpcode(0x7167, IMGUI_SHOW_STYLE_EDITOR);
    CLEO_RegisterOpcode(0x7168, IMGUI_TEXT);
    CLEO_RegisterOpcode(0x7169, IMGUI_TEXT_WRAPPED);
    CLEO_RegisterOpcode(0x716A, IMGUI_TEXT_DISABLED);
    CLEO_RegisterOpcode(0x716B, IMGUI_TEXT_COLORED);
    CLEO_RegisterOpcode(0x716C, IMGUI_COLUMNS);
    CLEO_RegisterOpcode(0x716D, IMGUI_NEXT_COLUMN);
    CLEO_RegisterOpcode(0x716E, IMGUI_SPACING);
    CLEO_RegisterOpcode(0x716F, IMGUI_DUMMY);
    CLEO_RegisterOpcode(0x7170, IMGUI_SAMELINE);
    CLEO_RegisterOpcode(0x7171, IMGUI_VSLIDER_INT);
    CLEO_RegisterOpcode(0x7172, IMGUI_VSLIDER_FLOAT);

    logger->Info("CLEO opcodes for ImGui registered.");
}