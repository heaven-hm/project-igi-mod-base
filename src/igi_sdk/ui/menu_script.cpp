#include "igi_sdk/ui/menu_script.hpp"
#include "igi_sdk/config/menu_config.hpp"
#include "qsc_lexer.h"
#include "qsc_parser.h"
#include "qvm_compiler.h"

#include <ostream>
#include <sstream>
#include <string_view>

namespace {
void quoted(std::ostream& out, std::string_view value) {
    out << '"';
    for (const char c : value) {
        if (c == '\\' || c == '"') out << '\\';
        if (c == '\n') out << "\\n";
        else if (c != '\r') out << c;
    }
    out << '"';
}
}

namespace igi {
bool writeIgi1MenuScript(std::ostream& out, const MenuConfig& config) {
    if (config.items.empty() || config.width <= 0 || config.height <= 0) return false;
    for (const auto& item : config.items) {
        if (item.kind != ModMenuItemKind::List) continue;
        // IGI-RE ListBox has 64 entries with 40-byte text buffers.
        if (item.values.empty() || item.values.size() > 64) return false;
        for (const auto& value : item.values)
            if (value.size() >= 40 || value.find('\0') != std::string::npos) return false;
    }
    out << "Task_DeclareParameters(\"MenuManager\", \"Current menuscreen\", \"Int32\", \"Resource path\", \"String256\", \"isOwnDisplay\", \"bool8\");\n"
           "Task_DeclareParameters(\"MenuScreen\", \"Background\", \"String32\", \"Frame x0\", \"Int32\", \"Frame y0\", \"Int32\", \"Frame x1\", \"Int32\", \"Frame y1\", \"Int32\", \"On Press Escape\", \"VarString\", \"On Init Data\", \"VarString\", \"On Change Data\", \"VarString\");\n"
           "Task_DeclareParameters(\"MenuFrame\", \"Sprite Resource\", \"String32\", \"X-Pos\", \"Int32\", \"Y-Pos\", \"Int32\", \"Width\", \"Int32\", \"Height\", \"Int32\", \"LineUpMode\", \"Int32\", \"X-Spacing\", \"Int16\", \"Y-Spacing\", \"Int16\", \"Is Selectable\", \"bool8\");\n"
           "Task_DeclareParameters(\"MenuText\", \"Text Resource\", \"String32\", \"Font Resource\", \"String32\", \"ColourIndex\", \"Int32\", \"On Click Script\", \"VarString\", \"Click Sound Resource\", \"String32\");\n"
           "Task_DeclareParameters(\"ToggleBox\", \"Get Data Script\", \"VarString\", \"Set Data Script\", \"VarString\");\n"
           "Task_DeclareParameters(\"ListBox\", \"Sprite Resource\", \"String32\", \"Font Resource\", \"String32\", \"Width\", \"Int32\", \"Collect Items Script\", \"VarString\", \"Get Data Script\", \"VarString\", \"Set Data Script\", \"VarString\", \"Modify Data Script\", \"VarString\");\n";
    out << "Task_New(-1, \"MenuManager\", \"IGI SDK\", " << config.screenId
        << ", \"LOCAL:menusystem/ingamemenu.res\", FALSE, Task_New(" << config.screenId
        << ", \"MenuScreen\", \"IGI SDK\", \"\", " << config.x << ", " << config.y << ", "
        << config.x + config.width << ", " << config.y + config.height
        << ", \"MenuManager_LeaveMenus(0, MENUMANAGER_IDLE)\", \"\", \"\", Task_New(-1, \"MenuFrame\", \"\", \"\", "
        << config.x << ", " << config.y << ", " << config.width << ", " << config.height
        << ", 1, 4, 4, FALSE";
    for (const auto& item : config.items) {
        if (item.kind == ModMenuItemKind::List) {
            // Item IDs in expressions are indices, avoiding script injection through config strings.
            const auto index = &item - config.items.data();
            const auto fill = "IGI_SDK_FillList(" + std::to_string(index) + ")";
            const auto select = "IGI_SDK_GetList(" + std::to_string(index) + ")";
            const auto change = "IGI_SDK_SetList(" + std::to_string(index) + ",this.nActiveID)";
            out << ", Task_New(-1, \"MenuFrame\", "; quoted(out, item.id);
            out << ", \"\", " << config.x << ", " << config.y << ", " << config.width
                << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
            quoted(out, item.label);
            out << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(-1, \"ListBox\", \"\", \"slide\", \"font1.fnt\", 140, ";
            quoted(out, fill); out << ", "; quoted(out, select); out << ", \"\", ";
            // IGI evaluates the last (action) expression when the active ID changes.
            quoted(out, change); out << "))";
            continue;
        }
        if (item.kind == ModMenuItemKind::Toggle) {
            out << ", Task_New(-1, \"MenuFrame\", "; quoted(out, item.id);
            out << ", \"\", " << config.x << ", " << config.y << ", " << config.width
                << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
            quoted(out, item.label);
            out << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(-1, \"ToggleBox\", \"\", ";
            quoted(out, item.defaultToggle ? "TRUE" : "FALSE");
            out << ", "; quoted(out, item.nativeAction); out << "))";
            continue;
        }
        out << ", Task_New(-1, \"MenuText\", "; quoted(out, item.id); out << ", ";
        quoted(out, item.label); out << ", \"font3.fnt\", 2, ";
        // Non-action widgets require separate value adapters, which are not yet implemented.
        quoted(out, item.kind == ModMenuItemKind::Action ? item.nativeAction : "");
        out << ", \"\")";
    }
    out << ")));\n";
    return static_cast<bool>(out);
}

bool compileIgi1MenuScript(const MenuConfig& config, std::vector<std::uint8_t>& qvm, std::string& error) {
    qvm.clear();
    error.clear();
    std::ostringstream script;
    if (!writeIgi1MenuScript(script, config)) { error = "invalid menu configuration"; return false; }
    auto lexed = qsc::Lex(script.str());
    if (!lexed.ok) { error = lexed.error; return false; }
    auto parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) { error = parsed.error; return false; }
    auto compiled = qvm::Compile(*parsed.program);
    if (!compiled.ok) { error = compiled.error; return false; }
    qvm = std::move(compiled.binary);
    return true;
}
}
