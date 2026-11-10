// The HUD editor's bar colours: the player's colours as text, and EMF's copy of the game's colour block for one bar
// (docs/research/hud_colours_2026-11-10.md in the private repo: keen::FbUiAnimatedHealthBarColors).
#include <doctest.h>

#include <cstring>

#include "core/hud_editor/colours.h"

using namespace emf::core::hud;

namespace {

Colour col(const char* text) {
    const ColourText t = parse_colour(text);
    REQUIRE(t.colour.has_value());
    return *t.colour;
}

// Six distinct game colours, each with its own alpha and intensity.
StyleBlock game_block() {
    StyleBlock b{};
    b[kFrame] = {rgba_to_game(0x10101080), 1.0f};
    b[kRange] = {rgba_to_game(0x202020C0), 1.0f};
    b[kFill] = {rgba_to_game(0x4FD17CF0), 1.1f};
    b[kFillEnd] = {rgba_to_game(0xA0F0B0E0), 1.2f};
    b[kDamage] = {rgba_to_game(0xD04040FF), 1.3f};
    b[kHeal] = {rgba_to_game(0xF0F0A0FF), 1.4f};
    return b;
}

}  // namespace

TEST_CASE("parse_colour: #rrggbb keeps the game's alpha, #rrggbbaa sets it, game, and no #") {
    const ColourText a = parse_colour("#FF4040");
    REQUIRE(a.colour.has_value());
    CHECK(a.colour->rgba == 0xFF4040FFu);
    CHECK(a.colour->set);
    CHECK_FALSE(a.colour->has_alpha);
    CHECK_FALSE(a.game);

    const ColourText b = parse_colour("#ff404080");
    REQUIRE(b.colour.has_value());
    CHECK(b.colour->rgba == 0xFF404080u);
    CHECK(b.colour->set);
    CHECK(b.colour->has_alpha);

    CHECK(parse_colour("game").game);
    CHECK(parse_colour("GAME").game);
    CHECK_FALSE(parse_colour("game").colour.has_value());

    const ColourText c = parse_colour("ff4040");  // a line in emf/commands.txt treats # as a comment
    REQUIRE(c.colour.has_value());
    CHECK(*c.colour == *a.colour);
}

TEST_CASE("parse_colour: not colours") {
    for (const char* t : {"", "#ff40", "#gg0000", "#ff4040ff00", "red", "#", "#ff4040f", " #ff4040"}) {
        const ColourText r = parse_colour(t);
        CHECK_MESSAGE(!r.colour.has_value(), t);
        CHECK_MESSAGE(!r.game, t);
    }
}

TEST_CASE("colour_text round trips") {
    CHECK(colour_text(col("#Ff4040")) == "#ff4040");
    CHECK(colour_text(col("#FF404080")) == "#ff404080");
    CHECK(colour_text(Colour{}) == "game");
    CHECK(col(colour_text(col("#00000000")).c_str()) == col("#00000000"));
}

TEST_CASE("rgba_to_game reverses the bytes") {
    CHECK(rgba_to_game(0x11223344u) == 0x44332211u);
    CHECK(game_to_rgba(0x44332211u) == 0x11223344u);
    CHECK(game_to_rgba(rgba_to_game(0xDEADBEEFu)) == 0xDEADBEEFu);
}

TEST_CASE("with_colour: #rrggbb keeps the game's alpha and intensity") {  // Review Focus 3
    const UiColour game{rgba_to_game(0x202020C0), 1.2f};
    const UiColour a = with_colour(game, col("#ff0000"));
    CHECK(a.value == rgba_to_game(0xFF0000C0));
    CHECK(a.intensity == 1.2f);
    const UiColour b = with_colour(game, col("#ff000040"));
    CHECK(b.value == rgba_to_game(0xFF000040));
    CHECK(b.intensity == 1.2f);
    const UiColour c = with_colour(game, Colour{});  // not set: the game's
    CHECK(c.value == game.value);
    CHECK(c.intensity == game.intensity);
}

TEST_CASE("lighten moves toward white") {
    CHECK(lighten(0xFF000080u, 0.4f) == 0xFF666680u);
    CHECK(lighten(0x12345678u, 0.0f) == 0x12345678u);
    CHECK(lighten(0x00000011u, 1.0f) == 0xFFFFFF11u);
}

TEST_CASE("recoloured: fill alone also lightens the end; the rest byte for byte") {
    const StyleBlock g = game_block();
    BarColours c;
    c.part[static_cast<size_t>(BarPart::Fill)] = col("#ff0000");
    const StyleBlock r = recoloured(g, c);
    CHECK(r[kFill].value == rgba_to_game(0xFF0000F0));  // the game's fill alpha
    CHECK(r[kFill].intensity == 1.1f);
    CHECK(r[kFillEnd].value == rgba_to_game(0xFF6666E0));  // 40 % toward white, the game's end alpha
    CHECK(r[kFillEnd].intensity == 1.2f);
    for (size_t f : {static_cast<size_t>(kFrame), static_cast<size_t>(kRange), static_cast<size_t>(kDamage),
                     static_cast<size_t>(kHeal)})
        CHECK_MESSAGE(std::memcmp(&r[f], &g[f], sizeof(UiColour)) == 0, f);
}

TEST_CASE("recoloured: an end of its own wins; the frame never changes") {
    const StyleBlock g = game_block();
    BarColours c;
    c.part[static_cast<size_t>(BarPart::Fill)] = col("#ff0000");
    c.part[static_cast<size_t>(BarPart::Empty)] = col("#00000000");
    c.part[static_cast<size_t>(BarPart::End)] = col("#0000ff");
    c.part[static_cast<size_t>(BarPart::Damage)] = col("#00ff00");
    c.part[static_cast<size_t>(BarPart::Heal)] = col("#ffff00");
    CHECK(c.any());
    const StyleBlock r = recoloured(g, c);
    CHECK(r[kRange].value == rgba_to_game(0x00000000));
    CHECK(r[kFillEnd].value == rgba_to_game(0x0000FFE0));
    CHECK(r[kDamage].value == rgba_to_game(0x00FF00FF));
    CHECK(r[kHeal].value == rgba_to_game(0xFFFF00FF));
    CHECK(std::memcmp(&r[kFrame], &g[kFrame], sizeof(UiColour)) == 0);
    CHECK_FALSE(BarColours{}.any());
}

TEST_CASE("ui_colour_text in the game's form") {
    CHECK(ui_colour_text({rgba_to_game(0x4FD17CC0), 1.2f}) == "4fd17c-c0*1.20");
    CHECK(ui_colour_text({rgba_to_game(0x4FD17CFF), 1.0f}) == "4fd17c");
    CHECK(ui_colour_text({rgba_to_game(0x000000FF), 0.85f}) == "000000*0.85");
}

TEST_CASE("find_bar_part and bar_part_id") {
    for (size_t i = 0; i < kBarPartCount; ++i) {
        const BarPart p = static_cast<BarPart>(i);
        const auto found = find_bar_part(bar_part_id(p));
        REQUIRE(found.has_value());
        CHECK(*found == p);
    }
    CHECK(find_bar_part("FILL") == BarPart::Fill);
    CHECK_FALSE(find_bar_part("frame").has_value());
    CHECK_FALSE(find_bar_part("").has_value());
}
