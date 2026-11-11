#pragma once
// The smallest `size` the API accepts in each struct it is passed: the end of the last field of the version that
// added the struct (not sizeof, which grows when a later version appends a field and would then refuse every mod
// built against the older header). A field appended later is read, or written, only when the caller's size covers
// it (see EmfProgramDef in api.cpp); the struct's other fields then keep their defaults.
#include <cstddef>
#include <cstdint>

#include "emf/emf_api.h"

namespace emf::core {

template <size_t Offset, size_t Size>
constexpr uint32_t field_end() {
    return static_cast<uint32_t>(Offset + Size);
}
#define EMF_FIELD_END(T, f) ::emf::core::field_end<offsetof(T, f), sizeof(T::f)>()

constexpr uint32_t kItemInfoV5Size = EMF_FIELD_END(EmfItemInfo, name);
constexpr uint32_t kWheelV10Size = EMF_FIELD_END(EmfWheel, user);
constexpr uint32_t kItemsRequestV11Size = EMF_FIELD_END(EmfItemsRequest, user);
constexpr uint32_t kSettingDefV13Size = EMF_FIELD_END(EmfSettingDef, choices);
constexpr uint32_t kModInfoV13Size = EMF_FIELD_END(EmfModInfo, main);
constexpr uint32_t kPatternV14Size = EMF_FIELD_END(EmfPattern, rel32_tail);
constexpr uint32_t kGameBuildV14Size = EMF_FIELD_END(EmfGameBuild, text_size);
constexpr uint32_t kCommandSenderV15Size = EMF_FIELD_END(EmfCommandSender, pad);
constexpr uint32_t kCommandDefV17Size = EMF_FIELD_END(EmfCommandDef, user);
constexpr uint32_t kTeleportV20Size = EMF_FIELD_END(EmfTeleport, user);
constexpr uint32_t kComponentChangeV23Size = EMF_FIELD_END(EmfComponentChange, user);
constexpr uint32_t kWindowDefV24Size = EMF_FIELD_END(EmfWindowDef, user);
constexpr uint32_t kUiEventV24Size = EMF_FIELD_END(EmfUiEvent, flags);
constexpr uint32_t kOfferV25Size = EMF_FIELD_END(EmfOffer, reserved);
// The API table itself, by the version that last appended to it (a mod checks api->size against these).
constexpr uint32_t kApiV17Size = EMF_FIELD_END(EmfApi, register_command_ex);
constexpr uint32_t kApiV18Size = EMF_FIELD_END(EmfApi, damage);
constexpr uint32_t kApiV19Size = EMF_FIELD_END(EmfApi, request_save);
constexpr uint32_t kApiV20Size = EMF_FIELD_END(EmfApi, teleport);
constexpr uint32_t kApiV21Size = EMF_FIELD_END(EmfApi, world_setting_info);
constexpr uint32_t kApiV22Size = EMF_FIELD_END(EmfApi, player_level_set);
constexpr uint32_t kApiV23Size = EMF_FIELD_END(EmfApi, change_component);
constexpr uint32_t kApiV24Size = EMF_FIELD_END(EmfApi, window_close);
constexpr uint32_t kApiV25Size = EMF_FIELD_END(EmfApi, offer_remove);
constexpr uint32_t kApiV26Size = EMF_FIELD_END(EmfApi, offer_active);
constexpr uint32_t kApiV27Size = EMF_FIELD_END(EmfApi, net_peer);
constexpr uint32_t kApiV28Size = EMF_FIELD_END(EmfApi, on_player);
constexpr uint32_t kApiV29Size = EMF_FIELD_END(EmfApi, game_language);
constexpr uint32_t kApiV30Size = EMF_FIELD_END(EmfApi, chat_broadcast_ex);
constexpr uint32_t kApiV31Size = EMF_FIELD_END(EmfApi, hit_rule_remove);
constexpr uint32_t kApiV32Size = EMF_FIELD_END(EmfApi, on_buff);
constexpr uint32_t kApiV33Size = EMF_FIELD_END(EmfApi, marker_unsend);
constexpr uint32_t kApiV34Size = EMF_FIELD_END(EmfApi, ground_at);
constexpr uint32_t kApiV35Size = EMF_FIELD_END(EmfApi, canvas_draw);
constexpr uint32_t kUiWidgetV24Size = EMF_FIELD_END(EmfUiWidget, pad);  // 56: a v24/v25 mod's widget_size

// The x64 layouts mods are built with. A field that moves breaks every built mod; appending one changes sizeof only:
// then read it only when size covers it and update the sizeof here.
static_assert(kItemInfoV5Size == 122 && sizeof(EmfItemInfo) == 124, "EmfItemInfo layout (v5)");
static_assert(kWheelV10Size == 56 && sizeof(EmfWheel) == 56, "EmfWheel layout (v10)");
static_assert(kItemsRequestV11Size == 48 && sizeof(EmfItemsRequest) == 48 && offsetof(EmfItemsRequest, slot) == 24,
              "EmfItemsRequest layout (v11)");
static_assert(kSettingDefV13Size == 80 && sizeof(EmfSettingDef) == 80 && offsetof(EmfSettingDef, min) == 48,
              "EmfSettingDef layout (v13)");
static_assert(kModInfoV13Size == 337 && sizeof(EmfModInfo) == 340, "EmfModInfo layout (v13)");
static_assert(kPatternV14Size == 36 && sizeof(EmfPattern) == 40 && offsetof(EmfPattern, offset) == 24,
              "EmfPattern layout (v14)");
static_assert(kGameBuildV14Size == 96 && sizeof(EmfGameBuild) == 96 && offsetof(EmfGameBuild, module_base) == 80,
              "EmfGameBuild layout (v14)");
static_assert(kCommandSenderV15Size == 24 && sizeof(EmfCommandSender) == 24 && offsetof(EmfCommandSender, from_chat) == 16 &&
                  offsetof(EmfCommandSender, admin) == 17,
              "EmfCommandSender layout (v15; admin in the old pad since v17)");
static_assert(kCommandDefV17Size == 40 && sizeof(EmfCommandDef) == 40 && offsetof(EmfCommandDef, name) == 8 &&
                  offsetof(EmfCommandDef, cb) == 24,
              "EmfCommandDef layout (v17)");
static_assert(kApiV17Size == 480 && offsetof(EmfApi, attribute_get) == kApiV17Size &&
                  offsetof(EmfApi, attribute_add) == kApiV17Size + 8 && offsetof(EmfApi, damage) == kApiV17Size + 16,
              "EmfApi layout (v18 appends attribute_get, attribute_add, damage after register_command_ex)");
static_assert(kApiV18Size == 504, "EmfApi size (v18)");
static_assert(offsetof(EmfApi, chat_tell) == kApiV18Size && offsetof(EmfApi, chat_broadcast) == kApiV18Size + 8 &&
                  offsetof(EmfApi, player_name) == kApiV18Size + 16 && offsetof(EmfApi, on_chat) == kApiV18Size + 24 &&
                  offsetof(EmfApi, request_save) == kApiV18Size + 32,
              "EmfApi layout (v19 appends chat_tell, chat_broadcast, player_name, on_chat, request_save after damage)");
static_assert(kApiV19Size == 544, "EmfApi size (v19)");
static_assert(kTeleportV20Size == 64 && sizeof(EmfTeleport) == 64 && offsetof(EmfTeleport, x) == 8 &&
                  offsetof(EmfTeleport, yaw_deg) == 32 && offsetof(EmfTeleport, flags) == 40 &&
                  offsetof(EmfTeleport, cb) == 48,
              "EmfTeleport layout (v20)");
static_assert(sizeof(EmfTeleportResult) == 56 && offsetof(EmfTeleportResult, x) == 16 &&
                  offsetof(EmfTeleportResult, distance) == 40 && offsetof(EmfTeleportResult, reason) == 48,
              "EmfTeleportResult layout (v20)");
static_assert(offsetof(EmfApi, teleport) == kApiV19Size, "EmfApi layout (v20 appends teleport after request_save)");
static_assert(kApiV20Size == 552, "EmfApi size (v20)");
static_assert(offsetof(EmfApi, world_setting_get) == kApiV20Size &&
                  offsetof(EmfApi, on_world_setting_changed) == kApiV20Size + 24 &&
                  offsetof(EmfApi, world_setting_info) == kApiV20Size + 32,
              "EmfApi layout (v21 appends world_setting_get, _set, _reset, on_world_setting_changed, world_setting_info "
              "after teleport)");
static_assert(kApiV21Size == 592, "EmfApi size (v21)");
static_assert(offsetof(EmfApi, player_xp_required) == kApiV21Size && offsetof(EmfApi, player_level_set) == kApiV21Size + 16,
              "EmfApi layout (v22 appends player_xp_required, player_level_cap, player_level_set after world_setting_info)");
static_assert(kApiV22Size == 616, "EmfApi size (v22)");
static_assert(offsetof(EmfApi, change_component) == kApiV22Size,
              "EmfApi layout (v23 appends change_component after player_level_set)");
static_assert(kApiV23Size == 624, "EmfApi size (v23)");
static_assert(offsetof(EmfApi, window_open) == kApiV23Size,
              "EmfApi layout (v24 appends window_open, window_update, window_close after change_component)");
static_assert(kApiV24Size == 648, "EmfApi size (v24)");
static_assert(offsetof(EmfApi, spawn_offer) == kApiV24Size,
              "EmfApi layout (v25 appends spawn_offer, offer, offer_remove after window_close)");
static_assert(kApiV25Size == 672, "EmfApi size (v25)");
static_assert(offsetof(EmfApi, image_load) == kApiV25Size && offsetof(EmfApi, offer_active) == kApiV25Size + 8 &&
                  kApiV26Size == 688,
              "EmfApi layout (v26 appends image_load and offer_active after offer_remove)");
static_assert(offsetof(EmfApi, net_channel) == kApiV26Size && kApiV27Size == 728,
              "EmfApi layout (v27 appends net_channel, net_to_server, net_to_player, net_to_players, net_peer)");
static_assert(offsetof(EmfApi, on_player) == kApiV27Size && kApiV28Size == 736,
              "EmfApi layout (v28 appends on_player)");
static_assert(offsetof(EmfApi, item_name) == kApiV28Size && kApiV29Size == 752,
              "EmfApi layout (v29 appends item_name and game_language)");
static_assert(offsetof(EmfApi, player_character_name) == kApiV29Size && offsetof(EmfApi, chat_tell_ex) == 760 &&
                  kApiV30Size == 776,
              "EmfApi layout (v30 appends player_character_name, chat_tell_ex, chat_broadcast_ex)");
static_assert(offsetof(EmfApi, on_hit) == kApiV30Size && offsetof(EmfApi, hit_rule_add) == 792 && kApiV31Size == 808,
              "EmfApi layout (v31 appends on_hit, on_hit_before, hit_rule_add, hit_rule_remove)");
static_assert(offsetof(EmfApi, buff_types) == kApiV31Size && offsetof(EmfApi, buff_apply) == 840 &&
                  kApiV32Size == 864,
              "EmfApi layout (v32 appends buff_types, buff_type, buff_text, buffs_of, buff_apply, buff_remove, on_buff)");
static_assert(offsetof(EmfApi, marker_set) == kApiV32Size && kApiV33Size == 920,
              "EmfApi layout (v33 appends marker_set, marker_remove, marker_clear, marker_types, world_to_screen, "
              "marker_send, marker_unsend)");
static_assert(offsetof(EmfApi, map_cursor) == kApiV33Size && kApiV34Size == 952,
              "EmfApi layout (v34 appends map_cursor, on_map_click, map_action, ground_at)");
static_assert(offsetof(EmfApi, player_view) == kApiV34Size && kApiV35Size == 992 && sizeof(EmfApi) == kApiV35Size,
              "EmfApi layout (v35 appends player_view, fog_at, map_markers, canvas_set, canvas_draw)");
static_assert(sizeof(EmfPlayerView) == 48 && offsetof(EmfPlayerView, heading_deg) == 32, "EmfPlayerView layout (v35)");
static_assert(sizeof(EmfGameMarker) == 40 && offsetof(EmfGameMarker, x) == 16, "EmfGameMarker layout (v35)");
static_assert(sizeof(EmfCanvas) == 24 && offsetof(EmfCanvas, x) == 8, "EmfCanvas layout (v35)");
static_assert(sizeof(EmfCanvasCmd) == 56 && offsetof(EmfCanvasCmd, a) == 8 && offsetof(EmfCanvasCmd, s) == 48,
              "EmfCanvasCmd layout (v35)");
static_assert(sizeof(EmfMapClick) == 32 && offsetof(EmfMapClick, x) == 16, "EmfMapClick layout (v34)");
static_assert(sizeof(EmfMarker) == 80 && offsetof(EmfMarker, text) == 40 && offsetof(EmfMarker, flags) == 64 &&
                  offsetof(EmfMarker, game_icon) == 72,
              "EmfMarker layout (v33)");
static_assert(sizeof(EmfMapMarkerType) == 64 && offsetof(EmfMapMarkerType, name) == 16, "EmfMapMarkerType layout (v33)");
static_assert(sizeof(EmfBuffType) == 40 && offsetof(EmfBuffType, default_ns) == 16 &&
                  offsetof(EmfBuffType, name_tag) == 28,
              "EmfBuffType layout (v32)");
static_assert(sizeof(EmfBuff) == 40 && offsetof(EmfBuff, level) == 16 && offsetof(EmfBuff, total_ns) == 24,
              "EmfBuff layout (v32)");
static_assert(sizeof(EmfBuffEvent) == 48 && offsetof(EmfBuffEvent, total_ns) == 24 &&
                  offsetof(EmfBuffEvent, why) == 40,
              "EmfBuffEvent layout (v32)");
static_assert(sizeof(EmfPlayerEvent) == 96 && offsetof(EmfPlayerEvent, account) == 8 &&
                  offsetof(EmfPlayerEvent, player) == 16 && offsetof(EmfPlayerEvent, host) == 24 &&
                  offsetof(EmfPlayerEvent, name) == 32,
              "EmfPlayerEvent layout (v28)");
static_assert(sizeof(EmfNetFrom) == 24 && offsetof(EmfNetFrom, account) == 8 && offsetof(EmfNetFrom, admin) == 20,
              "EmfNetFrom layout (v27)");
static_assert(sizeof(EmfNetChannel) == 56 && offsetof(EmfNetChannel, name) == 8 &&
                  offsetof(EmfNetChannel, on_server) == 32 && offsetof(EmfNetChannel, user) == 48,
              "EmfNetChannel layout (v27)");
static_assert(kOfferV25Size == 24 && sizeof(EmfOffer) == 24 && offsetof(EmfOffer, text) == 8 &&
                  offsetof(EmfOffer, sequence) == 16 && offsetof(EmfOffer, range_m) == 20,
              "EmfOffer layout (v25)");
static_assert(kComponentChangeV23Size == 48 && sizeof(EmfComponentChange) == 48 &&
                  offsetof(EmfComponentChange, data) == 16 && offsetof(EmfComponentChange, cb) == 32,
              "EmfComponentChange layout (v23)");
static_assert(kUiWidgetV24Size == 56 && offsetof(EmfUiWidget, key) == 8 && offsetof(EmfUiWidget, count) == 32 &&
                  offsetof(EmfUiWidget, rgba) == 40 && offsetof(EmfUiWidget, item) == 48,
              "EmfUiWidget layout (v24)");
static_assert(sizeof(EmfUiWidget) == 64 && offsetof(EmfUiWidget, fps) == 56 && offsetof(EmfUiWidget, height) == 60,
              "EmfUiWidget layout (v26 appends fps and height)");
static_assert(kUiEventV24Size == 40 && sizeof(EmfUiEvent) == 40 && offsetof(EmfUiEvent, window) == 8 &&
                  offsetof(EmfUiEvent, reason) == 32,
              "EmfUiEvent layout (v24)");
static_assert(kWindowDefV24Size == 72 && sizeof(EmfWindowDef) == 72 && offsetof(EmfWindowDef, widgets) == 24 &&
                  offsetof(EmfWindowDef, width) == 48 && offsetof(EmfWindowDef, world_kind) == 52 &&
                  offsetof(EmfWindowDef, cb) == 56,
              "EmfWindowDef layout (v24)");
static_assert(sizeof(EmfWorldSettingInfo) == 72 && offsetof(EmfWorldSettingInfo, key) == 8 &&
                  offsetof(EmfWorldSettingInfo, min) == 32 && offsetof(EmfWorldSettingInfo, flags) == 64,
              "EmfWorldSettingInfo layout (v21)");

static_assert(sizeof(EmfHit) == 80 && offsetof(EmfHit, amount) == 20 && offsetof(EmfHit, damage_types) == 32 &&
                  offsetof(EmfHit, weapon_category) == 36 && offsetof(EmfHit, reaction) == 52 &&
                  offsetof(EmfHit, health_before) == 56 && offsetof(EmfHit, position) == 64 &&
                  offsetof(EmfHit, cancel) == 76,
              "EmfHit layout (v31)");
static_assert(sizeof(EmfHitRule) == 36 && offsetof(EmfHitRule, damage_types_any) == 16 &&
                  offsetof(EmfHitRule, scale) == 20 && offsetof(EmfHitRule, healing) == 32 &&
                  offsetof(EmfHitRule, kinds) == 33,
              "EmfHitRule layout (v31)");

}  // namespace emf::core
