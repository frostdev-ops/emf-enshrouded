// Mod windows (API v24): the copy the core keeps of a mod's window description, and the checks on it.
#include <windows.h>

#include <doctest.h>

#include <string>
#include <vector>

#include "core/overlay/window_model.h"
#include "emf/emf_api.h"

using namespace emf::core;

namespace {
void cb(EmfWorld*, const EmfUiEvent*, void*) {}

struct Def {
    std::vector<EmfUiWidget> widgets;
    EmfWindowDef def{};
    Def() {
        widgets.resize(3);
        widgets[0] = {};
        widgets[0].type = EMF_UI_HEADING;
        widgets[0].text = "Active 1/3";
        widgets[1] = {};
        widgets[1].type = EMF_UI_BUTTON;
        widgets[1].key = "call";
        widgets[1].text = "Call";
        widgets[1].tooltip = "Call the beast";
        widgets[2] = {};
        widgets[2].type = EMF_UI_SLOT;
        widgets[2].flags = EMF_UI_SAME_LINE | EMF_UI_LOCKED;
        widgets[2].key = "active.3";
        widgets[2].text = "Level 15";
        widgets[2].count = 2;
        def.size = sizeof def;
        def.id = "beastmaster.stable";
        def.title = "Beasts";
        def.widgets = widgets.data();
        def.count = 3;
        def.widget_size = sizeof(EmfUiWidget);
        def.close_key = 'B';
        def.place = EMF_UI_PLACE_RIGHT;
        def.width = 30.0f;
        def.world_kind = EMF_WORLD_CLIENT;
        def.cb = cb;
        def.user = this;
    }
    std::string refused(bool update = false) {
        WindowRequest r;
        std::string why;
        if (copy_window_def(&def, update, &r, &why)) return "";
        return why.empty() ? "(no reason)" : why;
    }
};
}  // namespace

TEST_CASE("windows: a good description is copied with every string; fields past widget_size get defaults") {
    Def d;
    WindowRequest r;
    std::string why;
    REQUIRE(copy_window_def(&d.def, false, &r, &why));
    CHECK(r.id == "beastmaster.stable");
    CHECK(r.title == "Beasts");
    REQUIRE(r.widgets.size() == 3u);
    CHECK(r.widgets[0].type == EMF_UI_HEADING);
    CHECK(r.widgets[0].text == "Active 1/3");
    CHECK(r.widgets[0].key.empty());
    CHECK(r.widgets[1].key == "call");
    CHECK(r.widgets[1].tooltip == "Call the beast");
    CHECK(r.widgets[2].flags == (EMF_UI_SAME_LINE | EMF_UI_LOCKED));
    CHECK(r.widgets[2].count == 2u);
    CHECK(r.close_vk == 'B');
    CHECK(r.place == EMF_UI_PLACE_RIGHT);
    CHECK(r.width == 30.0f);
    CHECK(r.world_kind == EMF_WORLD_CLIENT);
    CHECK(r.cb == cb);
    CHECK(r.user == &d);

    // an older or smaller widget struct: the core steps by widget_size and leaves the fields past it at 0
    struct Small {
        uint32_t type, flags;
        const char *key, *text, *tooltip;
        uint32_t count, value, rgba;
    };
    Small older[2] = {{EMF_UI_TEXT, 0, nullptr, "one", nullptr, 0, 0, 0x112233ffu},
                      {EMF_UI_BAR, 0, nullptr, "two", nullptr, 0, 1500, 0}};
    d.def.widgets = reinterpret_cast<const EmfUiWidget*>(older);
    d.def.widget_size = sizeof(Small);
    d.def.count = 2;
    REQUIRE(copy_window_def(&d.def, false, &r, &why));
    REQUIRE(r.widgets.size() == 2u);
    CHECK(r.widgets[0].text == "one");
    CHECK(r.widgets[0].rgba == 0x112233ffu);
    CHECK(r.widgets[1].text == "two");
    CHECK(r.widgets[1].value == 1000u);  // a bar's value clamps to 1000
}

TEST_CASE("windows: defaults; NULL texts are empty; an update needs no callback") {
    Def d;
    d.def.title = nullptr;
    d.def.width = 0;
    d.def.close_key = 0;
    d.widgets[0].text = nullptr;
    WindowRequest r;
    std::string why;
    REQUIRE(copy_window_def(&d.def, false, &r, &why));
    CHECK(r.title.empty());
    CHECK(r.width == kDefaultWindowWidth);
    CHECK(r.close_vk == 0);
    CHECK(r.widgets[0].text.empty());
    d.def.cb = nullptr;
    CHECK_FALSE(d.refused(false).empty());  // open needs a callback
    CHECK(d.refused(true).empty());         // an update keeps the first one
    d.def.count = 0;
    d.def.widgets = nullptr;
    CHECK(d.refused(true).empty());  // an empty window is allowed
}

TEST_CASE("windows: a bad description is refused with a reason naming what is wrong") {
    CHECK(Def().refused().empty());
    WindowRequest r;
    std::string why;
    CHECK_FALSE(copy_window_def(nullptr, false, &r, &why));
    {
        Def d;
        d.def.size = 16;
        CHECK(d.refused().find("size") != std::string::npos);
    }
    {
        Def d;
        d.def.id = "beastmaster stable";
        CHECK(d.refused().find("id") != std::string::npos);
        d.def.id = "";
        CHECK(d.refused().find("id") != std::string::npos);
        d.def.id = nullptr;
        CHECK(d.refused().find("id") != std::string::npos);
        const std::string long_id(49, 'a');
        d.def.id = long_id.c_str();
        CHECK(d.refused().find("id") != std::string::npos);
    }
    {
        Def d;
        const std::string title(65, 't');
        d.def.title = title.c_str();
        CHECK(d.refused().find("title") != std::string::npos);
    }
    {
        Def d;
        std::vector<EmfUiWidget> many(257);
        for (auto& w : many) w = {};
        d.def.widgets = many.data();
        d.def.count = 257;
        CHECK(d.refused().find("256") != std::string::npos);
        d.def.widgets = nullptr;
        d.def.count = 2;
        CHECK(d.refused().find("widgets") != std::string::npos);
    }
    {
        Def d;
        d.widgets[1].type = 99;
        CHECK(d.refused().find("type") != std::string::npos);
    }
    {
        Def d;
        const std::string key(49, 'k');
        d.widgets[1].key = key.c_str();
        CHECK(d.refused().find("key") != std::string::npos);
    }
    {
        Def d;
        const std::string text(257, 'x');
        d.widgets[1].text = text.c_str();
        CHECK(d.refused().find("text") != std::string::npos);
        const std::string tip(513, 'x');
        d.widgets[1].text = "ok";
        d.widgets[1].tooltip = tip.c_str();
        CHECK(d.refused().find("tooltip") != std::string::npos);
    }
    {
        Def d;
        d.def.widget_size = 16;
        CHECK(d.refused().find("widget_size") != std::string::npos);
    }
    {
        Def d;
        d.def.world_kind = EMF_WORLD_PREDICTION;
        CHECK(d.refused().find("world_kind") != std::string::npos);
    }
    {
        Def d;
        d.def.place = 7;
        CHECK(d.refused().find("place") != std::string::npos);
        d.def.place = EMF_UI_PLACE_CENTRE;
        d.def.width = -3;
        CHECK(d.refused().find("width") != std::string::npos);
    }
    {
        Def d;
        d.def.close_key = VK_LBUTTON;  // as for a wheel's hold_key: keyboard keys only
        CHECK(d.refused().find("close_key") != std::string::npos);
        d.def.close_key = VK_ESCAPE;  // Esc closes anyway
        CHECK(d.refused().empty());
    }
}

TEST_CASE("windows: describe_window lists the window and each widget with its kind, key, flags and text") {
    WindowRequest r;
    r.id = "beastmaster.stable";
    r.title = "Beasts";
    r.world_kind = EMF_WORLD_CLIENT;
    WindowWidget a;
    a.type = EMF_UI_HEADING;
    a.text = "Active 1/3";
    WindowWidget b;
    b.type = EMF_UI_SLOT;
    b.key = "active.3";
    b.text = "Level 15";
    b.flags = EMF_UI_SAME_LINE | EMF_UI_LOCKED;
    b.count = 2;
    WindowWidget c;
    c.type = EMF_UI_BAR;
    c.text = "Wolf 2";
    c.value = 800;
    r.widgets = {a, b, c};
    const auto lines = describe_window(r);
    REQUIRE(lines.size() == 4u);
    CHECK(lines[0] == "window beastmaster.stable \"Beasts\": 3 widgets, events on the client world");
    CHECK(lines[1] == "  0 heading \"Active 1/3\"");
    CHECK(lines[2] == "  1 slot active.3 \"Level 15\" count 2 [same_line locked]");
    CHECK(lines[3] == "  2 bar \"Wolf 2\" 800/1000");
}
