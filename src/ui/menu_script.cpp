#include "ui/menu_script.hpp"
#include "config/menu_config.hpp"
#include "ui/menu_commands.hpp"
#include "qsc_lexer.h"
#include "qsc_parser.h"
#include "qvm_compiler.h"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <string_view>

namespace igi {
namespace {
void quoted(std::ostream &out, std::string_view value) {
    out << '"';
    for (const char c : value) {
        if (c == '\\' || c == '"')
            out << '\\';
        if (c == '\n')
            out << "\\n";
        else if (c != '\r')
            out << c;
    }
    out << '"';
}
} // namespace

bool writeMenuScript(std::ostream &out, const MenuConfig &config, bool standaloneManager) {
    if (config.items.empty() || config.width <= 0 || config.height <= 0)
        return false;
    if (!validateMenuItemConfig(config))
        return false;
    out << "Task_DeclareParameters(\"MenuScreen\", \"Background\", \"String32\", \"Frame x0\", "
           "\"Int32\", \"Frame y0\", \"Int32\", \"Frame x1\", \"Int32\", \"Frame y1\", \"Int32\", "
           "\"On Press Escape\", \"VarString\", \"On Init Data\", \"VarString\", \"On Change "
           "Data\", \"VarString\");\n"
           "Task_DeclareParameters(\"MenuFrame\", \"Sprite Resource\", \"String32\", \"X-Pos\", "
           "\"Int32\", \"Y-Pos\", \"Int32\", \"Width\", \"Int32\", \"Height\", \"Int32\", "
           "\"LineUpMode\", \"Int32\", \"X-Spacing\", \"Int16\", \"Y-Spacing\", \"Int16\", \"Is "
           "Selectable\", \"bool8\");\n"
           "Task_DeclareParameters(\"MenuText\", \"Text Resource\", \"String32\", \"Font "
           "Resource\", \"String32\", \"ColourIndex\", \"Int32\", \"On Click Script\", "
           "\"VarString\", \"Click Sound Resource\", \"String32\");\n"
           "Task_DeclareParameters(\"ToggleBox\", \"Get Data Script\", \"VarString\", \"Set Data "
           "Script\", \"VarString\");\n"
           "Task_DeclareParameters(\"ListBox\", \"Sprite Resource\", \"String32\", \"Font "
           "Resource\", \"String32\", \"Width\", \"Int32\", \"Collect Items Script\", "
           "\"VarString\", \"Get Data Script\", \"VarString\", \"Set Data Script\", \"VarString\", "
           "\"Modify Data Script\", \"VarString\");\n";
    if (standaloneManager)
        out << "Task_DeclareParameters(\"MenuManager\", \"Current menuscreen\", \"Int32\", "
               "\"Resource path\", \"String256\", \"isOwnDisplay\", \"bool8\");\n"
               "Task_New(-1, \"MenuManager\", \"IGI SDK\", "
            << config.screenId << ", \"LOCAL:menusystem/ingamemenu.res\", FALSE, ";
    const bool commandList = menuNeedsCommandList(config);
    // One row per control needs more height than the configured frame for most
    // menus, so the frame is grown upwards to keep every control on screen.
    const int frameY = commandList ? config.y : menuLayoutY(config);
    const int frameHeight = commandList ? config.height : menuRequiredHeight(config);
    out << "Task_New(" << config.screenId << ", \"MenuScreen\", \"IGI SDK\", \"\", " << config.x
        << ", " << config.y << ", " << config.x + config.width << ", " << config.y + config.height
        << ", \"IGI_SDK_Cancel() - MenuManager_PopScreen(FALSE)\", \"\", \"\", Task_New(-1, "
           "\"MenuFrame\", \"\", \"\", "
        << config.x << ", " << frameY << ", " << config.width << ", " << frameHeight
        << ", 1, 4, 4, FALSE";
    if (!config.title.empty()) {
        out << ", Task_New(-1, \"MenuText\", \"title\", ";
        quoted(out, config.title);
        out << ", \"font3.fnt\", 2, \"\", \"\")";
    }
    // A control set that no longer fits the retail frame is rendered as one
    // scrollable ListBox over the flattened command entries. Every configured
    // option stays reachable instead of being drawn past the frame edge.
    if (commandList) {
        out << ", Task_New(-1, \"MenuFrame\", \"commands\", \"\", " << config.x << ", " << config.y
            << ", " << config.width << ", " << config.height
            << ", 8, 20, 4, TRUE, "
               "Task_New(-1, \"MenuText\", \"\", \"Controls\", \"font3.fnt\", 2, \"\", \"\"), "
               "Task_New("
            << (config.screenId >= 0 ? config.screenId + 1 : -1)
            << ", \"ListBox\", \"\", \"slide\", \"font1.fnt\", " << std::max(config.width - 20, 40)
            << ", ";
        quoted(out, "IGI_SDK_FillList(-1)");
        out << ", ";
        quoted(out, "IGI_SDK_GetList(-1)");
        out << ", ";
        quoted(out, "IGI_SDK_SetList(-1,this.nActiveID)");
        out << ", ";
        quoted(out, "IGI_SDK_SetList(-1,this.nActiveID)");
        out << "))";
    }
    for (const auto &item : config.items) {
        if (commandList)
            break;
        const auto index = &item - config.items.data();
        const auto widgetId =
            config.screenId >= 0 ? (config.screenId + 1 + static_cast<int>(index)) : -1;
        if (item.kind == ModMenuItemKind::List || item.kind == ModMenuItemKind::Input) {
            out << ", Task_New(-1, \"MenuFrame\", ";
            quoted(out, item.id);
            out << ", \"\", " << config.x << ", " << frameY << ", " << config.width
                << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
            quoted(out, item.label);
            out << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(" << widgetId
                << ", \"ListBox\", \"\", \"slide\", \"font1.fnt\", 140, ";
            quoted(out, "IGI_SDK_FillList(" + std::to_string(index) + ")");
            out << ", ";
            quoted(out, "IGI_SDK_GetList(" + std::to_string(index) + ")");
            out << ", ";
            quoted(out, "IGI_SDK_SetList(" + std::to_string(index) + ",this.nActiveID)");
            out << ", ";
            quoted(out, "IGI_SDK_SetList(" + std::to_string(index) + ",this.nActiveID)");
            out << "))";
        } else if (item.kind == ModMenuItemKind::Toggle) {
            out << ", Task_New(-1, \"MenuFrame\", ";
            quoted(out, item.id);
            out << ", \"\", " << config.x << ", " << config.y << ", " << config.width
                << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
            quoted(out, item.label);
            out << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(" << widgetId
                << ", \"ToggleBox\", \"\", ";
            quoted(out, "IGI_SDK_GetToggle(" + std::to_string(index) + ")");
            out << ", ";
            quoted(out, "IGI_SDK_SetToggle(" + std::to_string(index) + ",this.isOn)");
            out << "))";
        } else {
            out << ", Task_New(-1, \"MenuText\", ";
            quoted(out, item.id);
            out << ", ";
            quoted(out, item.label);
            out << ", \"font3.fnt\", 2, ";
            if (item.kind == ModMenuItemKind::Action)
                quoted(out, "IGI_SDK_Action(" + std::to_string(index) + ")");
            else
                quoted(out, "");
            out << ", \"\")";
        }
    }
    out << ", Task_New(-1, \"MenuFrame\", \"ControlsFrame\", \"\", " << config.x << ", " << config.y
        << ", " << config.width << ", 25, 8, 20, 4, TRUE, "
        << "Task_New(-1, \"MenuText\", \"restart\", \"Restart\", \"font3.fnt\", 2, "
           "\"IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 12)\", \"menu_ok\"), "
        << "Task_New(-1, \"MenuText\", \"okay\", \"Okay\", \"font3.fnt\", 2, \"IGI_SDK_Apply() - "
           "MenuManager_LeaveMenus(900, 9)\", \"menu_ok\"), "
        << "Task_New(-1, \"MenuText\", \"cancel\", \"Cancel\", \"font3.fnt\", 2, "
           "\"IGI_SDK_Cancel() - MenuManager_PopScreen(FALSE)\", \"menu_cancel\"))";
    out << (standaloneManager ? ")));\n" : "));\n");
    if (!standaloneManager)
        out << "MenuManager_RequestScreen(" << config.screenId << ", FALSE);\n";
    return static_cast<bool>(out);
}

bool compileMenuScript(const MenuConfig &config, std::vector<std::uint8_t> &qvm, std::string &error,
                       bool manager) {
    qvm.clear();
    error.clear();
    std::ostringstream script;
    if (!writeMenuScript(script, config, manager)) {
        error = "invalid menu configuration";
        return false;
    }
    auto lexed = qsc::Lex(script.str());
    if (!lexed.ok) {
        error = lexed.error;
        return false;
    }
    auto parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) {
        error = parsed.error;
        return false;
    }
    auto compiled = qvm::Compile(*parsed.program);
    if (!compiled.ok) {
        error = compiled.error;
        return false;
    }
    qvm = std::move(compiled.binary);
    return true;
}
} // namespace igi

namespace igi {
bool writeIgiMenuScript(std::ostream &out, const MenuConfig &config) {
    return writeMenuScript(out, config, true);
}

bool writeIgiMenuScreenScript(std::ostream &out, const MenuConfig &config) {
    return writeMenuScript(out, config, false);
}

bool compileIgiMenuScript(const MenuConfig &config, std::vector<std::uint8_t> &qvm,
                          std::string &error) {
    return compileMenuScript(config, qvm, error, true);
}
bool compileIgiMenuScreenScript(const MenuConfig &config, std::vector<std::uint8_t> &qvm,
                                std::string &error) {
    return compileMenuScript(config, qvm, error, false);
}
bool compileIgiMenuCommandScript(std::string_view command, std::vector<std::uint8_t> &qvm,
                                 std::string &error) {
    qvm.clear();
    error.clear();
    auto lexed = qsc::Lex(std::string(command));
    if (!lexed.ok) {
        error = lexed.error;
        return false;
    }
    auto parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) {
        error = parsed.error;
        return false;
    }
    auto compiled = qvm::Compile(*parsed.program);
    if (!compiled.ok) {
        error = compiled.error;
        return false;
    }
    qvm = std::move(compiled.binary);
    return true;
}
} // namespace igi
