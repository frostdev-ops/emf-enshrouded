#include "core/buffs.h"

#include <cstring>

#include "common/fnv.h"
#include "common/seh.h"

namespace emf::core {

namespace {

template <typename T>
T at(const uint8_t* p, size_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}
template <typename T>
void put(uint8_t* p, size_t off, T v) {
    std::memcpy(p + off, &v, sizeof v);
}

struct Group {
    uint8_t slot;
    size_t offset;
    uint8_t count;
};
constexpr size_t kInstance = 12;
constexpr size_t kExtendedFood = 0x1D0;
// container order: food +0, buffs +0x30, debuffs +0xF0, rested +0x1B0, terrain +0x1BC
constexpr Group kGroups[] = {{EMF_BUFF_FOOD, 0x000, 4},   {EMF_BUFF_BUFF, 0x030, 16},  {EMF_BUFF_DEBUFF, 0x0F0, 16},
                             {EMF_BUFF_RESTED, 0x1B0, 1}, {EMF_BUFF_TERRAIN, 0x1BC, 1}};

uint8_t used_slots(const uint8_t* c, const Group& g) {
    if (g.slot == EMF_BUFF_FOOD) return c[kExtendedFood] ? 4 : 3;
    return g.count;
}

}  // namespace

uint32_t buff_guid_id(const uint8_t* g) {
    static const uint8_t zero[16] = {};
    if (std::memcmp(g, zero, 16) == 0) return 0;
    return emf::fnv1a32(g, 16);
}

bool buff_collection(const uint8_t* registry, BuffCollection* out) {
    if (!registry || !out) return false;
    const uint8_t* blob = nullptr;
    if (!seh_read(registry + 0x20, &blob, sizeof blob) || !blob) return false;
    int32_t rel = 0;
    uint32_t count = 0;
    if (!seh_read(blob + 0x18, &rel, sizeof rel) || !seh_read(blob + 0x1C, &count, sizeof count)) return false;
    if (count == 0 || count > kBuffMaxTypes || rel == 0) return false;
    const uint8_t* records = blob + 0x18 + rel;
    uint8_t probe = 0;
    if (!seh_read(records, &probe, 1) || !seh_read(records + static_cast<size_t>(count) * kBuffTypeSize - 1, &probe, 1))
        return false;
    out->records = records;
    out->count = count;
    return true;
}

const uint8_t* buff_record(const BuffCollection& c, uint32_t id) {
    if (!c.records || !id) return nullptr;
    for (uint32_t i = 0; i < c.count; ++i) {
        const uint8_t* r = c.records + static_cast<size_t>(i) * kBuffTypeSize;
        uint32_t rid = 0;
        if (!seh_read(r, &rid, sizeof rid)) return nullptr;
        if (rid == id) return r;
    }
    return nullptr;
}

bool buff_type_of(const uint8_t* record, EmfBuffType* out) {
    uint8_t r[kBuffTypeSize];
    if (!record || !out || !seh_read(record, r, sizeof r)) return false;
    EmfBuffType t{};
    t.size = sizeof t;
    t.id = at<uint32_t>(r, 0x00);
    t.slot = r[0x04];
    t.apply_type = r[0x05];
    t.default_ns = at<int64_t>(r, 0x08);
    t.name_tag = buff_guid_id(r + 0x10);
    t.desc_tag = buff_guid_id(r + 0x20);
    if (r[0x30]) t.flags |= EMF_BUFF_FOOD_POISON;
    if (r[0x31]) t.flags |= EMF_BUFF_HIDDEN;
    if (r[0x32]) t.flags |= EMF_BUFF_SHOW_AS_TAG;
    if (r[0x49]) t.flags |= EMF_BUFF_DESPAWN_ON_DEATH;
    if (r[0x4A]) t.flags |= EMF_BUFF_SAVED;
    t.faction_exclude = at<uint16_t>(r, 0x5C);
    t.follow_up = r[0x98] ? buff_guid_id(r + 0x88) : 0;
    *out = t;
    return true;
}

uint32_t read_buffs(const uint8_t* c, BuffNetFn net, void* ctx, int64_t now_ns, EmfBuff* out, uint32_t cap) {
    uint32_t n = 0;
    for (const Group& g : kGroups) {
        const uint8_t used = used_slots(c, g);
        for (uint8_t i = 0; i < used; ++i) {
            const uint8_t* inst = c + g.offset + i * kInstance;
            const uint32_t entity = at<uint32_t>(inst, 0);
            if (!entity) continue;
            if (out && n < cap) {
                EmfBuff b{};
                b.size = sizeof b;
                b.type = at<uint32_t>(inst, 4);
                b.entity = entity;
                b.slot = g.slot;
                b.index = i;
                b.cleansed = inst[8] ? 1 : 0;
                b.total_ns = b.remaining_ns = -1;
                const BuffNet d = net ? net(entity, ctx) : BuffNet{};
                if (d.known && d.end) {
                    b.total_ns = d.life;
                    b.remaining_ns = d.end > now_ns ? d.end - now_ns : 0;
                    b.level = d.level;
                }
                out[n] = b;
            }
            ++n;
        }
    }
    return n;
}

bool buff_has(const uint8_t* c, uint32_t type) {
    if (!type) return false;
    for (const Group& g : kGroups)
        for (uint8_t i = 0; i < g.count; ++i)
            if (at<uint32_t>(c, g.offset + i * kInstance + 4) == type) return true;
    return false;
}

bool buff_cleanse(uint8_t* c, uint32_t type) {
    if (!type) return false;
    for (const Group& g : kGroups)
        for (uint8_t i = 0; i < g.count; ++i) {
            uint8_t* inst = c + g.offset + i * kInstance;
            if (at<uint32_t>(inst, 4) == type) {
                inst[8] = 1;
                return true;
            }
        }
    return false;
}

void fill_apply_ctx(uint8_t* c, const BuffHandles& h, const BuffApplyArgs& a) {
    std::memset(c, 0, kApplyBuffCtxSize);
    put(c, 0x00, reinterpret_cast<uint64_t>(h.program_context));
    put(c, 0x10, a.item);
    put(c, 0x28, h.container_w);
    put(c, 0x30, h.lifetime_w);
    put(c, 0x38, reinterpret_cast<uint64_t>(a.target_skills_ui));
    put(c, 0x40, reinterpret_cast<uint64_t>(h.skill_tree));
    put(c, 0x48, reinterpret_cast<uint64_t>(a.target_wet_reduction));
    put(c, 0x60, a.duration_ns);
    if (a.with_source)
        for (int i = 0; i < 11; ++i) put(c, 0x68 + 8 * i, h.source[i]);
    put(c, 0xC0, h.faction_r);
    put(c, 0xC8, reinterpret_cast<uint64_t>(a.item ? h.item_registry : nullptr));
    put(c, 0xD0, reinterpret_cast<uint64_t>(h.balancing));
    put(c, 0xD8, reinterpret_cast<uint64_t>(h.events));
    put(c, 0xE0, reinterpret_cast<uint64_t>(h.events));
}

const char* buff_slot_name(uint8_t slot) {
    switch (slot) {
        case EMF_BUFF_FOOD: return "food";
        case EMF_BUFF_BUFF: return "buff";
        case EMF_BUFF_DEBUFF: return "debuff";
        case EMF_BUFF_RESTED: return "rested";
        case EMF_BUFF_TERRAIN: return "terrain";
        default: return "?";
    }
}

BuffCheck check_applied(uint8_t err, const uint8_t* c, uint32_t type, uint8_t slot, uint16_t faction_exclude,
                        int target_faction, bool despawn_on_death) {
    BuffCheck r;
    switch (err) {
        case 0: break;
        case 0x11: r.why = "the target has it already and this type cannot be given again"; return r;
        case 0x12: r.why = "the target has no buff container"; return r;
        case 0x14: r.why = std::string("no free ") + buff_slot_name(slot) + " slot"; return r;
        case 0x15: r.why = "the buff it has of this type has no entity"; return r;
        default: r.why = "the game refused it (error " + std::to_string(err) + ")"; return r;
    }
    for (const Group& g : kGroups) {
        if (g.slot != slot) continue;
        for (uint8_t i = 0; i < g.count; ++i)
            if (at<uint32_t>(c, g.offset + i * kInstance + 4) == type && at<uint32_t>(c, g.offset + i * kInstance)) {
                r.applied = true;
                return r;
            }
    }
    if (target_faction >= 0 && target_faction < 16 && ((faction_exclude >> target_faction) & 1u))
        r.why = "the type is never given to the target's faction (" + std::to_string(target_faction) + ")";
    else if (despawn_on_death)
        r.why = "the target is dead (this type is never given to the dead)";
    else
        r.why = "the game did not give it";
    return r;
}

std::vector<EmfBuffEvent> BuffDiff::step(uint32_t entity, const uint8_t* c, BuffNetFn net, void* ctx, int64_t now) {
    std::vector<Inst> cur;
    for (const Group& g : kGroups)
        for (uint8_t i = 0; i < g.count; ++i) {
            const uint8_t* inst = c + g.offset + i * kInstance;
            const uint32_t be = at<uint32_t>(inst, 0);
            if (!be) continue;
            const BuffNet d = net ? net(be, ctx) : BuffNet{};
            cur.push_back(Inst{be, at<uint32_t>(inst, 4), g.slot, static_cast<uint8_t>(inst[8] ? 1 : 0),
                               d.known && d.end ? d.end : -1});
        }
    std::vector<EmfBuffEvent> ev;
    const auto it = last_.find(entity);
    if (it == last_.end()) {
        last_.emplace(entity, std::move(cur));
        return ev;
    }
    auto make = [&](uint32_t kind, const Inst& x) {
        EmfBuffEvent e{};
        e.size = sizeof e;
        e.kind = kind;
        e.target = entity;
        e.type = x.type;
        e.buff_entity = x.buff_entity;
        e.slot = x.slot;
        e.cleansed = x.cleansed;
        e.total_ns = e.remaining_ns = -1;
        if (x.end >= 0) {
            const BuffNet d = net ? net(x.buff_entity, ctx) : BuffNet{};
            if (d.known) e.total_ns = d.life;
            e.remaining_ns = x.end > now ? x.end - now : 0;
        }
        return e;
    };
    const std::vector<Inst>& old = it->second;
    for (const Inst& x : cur) {
        const Inst* was = nullptr;
        for (const Inst& o : old)
            if (o.buff_entity == x.buff_entity) was = &o;
        if (!was)
            ev.push_back(make(EMF_BUFF_APPLIED, x));
        else if (was->end >= 0 && x.end > was->end + kBuffRefreshMinNs)
            ev.push_back(make(EMF_BUFF_REFRESHED, x));
    }
    for (const Inst& o : old) {
        bool still = false;
        for (const Inst& x : cur)
            if (x.buff_entity == o.buff_entity) still = true;
        if (!still) {
            EmfBuffEvent e = make(EMF_BUFF_REMOVED, o);
            e.total_ns = e.remaining_ns = -1;
            e.remaining_ns = 0;
            ev.push_back(e);
        }
    }
    it->second = std::move(cur);
    return ev;
}

void BuffDiff::forget(uint32_t entity) { last_.erase(entity); }

void BuffDiff::retain(const std::vector<uint32_t>& present) {
    for (auto it = last_.begin(); it != last_.end();) {
        bool keep = false;
        for (uint32_t e : present)
            if (e == it->first) keep = true;
        it = keep ? std::next(it) : last_.erase(it);
    }
}

void BuffDiff::reset() { last_.clear(); }

}  // namespace emf::core
