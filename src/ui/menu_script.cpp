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
    // One retail page holds kMenuRowsPerPage rows. Larger menus are emitted as
    // several MenuScreen tasks in this same resource so every control keeps the
    // rich row layout (label plus ListBox/ToggleBox/button). Page navigation is
    // baked into the script, so the retail manager performs the switch itself.
    const std::size_t pageCount = menuPageCount(config);
    const std::size_t rowsPerPage = kMenuRowsPerPage;
    const bool paged = pageCount > 1;
    const std::size_t activePage = std::min(config.pageIndex, pageCount - 1);
    const int frameY = menuLayoutY(config);
    const int frameHeight = menuRequiredHeight(config);
    const int firstScreenId = igi::menuPageScreenId(config, 0);
    // The screen that is actually emitted below: requesting any other id leaves the
    // retail manager with a null screen task and freezes its state machine.
    const int activeScreenId = igi::menuPageScreenId(config, activePage);
    const int lastScreenId = igi::menuPageScreenId(config, pageCount - 1);
    const int screenTop = frameY;
    // The MenuScreen rectangle is the visible background, so it must cover the
    // grown row frame instead of the smaller configured rect.
    const int screenBottom = frameY + frameHeight;

    const auto pageScreen = [&](std::size_t page) {
        std::ostringstream pageOut;
        const int pageScreenId = igi::menuPageScreenId(config, page);
        const std::size_t firstRow = page * rowsPerPage;
        const std::size_t lastRow = std::min(firstRow + rowsPerPage, config.items.size());
        pageOut
            << "Task_New(" << pageScreenId << ", \"MenuScreen\", \"IGI SDK\", \"\", " << config.x
            << ", " << config.y << ", " << config.x + config.width << ", "
            << config.y + config.height
            << ", \"IGI_SDK_Cancel() - MenuManager_PopScreen(FALSE)\", \"\", \"\", Task_New(-1, "
               "\"MenuFrame\", \"\", \"\", "
            << config.x << ", " << frameY << ", " << config.width << ", " << frameHeight
            << ", 1, 4, 4, FALSE";
        if (!config.title.empty()) {
            pageOut << ", Task_New(-1, \"MenuText\", \"title\", ";
            quoted(pageOut, paged ? std::string(config.title) + " (" + std::to_string(page + 1) +
                                        "/" + std::to_string(pageCount) + ")"
                                  : std::string(config.title));
            pageOut << ", \"font3.fnt\", 2, \"\", \"\")";
        }
        for (std::size_t index = firstRow; index < lastRow; ++index) {
            const auto &item = config.items[index];
            const auto widgetId = pageScreenId + 1 + static_cast<int>(index - firstRow);
            if (item.kind == ModMenuItemKind::List || item.kind == ModMenuItemKind::Input) {
                pageOut << ", Task_New(-1, \"MenuFrame\", ";
                quoted(pageOut, item.id);
                pageOut << ", \"\", " << config.x << ", " << frameY << ", " << config.width
                        << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
                quoted(pageOut, item.label);
                pageOut << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(" << widgetId
                        << ", \"ListBox\", \"\", \"slide\", \"font1.fnt\", 140, ";
                quoted(pageOut, "IGI_SDK_FillList(" + std::to_string(index) + ")");
                pageOut << ", ";
                quoted(pageOut, "IGI_SDK_GetList(" + std::to_string(index) + ")");
                pageOut << ", ";
                quoted(pageOut, "IGI_SDK_SetList(" + std::to_string(index) + ",this.nActiveID)");
                pageOut << ", ";
                quoted(pageOut, "IGI_SDK_SetList(" + std::to_string(index) + ",this.nActiveID)");
                pageOut << "))";
            } else if (item.kind == ModMenuItemKind::Toggle) {
                pageOut << ", Task_New(-1, \"MenuFrame\", ";
                quoted(pageOut, item.id);
                pageOut << ", \"\", " << config.x << ", " << frameY << ", " << config.width
                        << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"\", ";
                quoted(pageOut, item.label);
                pageOut << ", \"font3.fnt\", 2, \"\", \"\"), Task_New(" << widgetId
                        << ", \"ToggleBox\", \"\", ";
                quoted(pageOut, "IGI_SDK_GetToggle(" + std::to_string(index) + ")");
                pageOut << ", ";
                quoted(pageOut, "IGI_SDK_SetToggle(" + std::to_string(index) + ",this.isOn)");
                pageOut << "))";
            } else {
                pageOut << ", Task_New(-1, \"MenuText\", ";
                quoted(pageOut, item.id);
                pageOut << ", ";
                quoted(pageOut, item.label);
                pageOut << ", \"font3.fnt\", 2, ";
                if (item.kind == ModMenuItemKind::Action)
                    quoted(pageOut, "IGI_SDK_Action(" + std::to_string(index) + ")");
                else
                    quoted(pageOut, "");
                pageOut << ", \"\")";
            }
        }
        if (paged) {
            const int nextScreen =
                page + 1 < pageCount ? igi::menuPageScreenId(config, page + 1) : firstScreenId;
            const int prevScreen =
                page > 0 ? igi::menuPageScreenId(config, page - 1) : lastScreenId;
            pageOut << ", Task_New(-1, \"MenuFrame\", \"PageFrame\", \"\", " << config.x << ", "
                    << frameY << ", " << config.width
                    << ", 25, 8, 20, 4, TRUE, Task_New(-1, \"MenuText\", \"page_prev\", "
                       "\"< Prev Page\", \"font3.fnt\", 2, ";
            quoted(pageOut, "IGI_SDK_Page(-1)");
            pageOut << ", \"\"), Task_New(-1, \"MenuText\", \"page_next\", \"Next Page >\", "
                       "\"font3.fnt\", 2, ";
            quoted(pageOut, "IGI_SDK_Page(1)");
            pageOut << ", \"\"))";
        }
        pageOut << ", Task_New(-1, \"MenuFrame\", \"ControlsFrame\", \"\", " << config.x << ", "
                << frameY << ", " << config.width
                << ", 25, 8, 20, 4, TRUE, "
                   "Task_New(-1, \"MenuText\", \"restart\", \"Restart\", \"font3.fnt\", 2, "
                   "\"IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 12)\", \"menu_ok\"), "
                   "Task_New(-1, \"MenuText\", \"okay\", \"Okay\", \"font3.fnt\", 2, "
                   "\"IGI_SDK_Apply() - MenuManager_LeaveMenus(900, 9)\", \"menu_ok\"), "
                   "Task_New(-1, \"MenuText\", \"cancel\", \"Cancel\", \"font3.fnt\", 2, "
                   "\"IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 9)\", \"menu_cancel\"))";
        pageOut << "))";
        return pageOut.str();
    };

    if (standaloneManager) {
        out << "Task_DeclareParameters(\"MenuManager\", \"Current menuscreen\", \"Int32\", "
               "\"Resource path\", \"String256\", \"isOwnDisplay\", \"bool8\");\n"
               "Task_New(-1, \"MenuManager\", \"IGI SDK\", "
            << activeScreenId << ", \"LOCAL:menusystem/ingamemenu.res\", FALSE, "
            << pageScreen(activePage) << ");\n";
    } else {
        // Exactly one MenuScreen task exists per attachment, so closing the menu
        // never leaves an orphaned screen behind in the retail manager.
        out << pageScreen(activePage) << ";\n";
        out << "MenuManager_RequestScreen(" << activeScreenId << ", FALSE);\n";
    }
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
