// Proves the shipping Heaven control set produces a complete, compilable retail
// menu resource. The retail MenuFrame can only draw a handful of rows, so this
// test pins the command-list layout that keeps every option reachable.
#include "config/menu_config.hpp"
#include "ui/menu_commands.hpp"
#include "ui/menu_script.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

bool configureModMenu(igi::MenuConfig &config);

namespace igi {
// The layout test never attaches the retail screen, so the host transition hook
// is stubbed instead of pulling the platform menu host into this target.
bool prepareMenuForLevelChange() {
    return true;
}
} // namespace igi

namespace {
constexpr std::size_t kMinimumControls = 30;
constexpr std::size_t kMaximumControls = 90;

std::size_t countKind(const igi::MenuConfig &config, igi::ModMenuItemKind kind) {
    std::size_t count = 0;
    for (const auto &item : config.items) {
        if (item.kind == kind)
            ++count;
    }
    return count;
}
} // namespace

int main() {
    igi::MenuConfig config;
    assert(configureModMenu(config));

    // The shipping menu must cover a real controller set, not a token few.
    assert(config.items.size() >= kMinimumControls);
    assert(config.items.size() <= kMaximumControls);
    assert(countKind(config, igi::ModMenuItemKind::List) > 0);
    assert(countKind(config, igi::ModMenuItemKind::Action) > 0);

    // Every control must have an id, a label, and a category so no option can
    // ship unreachable or unlabelled in either menu frontend.
    std::vector<std::string> categories;
    for (const auto &item : config.items) {
        assert(!item.id.empty());
        assert(!item.label.empty());
        assert(!item.category.empty());
        if (item.kind == igi::ModMenuItemKind::List)
            assert(!item.values.empty());
        bool known = false;
        for (const auto &category : categories)
            known = known || category == item.category;
        if (!known)
            categories.push_back(item.category);
    }
    assert(categories.size() >= 6);
    assert(config.refreshLiveItems);

    // The retail frame cannot show this many rows, so the generator must pick
    // the scrollable command list.
    assert(igi::menuNeedsCommandList(config));
    const auto commands = igi::buildMenuCommands(config);
    assert(!commands.empty());
    // Every list value becomes its own runnable entry.
    std::size_t expected = 0;
    for (const auto &item : config.items) {
        switch (item.kind) {
        case igi::ModMenuItemKind::List:
            expected += item.values.size();
            break;
        case igi::ModMenuItemKind::Toggle:
            expected += 2;
            break;
        case igi::ModMenuItemKind::Action:
            expected += 1;
            break;
        case igi::ModMenuItemKind::Slider:
            expected += 1;
            break;
        case igi::ModMenuItemKind::Input:
            expected += igi::inputSamples(item).size();
            break;
        }
    }
    assert(commands.size() >= expected);
    for (const auto &command : commands) {
        assert(!command.label.empty());
        assert(command.itemIndex < config.items.size());
    }

    std::ostringstream script;
    assert(igi::writeIgiMenuScreenScript(script, config));
    if (const char *dump = std::getenv("IGI_DUMP_MENU_SCRIPT"))
        std::fprintf(stderr, "%s\n", script.str().c_str());
    (void)0;
    const auto text = script.str();
    // Every control keeps the rich row layout; large menus are paged instead of
    // collapsing into a one-line selector.
    assert(text.find("IGI_SDK_FillList(-1)") == std::string::npos);
    assert(text.find("ListBox") != std::string::npos);
    assert(text.find("IGI_SDK_Apply()") != std::string::npos);
    const std::size_t pages = igi::menuPageCount(config);
    assert(pages > 1);
    assert(text.find("IGI_SDK_FillList(") != std::string::npos);
    assert(text.find("ToggleBox") != std::string::npos ||
           text.find("IGI_SDK_Action(") != std::string::npos);
    assert(text.find("Next Page >") != std::string::npos);
    assert(text.find("Prev Page") != std::string::npos);
    // Only the current page is emitted: closed pages must never be left behind as
    // orphaned MenuScreen tasks (that crashed the retail manager).
    std::size_t screenCount = 0;
    for (std::size_t page = 0; page < pages; ++page) {
        const auto screenId = std::to_string(igi::menuPageScreenId(config, page));
        if (text.find("Task_New(" + screenId + ", \"MenuScreen\"") != std::string::npos)
            ++screenCount;
    }
    assert(screenCount == 1);
    const auto activeId = std::to_string(igi::menuPageScreenId(config, config.pageIndex));
    assert(text.find("Task_New(" + activeId + ", \"MenuScreen\"") != std::string::npos);
    // Page navigation is a registered SDK symbol, so the host owns the switch.
    assert(text.find("IGI_SDK_Page(1)") != std::string::npos);
    // The requested screen must be the page that was actually emitted: requesting
    // another page id leaves the retail manager with a null screen task.
    assert(text.find("MenuManager_RequestScreen(" + activeId + ", FALSE)") != std::string::npos);
    assert(text.find("IGI_SDK_Page(-1)") != std::string::npos);
    assert(igi::menuPageScreenId(config, 1) > config.screenId + static_cast<std::int32_t>(
                                                                  igi::kMenuRowsPerPage));
    assert(igi::menuScreenIdSpan(config) > config.items.size());
    // The visible background must cover the grown row frame.
    const int backgroundBottom = igi::menuLayoutY(config) + igi::menuRequiredHeight(config);
    assert(text.find(std::to_string(backgroundBottom)) != std::string::npos);
    // The frame grows with the content instead of clipping long rows.
    assert(igi::menuRequiredWidth(config) >= config.width);
    igi::MenuConfig wide = config;
    wide.items.front().label = std::string(80, 'W');
    assert(igi::menuRequiredWidth(wide) > igi::menuRequiredWidth(config));
    assert(igi::menuRequiredWidth(wide) <= 780);
    assert(igi::menuLayoutX(wide) >= 8);
    assert(igi::menuLayoutX(wide) <= igi::menuLayoutX(config));
    // The last page draws fewer rows, so its frame must be shorter instead of
    // reserving empty space for rows that do not exist.
    assert(igi::menuRowsOnPage(config) == igi::kMenuRowsPerPage);
    igi::MenuConfig last = config;
    last.pageIndex = igi::menuPageCount(config) - 1;
    assert(igi::menuRowsOnPage(last) <= igi::kMenuRowsPerPage);
    assert(igi::menuRequiredHeight(last) <= igi::menuRequiredHeight(config));
    assert(igi::menuRowsOnPage(last) > 0);
    // A page switch is a retail screen request, not host-side state.
    assert(text.find("MenuManager_RequestScreen(") != std::string::npos);

    // The generated resource must compile for the retail script runtime.
    std::vector<std::uint8_t> qvm;
    std::string error;
    // Page 2 must request its own screen id, not page 1's.
    igi::MenuConfig second = config;
    second.pageIndex = 1;
    std::ostringstream secondScript;
    assert(igi::writeIgiMenuScreenScript(secondScript, second));
    const auto pageTwo = igi::menuPageScreenId(second, 1);
    assert(secondScript.str().find("Task_New(" + std::to_string(pageTwo) + ", \"MenuScreen\"") !=
           std::string::npos);
    assert(secondScript.str().find("MenuManager_RequestScreen(" + std::to_string(pageTwo) + ", FALSE)") !=
           std::string::npos);
    assert(secondScript.str().find("IGI_SDK_Page(1)") != std::string::npos);
    std::vector<std::uint8_t> pageQvm;
    std::string pageError;
    assert(igi::compileIgiMenuScreenScript(second, pageQvm, pageError));

    assert(igi::compileIgiMenuScreenScript(config, qvm, error));
    assert(!qvm.empty());
    assert(error.empty());

    std::printf("PASS Heaven layout: %zu controls, %zu categories, %zu command entries, "
                "QVM %zu bytes\n",
                config.items.size(), categories.size(), commands.size(), qvm.size());
    return 0;
}
