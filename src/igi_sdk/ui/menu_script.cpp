#include "igi_sdk/ui/menu_script.hpp"
#include "igi_sdk/config/menu_config.hpp"
#include "qsc_lexer.h"
#include "qsc_parser.h"
#include "qvm_compiler.h"

#include <ostream>
#include <sstream>
#include <string_view>

namespace igi { namespace {
void quoted(std::ostream& out, std::string_view value) {
    out << '"';
    for (const char c : value) {
        if (c == '\\' || c == '"') out << '\\';
        if (c == '\n') out << "\\n";
        else if (c != '\r') out << c;
    }
    out << '"';
}}

bool writeMenuScript(std::ostream& out, const MenuConfig& config, bool standaloneManager) {
    if (config.items.empty() || config.width <= 0 || config.height <= 0) return false;
    for (const auto& item : config.items) {
        if (item.kind != ModMenuItemKind::List) continue;
        if (item.values.empty() || item.values.size() > 64) return false;
        for (const auto& value : item.values)
            if (value.size() >= 40 || value.find('\0') != std::string::npos) return false;
    }
    out << "Task_DeclareParameters(\"MenuScreen\", \"Background\", \"String32\", \"Frame x0\", \"Int32\", \"Frame y0\", \"Int32\", \"Frame x1\", \"Int32\", \"Frame y1\", \"Int32\", \"On Press Escape\", \"VarString\", \"On Init Data\", \"VarString\", \"On Change Data\", \"VarString\");\n"
           "Task_DeclareParameters(\"MenuFrame\", \"Sprite Resource\", \"String32\", \"X-Pos\", \"Int32\", \"Y-Pos\", \"Int32\", \"Width\", \"Int32\", \"Height\", \"Int32\", \"LineUpMode\", \"Int32\", \"X-Spacing\", \"Int16\", \"Y-Spacing\", \"Int16\", \"Is Selectable\", \"bool8\");\n"
           "Task_DeclareParameters(\"MenuText\", \"Text Resource\", \"String32\", \"Font Resource\", \"String32\", \"ColourIndex\", \"Int32\", \"On Click Script\", \"VarString\", \"Click Sound Resource\", \"String32\");\n"
           "Task_DeclareParameters(\"ToggleBox\", \"Get Data Script\", \"VarString\", \"Set Data Script\", \"VarString\");\n"
           "Task_DeclareParameters(\"ListBox\", \"Sprite Resource\", \"String32\", \"Font Resource\", \"String32\", \"Width\", \"Int32\", \"Collect Items Script\", \"VarString\", \"Get Data Script\", \"VarString\", \"Set Data Script\", \"VarString\", \"Modify Data Script\", \"VarString\");\n";
    if (standaloneManager)
        out << "Task_DeclareParameters(\"MenuManager\", \"Current menuscreen\", \"Int32\", \"Resource path\", \"String256\", \"isOwnDisplay\", \"bool8\");\n"
             "Task_New(-1, \"MenuManager\", \"IGI SDK\", " << config.screenId
            << ", \"LOCAL:menusystem/ingamemenu.res\", FALSE, ";
    out << "Task_New(" << config.screenId << ", \"MenuScreen\", \"IGI SDK\", \"\", "
        << config.x << ", " << config.y << ", " << config.x + config.width << ", " << config.y + config.height
        << ", \"IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 9)\", \"\", \"\", Task_New(-1, \"MenuFrame\", \"\", \"\", "
        << config.x << ", " << config.y << ", " << config.width << ", " << config.height << ", 1, 4, 4, FALSE";
    for (const auto& item : config.items) {
        const auto index = &item - config.items.data();
        const auto widgetId = config.screenId >= 0 ? (config.screenId + 1 + static_cast<int>(index)) : -1;
        if (item.kind == ModMenuItemKind::List) {
            out << ", Task_New(-1, \"MenuFrame\", "; quoted(out, item.id);
            out << ", \"\", " << config.x << ", " << config.y << ", " << config.width
                << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
            quoted(out, item.label);
            out << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(" << widgetId << ", \"ListBox\", \"\", \"slide\", \"font1.fnt\", 140, ";
            quoted(out, "IGI_SDK_FillList(" + std::to_string(index) + ")"); out << ", ";
            quoted(out, "IGI_SDK_GetList(" + std::to_string(index) + ")"); out << ", ";
            quoted(out, "IGI_SDK_SetList(" + std::to_string(index) + ",this.nActiveID)"); out << ", ";
            quoted(out, "IGI_SDK_SetList(" + std::to_string(index) + ",this.nActiveID)"); out << "))";
        } else if (item.kind == ModMenuItemKind::Toggle) {
            out << ", Task_New(-1, \"MenuFrame\", "; quoted(out, item.id);
            out << ", \"\", " << config.x << ", " << config.y << ", " << config.width
                << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
            quoted(out, item.label);
            out << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(" << widgetId << ", \"ToggleBox\", \"\", ";
            quoted(out, "IGI_SDK_GetToggle(" + std::to_string(index) + ")");
            out << ", ";
            quoted(out, "IGI_SDK_SetToggle(" + std::to_string(index) + ",this.isOn)");
            out << "))";
        } else {
            out << ", Task_New(-1, \"MenuText\", "; quoted(out, item.id); out << ", "; quoted(out, item.label);
            out << ", \"font3.fnt\", 2, ";
            if (item.kind == ModMenuItemKind::Action) quoted(out, "IGI_SDK_Action(" + std::to_string(index) + ")");
            else quoted(out, "");
            out << ", \"\")";
        }
    }
    out << ", Task_New(-1, \"MenuFrame\", \"ControlsFrame\", \"\", " << config.x << ", " << config.y << ", " << config.width
        << ", 25, 8, 20, 4, TRUE, "
        << "Task_New(-1, \"MenuText\", \"restart\", \"Restart\", \"font3.fnt\", 2, \"IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 12)\", \"menu_ok\"), "
        << "Task_New(-1, \"MenuText\", \"okay\", \"Okay\", \"font3.fnt\", 2, \"IGI_SDK_Apply() - MenuManager_LeaveMenus(900, 9)\", \"menu_ok\"), "
        << "Task_New(-1, \"MenuText\", \"cancel\", \"Cancel\", \"font3.fnt\", 2, \"IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 9)\", \"menu_cancel\"))";
    out << (standaloneManager ? ")));\n" : "));\n");
    if (!standaloneManager)
        out << "MenuManager_RequestScreen(" << config.screenId << ", FALSE);\n";
    return static_cast<bool>(out);
}

bool compileMenuScript(const MenuConfig& config, std::vector<std::uint8_t>& qvm, std::string& error, bool manager) {
    qvm.clear(); error.clear(); std::ostringstream script;
    if (!writeMenuScript(script, config, manager)) { error = "invalid menu configuration"; return false; }
    auto lexed = qsc::Lex(script.str());
    if (!lexed.ok) { error = lexed.error; return false; }
    auto parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) { error = parsed.error; return false; }
    auto compiled = qvm::Compile(*parsed.program);
    if (!compiled.ok) { error = compiled.error; return false; }
    qvm = std::move(compiled.binary); return true;
}
}

namespace igi {
bool writeIgi1MenuScript(std::ostream& out, const MenuConfig& config) {
    return writeMenuScript(out, config, true);
}

bool writeIgi1MenuScreenScript(std::ostream& out, const MenuConfig& config) { return writeMenuScript(out, config, false); }

bool compileIgi1MenuScript(const MenuConfig& config, std::vector<std::uint8_t>& qvm, std::string& error) {
    return compileMenuScript(config, qvm, error, true);
}
bool compileIgi1MenuScreenScript(const MenuConfig& config, std::vector<std::uint8_t>& qvm, std::string& error) { return compileMenuScript(config, qvm, error, false); }
bool compileIgi1MenuCommandScript(std::string_view command, std::vector<std::uint8_t>& qvm, std::string& error) {
    qvm.clear(); error.clear();
    auto lexed = qsc::Lex(std::string(command));
    if (!lexed.ok) { error = lexed.error; return false; }
    auto parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) { error = parsed.error; return false; }
    auto compiled = qvm::Compile(*parsed.program);
    if (!compiled.ok) { error = compiled.error; return false; }
    qvm = std::move(compiled.binary); return true;
}
}
