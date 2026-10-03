/*
 * mod-coa-lre: two Legendary Runic Enhancements for a personal CoA server.
 *
 *   Rune of the Hallowed Path   Consecration follows its caster instead of staying on the ground.
 *   Rune of the Eternal Conduit Lightning Shield never expires, never loses charges, and every
 *                               3 seconds strikes up to 5 hostile enemies within 8 yards.
 *   Rune of the Righteous Crusading Storm  Divine Storm delivers a Crusader Strike to each enemy hit.
 *   Rune of the Thunderous Gale  Thunder Clap drops a full Hurricane at your feet and 3 Nature
 *                               pulses of 30% of the Thunder Clap's damage.
 *   Rune of the Purifying Fires  Crusader Strike engulfs its target in your highest Holy Fire.
 *
 * Each rune also has an item (9900001-9900005): using it grants the rune, teaches its spells at the
 * rank the character's level allows, and is used up. New ranks are learned while levelling.
 *
 * Runes can also be granted with GM commands (.lre add / remove / list / info) and are stored in
 * the characters table coa_lre_runes, which the module creates on startup.
 */

#include "CellImpl.h"
#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "DynamicObject.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "CreatureScript.h"
#include "AllCreatureScript.h"
#include "PassiveAI.h"
#include "ScriptedGossip.h"
#include "ItemScript.h"
#include "TemporarySummon.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "UnitScript.h"
#include <unordered_map>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <list>
#include <string>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    enum LreRune : uint32
    {
        RUNE_HALLOWED_PATH    = 0x0001,
        RUNE_ETERNAL_CONDUIT  = 0x0002,
        RUNE_CRUSADING_STORM  = 0x0004,
        RUNE_THUNDEROUS_GALE  = 0x0008,
        RUNE_PURIFYING_FIRES  = 0x0010,
        RUNE_ARCING_SHOCKS    = 0x0020,
        RUNE_SPREADING_SHOCKS = 0x0040,
        RUNE_CRASHING_STORM   = 0x0080,
        RUNE_WALKING_DECAY    = 0x0100,
        RUNE_MASS_GRIP        = 0x0200,
        RUNE_SEARING_SWARM    = 0x0400,
        RUNE_MAELSTROM_WEAPON = 0x0800,
        RUNE_THASSARIAN       = 0x1000,
    };

    // First rank of each spell a rune works with (rank chains from the core's spell_ranks / talents).
    constexpr uint32 SPELL_CONSECRATION_R1     = 26573;
    constexpr uint32 SPELL_LIGHTNING_SHIELD_R1 = 324;
    constexpr uint32 SPELL_CRUSADER_STRIKE     = 35395;
    constexpr uint32 SPELL_DIVINE_STORM        = 53385;
    constexpr uint32 SPELL_THUNDER_CLAP_R1     = 6343;
    constexpr uint32 SPELL_HURRICANE_R1        = 16914;
    constexpr uint32 SPELL_HOLY_FIRE_R1        = 14914;
    constexpr uint32 SPELL_EARTH_SHOCK_R1      = 8042;
    constexpr uint32 SPELL_FLAME_SHOCK_R1      = 8050;
    constexpr uint32 SPELL_FROST_SHOCK_R1      = 8056;
    constexpr uint32 SPELL_STORMSTRIKE         = 17364;
    constexpr uint32 SPELL_DEATH_AND_DECAY_R1  = 43265;
    constexpr uint32 SPELL_DEATH_GRIP          = 49576;
    constexpr uint32 SPELL_SEARING_TOTEM_R1    = 3599;
    constexpr uint32 TALENT_MAELSTROM_WEAPON_R1 = 51528;
    constexpr uint32 TALENT_THASSARIAN_R1      = 65661;

    enum RuneTier : uint8
    {
        TIER_EPIC      = 4,
        TIER_LEGENDARY = 5,
    };

    struct RuneDef
    {
        uint32 Bit;
        uint32 Item;
        char const* Name;
        std::array<uint32, 3> Spells; // first ranks the rune's item teaches (0 = none)
        RuneTier Tier;
        bool FullRank;                // talents: always the top rank (5/5, 3/3...)
    };

    constexpr std::array<RuneDef, 13> RUNES = { {
        { RUNE_HALLOWED_PATH,    9900001, "Rune of the Hallowed Path",                      { SPELL_CONSECRATION_R1, 0, 0 }, TIER_LEGENDARY, false },
        { RUNE_ETERNAL_CONDUIT,  9900002, "Rune of the Eternal Conduit",                    { SPELL_LIGHTNING_SHIELD_R1, 0, 0 }, TIER_LEGENDARY, false },
        { RUNE_CRUSADING_STORM,  9900003, "Rune of the Righteous Crusading Storm",          { SPELL_CRUSADER_STRIKE, SPELL_DIVINE_STORM, 0 }, TIER_LEGENDARY, false },
        { RUNE_THUNDEROUS_GALE,  9900004, "Rune of the Thunderous Gale",                    { SPELL_THUNDER_CLAP_R1, SPELL_HURRICANE_R1, 0 }, TIER_LEGENDARY, false },
        { RUNE_PURIFYING_FIRES,  9900005, "Rune of the Purifying Fires of the Silver Hand", { SPELL_CRUSADER_STRIKE, SPELL_HOLY_FIRE_R1, 0 }, TIER_LEGENDARY, false },
        { RUNE_ARCING_SHOCKS,    9900011, "Rune of Arcing Shocks",                          { SPELL_EARTH_SHOCK_R1, SPELL_FLAME_SHOCK_R1, SPELL_FROST_SHOCK_R1 }, TIER_LEGENDARY, false },
        { RUNE_SPREADING_SHOCKS, 9900012, "Rune of Spreading Shocks",                       { SPELL_EARTH_SHOCK_R1, SPELL_FLAME_SHOCK_R1, SPELL_FROST_SHOCK_R1 }, TIER_LEGENDARY, false },
        { RUNE_CRASHING_STORM,   9900013, "Rune of the Crashing Storm",                     { SPELL_STORMSTRIKE, 0, 0 }, TIER_LEGENDARY, false },
        { RUNE_WALKING_DECAY,    9900014, "Rune of the Walking Decay",                      { SPELL_DEATH_AND_DECAY_R1, 0, 0 }, TIER_LEGENDARY, false },
        { RUNE_MASS_GRIP,        9900015, "Rune of the Mass Grip",                          { SPELL_DEATH_GRIP, 0, 0 }, TIER_LEGENDARY, false },
        { RUNE_SEARING_SWARM,    9900016, "Rune of the Searing Swarm",                      { SPELL_SEARING_TOTEM_R1, 0, 0 }, TIER_LEGENDARY, false },
        { RUNE_MAELSTROM_WEAPON, 9900017, "Rune of Maelstrom Weapon",                       { TALENT_MAELSTROM_WEAPON_R1, 0, 0 }, TIER_EPIC, true },
        { RUNE_THASSARIAN,       9900018, "Rune of the Threat of Thassarian",               { TALENT_THASSARIAN_R1, 0, 0 }, TIER_EPIC, true },
    } };

    RuneDef const* RuneByBit(uint32 bit)
    {
        for (RuneDef const& rune : RUNES)
            if (rune.Bit == bit)
                return &rune;
        return nullptr;
    }

    RuneDef const* RuneByItem(uint32 entry)
    {
        for (RuneDef const& rune : RUNES)
            if (rune.Item == entry)
                return &rune;
        return nullptr;
    }

    void SyncRuneSpells(Player* player, uint32 runes);

    // Thunderous Gale: extra Nature pulses scaled from the Thunder Clap that started them.
    constexpr uint32 GALE_PULSES = 3;
    constexpr uint32 GALE_INTERVAL_MS = 1000;
    constexpr float GALE_DAMAGE_PCT = 30.0f;
    constexpr float GALE_DEFAULT_RADIUS = 8.0f;

    // Every 10 s a rune owner's taught spells are checked again (Wildcard rerolls can remove them).
    constexpr uint32 SPELL_SYNC_MS = 10000;

    constexpr char const* DATA_KEY = "coa_lre";

    // Lightning Shield damage, rank 1. Higher ranks come from the same rank chain.
    constexpr uint32 LIGHTNING_SHIELD_DAMAGE_R1 = 26364;
    // Lightning Shield's family flag, the same lookup the core's Static Shock script uses.
    constexpr uint32 LIGHTNING_SHIELD_FAMILY_FLAG = 0x00000400;

    constexpr uint32 CONDUIT_INTERVAL_MS = 3000;
    constexpr float CONDUIT_RADIUS = 8.0f;
    constexpr std::size_t CONDUIT_MAX_TARGETS = 5;

    constexpr uint32 HALLOWED_FOLLOW_MS = 200;
    constexpr uint32 HALLOWED_PULSE_MS = 1000;
    constexpr float HALLOWED_MIN_MOVE = 0.5f;

    // SpellVisualKit.dbc ids, read from the server's own data. Changeable live with .lre kit.
    //   282  Chain Lightning impact (chest + sound)      on every enemy the Conduit strikes
    //   62   Lightning Shield impact (base + sound)      on the shaman when the Conduit fires
    //   22284 paladin_lighthammer_holynova (base + sound) on the paladin every Hallowed Path pulse
    std::atomic<uint32> ConduitTargetKit{ 282 };
    std::atomic<uint32> ConduitSelfKit{ 62 };
    std::atomic<uint32> HallowedPulseKit{ 22284 }; // paladin_lighthammer_holynova, chosen in game
    // Consecration's own ground glow: off = hidden (only the pulse shows), on = redrawn at the feet.
    std::atomic<bool> HallowedGround{ false };
    // A buff shown on the paladin for as long as the Hallowed Path Consecration lasts (0 = none). Any
    // existing harmless dummy-aura spell will do; .lre findbuff lists candidates with the right icon.
    std::atomic<uint32> HallowedBuffSpell{ 0 };
    // Walking Decay: the pulse played at the death knight's feet every second (live: .lre kit decay <id>).
    std::atomic<uint32> DecayPulseKit{ 9242 }; // deathanddecay_area_base, with sound
    // A spell whose cast visual the client plays as arcs from the shaman to the struck enemies.
    // 421 = Chain Lightning rank 1. Only its look is sent; it never casts, costs or damages.
    std::atomic<uint32> ConduitArcSpell{ 421 };
    // Optional visual at the player on each Thunderous Gale pulse (0 = none until one is chosen).
    std::atomic<uint32> GaleKit{ 0 };

    struct LreData : public DataMap::Base
    {
        uint32 Runes = 0;
        uint32 ConduitTimer = 0;
        uint32 HallowedSpell = 0;
        uint32 FollowTimer = 0;
        uint32 PulseTimer = 0;
        uint32 SyncTimer = 0;
        uint32 DecaySpell = 0;
        uint32 DecayFollowTimer = 0;
        uint32 DecayRedrawTimer = 0;

        struct Gale
        {
            uint32 Timer = 0;
            uint32 PulsesLeft = GALE_PULSES;
            uint32 Damage = 0;
            float Radius = GALE_DEFAULT_RADIUS;
        };
        std::vector<Gale> Gales;

        // A Hurricane from Thunderous Gale: its ground effect and slow come from the spell itself; its
        // damage is dealt here every tick, because Hurricane's damage normally comes from the caster's
        // own channel, which the invisible helper cannot give the player.
        struct Storm
        {
            ObjectGuid Anchor;
            uint32 DamageSpell = 0;
            uint32 Interval = 1000;
            uint32 Timer = 0;
            uint32 TicksLeft = 0;
            float Radius = 8.0f;
        };
        std::vector<Storm> Storms;
    };

    // Only characters that own a rune ever get data, so bots and everyone else cost one lookup.
    LreData* FindData(Player* player)
    {
        return player->CustomData.Get<LreData>(DATA_KEY);
    }

    LreData* EnsureData(Player* player)
    {
        return player->CustomData.GetDefault<LreData>(DATA_KEY);
    }

    bool HasRune(Unit* unit, uint32 rune)
    {
        Player* player = unit ? unit->ToPlayer() : nullptr;
        if (!player)
            return false;
        LreData* data = FindData(player);
        return data && (data->Runes & rune);
    }

    // The client draws an item's bag icon from its own Item.dbc, which has no rows for these custom
    // items, so it shows a question mark. CoA's client (Extensions.dll) accepts Item.dbc rows from the
    // server: SMSG_PATCH_ITEM (0x932), 8 dwords = ID, class, subclass, sound override subclass,
    // material, display id, inventory type, sheath. Values come from item_template, so they always
    // match the database.
    constexpr uint16 SMSG_PATCH_ITEM = 0x0932;

    // The reusable unlearning item and the unlearning NPC.
    constexpr uint32 ITEM_RUNE_UNLEARN = 9900010;
    constexpr uint32 GOSSIP_SENDER_RUNE = 1;

    void SendRuneItemRows(Player* player)
    {
        if (!player->GetSession())
            return;
        std::vector<uint32> items;
        for (RuneDef const& rune : RUNES)
            items.push_back(rune.Item);
        items.push_back(ITEM_RUNE_UNLEARN);
        for (uint32 entry : items)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            if (!proto)
                continue;
            WorldPacket packet(SMSG_PATCH_ITEM, 32);
            packet << uint32(proto->ItemId);
            packet << uint32(proto->Class);
            packet << uint32(proto->SubClass);
            packet << int32(proto->SoundOverrideSubclass);
            packet << int32(proto->Material);
            packet << uint32(proto->DisplayInfoID);
            packet << uint32(proto->InventoryType);
            packet << uint32(proto->Sheath);
            player->GetSession()->SendPacket(&packet);
        }
    }

    void LoadRunes(Player* player)
    {
        QueryResult result = CharacterDatabase.Query("SELECT `rune` FROM `coa_lre_runes` WHERE `guid` = {}",
            player->GetGUID().GetCounter());
        if (!result)
            return;

        LreData* data = EnsureData(player);
        data->Runes = 0;
        do
        {
            data->Runes |= (*result)[0].Get<uint32>();
        } while (result->NextRow());
        SyncRuneSpells(player, data->Runes);
    }

    // Grants a rune: stored, active immediately, spells taught. False if already owned.
    bool GrantRune(Player* player, uint32 rune)
    {
        LreData* data = EnsureData(player);
        if (data->Runes & rune)
            return false;
        CharacterDatabase.Execute("INSERT IGNORE INTO `coa_lre_runes` (`guid`, `rune`) VALUES ({}, {})",
            player->GetGUID().GetCounter(), rune);
        data->Runes |= rune;
        SyncRuneSpells(player, data->Runes);
        return true;
    }

    // Unlearns a rune: its effect stops, it is deleted, and every rank of the spells it taught is
    // removed (with their active auras) unless another rune the character still owns teaches them.
    // False if the character does not own it.
    bool UnlearnRune(Player* player, uint32 rune)
    {
        LreData* data = FindData(player);
        RuneDef const* def = RuneByBit(rune);
        if (!data || !def || !(data->Runes & rune))
            return false;

        CharacterDatabase.Execute("DELETE FROM `coa_lre_runes` WHERE `guid` = {} AND `rune` = {}",
            player->GetGUID().GetCounter(), rune);
        data->Runes &= ~rune;
        if (rune == RUNE_HALLOWED_PATH)
            data->HallowedSpell = 0;
        if (rune == RUNE_WALKING_DECAY)
            data->DecaySpell = 0;
        if (rune == RUNE_THUNDEROUS_GALE)
        {
            data->Gales.clear();
            data->Storms.clear();
        }

        for (uint32 first : def->Spells)
        {
            if (!first)
                continue;
            bool neededElsewhere = false;
            for (RuneDef const& other : RUNES)
                if ((data->Runes & other.Bit) &&
                    std::find(other.Spells.begin(), other.Spells.end(), first) != other.Spells.end())
                    neededElsewhere = true;
            if (neededElsewhere)
                continue;
            for (uint32 id = first; id; id = sSpellMgr->GetNextSpellInChain(id))
            {
                player->RemoveAurasDueToSpell(id);
                if (player->HasSpell(id))
                    player->removeSpell(id, SPEC_MASK_ALL, false);
            }
        }
        return true;
    }

    // The unlearning menu, shared by the item and the NPC: one line per owned rune, each confirmed.
    constexpr uint32 GOSSIP_SENDER_VENDOR = 2;

    bool ShowUnlearnMenu(Player* player, ObjectGuid source, bool vendor = false)
    {
        ClearGossipMenuFor(player);
        if (vendor)
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "Browse runes", GOSSIP_SENDER_VENDOR, 0);
        LreData* data = FindData(player);
        uint32 shown = 0;
        for (RuneDef const& rune : RUNES)
        {
            if (!data || !(data->Runes & rune.Bit))
                continue;
            std::string const confirm = std::string("Unlearn the ") + rune.Name +
                "? Its spells are removed and the rune is gone.";
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, std::string("Unlearn ") + rune.Name,
                GOSSIP_SENDER_RUNE, rune.Bit, confirm, 0, false);
            ++shown;
        }
        if (!shown && !vendor)
        {
            CloseGossipMenuFor(player);
            ChatHandler(player->GetSession()).SendSysMessage("You know no Runic Enhancements.");
            return false;
        }
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, source);
        return true;
    }

    void HandleUnlearnChoice(Player* player, uint32 sender, uint32 action)
    {
        CloseGossipMenuFor(player);
        if (sender != GOSSIP_SENDER_RUNE)
            return;
        RuneDef const* rune = RuneByBit(action);
        if (rune && UnlearnRune(player, rune->Bit))
            ChatHandler(player->GetSession()).PSendSysMessage("You have unlearned the {}.", rune->Name);
    }

    bool ParseRune(std::string name, uint32& rune)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (name == "hallowed" || name == "hallowedpath" || name == "consecration")
            rune = RUNE_HALLOWED_PATH;
        else if (name == "conduit" || name == "eternalconduit" || name == "lightningshield")
            rune = RUNE_ETERNAL_CONDUIT;
        else if (name == "storm" || name == "crusadingstorm" || name == "divinestorm")
            rune = RUNE_CRUSADING_STORM;
        else if (name == "gale" || name == "thunderousgale" || name == "thunderclap")
            rune = RUNE_THUNDEROUS_GALE;
        else if (name == "fires" || name == "purifyingfires" || name == "holyfire")
            rune = RUNE_PURIFYING_FIRES;
        else if (name == "arcing" || name == "arcingshocks")
            rune = RUNE_ARCING_SHOCKS;
        else if (name == "spreading" || name == "spreadingshocks")
            rune = RUNE_SPREADING_SHOCKS;
        else if (name == "crashing" || name == "crashingstorm" || name == "stormstrike")
            rune = RUNE_CRASHING_STORM;
        else if (name == "decay" || name == "walkingdecay")
            rune = RUNE_WALKING_DECAY;
        else if (name == "grip" || name == "massgrip")
            rune = RUNE_MASS_GRIP;
        else if (name == "searing" || name == "searingswarm")
            rune = RUNE_SEARING_SWARM;
        else if (name == "maelstrom" || name == "maelstromweapon")
            rune = RUNE_MAELSTROM_WEAPON;
        else if (name == "thassarian" || name == "threatofthassarian")
            rune = RUNE_THASSARIAN;
        else
            return false;
        return true;
    }

    char const* RuneName(uint32 rune)
    {
        RuneDef const* def = RuneByBit(rune);
        return def ? def->Name : "Unknown rune";
    }

    // Highest rank of a rank chain this character knows (0 = none).
    uint32 HighestKnown(Player* player, uint32 firstRank)
    {
        uint32 best = 0;
        for (uint32 id = firstRank; id; id = sSpellMgr->GetNextSpellInChain(id))
            if (player->HasSpell(id))
                best = id;
        return best;
    }

    // Highest rank of a rank chain this character's level allows; never less than rank 1.
    // From this level on, runes teach the top rank of their spells (its damage is scaled to the level).
    constexpr uint8 TOP_RANK_LEVEL = 60;

    uint32 RankForLevel(uint32 firstRank, uint8 level)
    {
        if (level >= TOP_RANK_LEVEL)
            return sSpellMgr->GetLastSpellInChain(firstRank);
        uint32 best = 0;
        for (uint32 id = firstRank; id; id = sSpellMgr->GetNextSpellInChain(id))
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info)
                break;
            uint32 const required = info->SpellLevel ? info->SpellLevel : info->BaseLevel;
            if (required > level && best)
                break;
            best = id; // the first rank is always given, even below its normal level
        }
        return best;
    }

    // Teaches every owned rune's spells at the right rank for the character's level. Never lowers a
    // rank the character already has from elsewhere.
    void SyncRuneSpells(Player* player, uint32 runes)
    {
        for (RuneDef const& rune : RUNES)
        {
            if (!(runes & rune.Bit))
                continue;
            for (uint32 first : rune.Spells)
            {
                if (!first)
                    continue;
                uint32 const wanted = rune.FullRank ? sSpellMgr->GetLastSpellInChain(first)
                                                    : RankForLevel(first, player->GetLevel());
                if (!wanted)
                    continue;
                uint32 const known = HighestKnown(player, first);
                SpellInfo const* wantedInfo = sSpellMgr->GetSpellInfo(wanted);
                SpellInfo const* knownInfo = known ? sSpellMgr->GetSpellInfo(known) : nullptr;
                if (knownInfo && wantedInfo && knownInfo->GetRank() >= wantedInfo->GetRank())
                    continue;
                player->learnSpell(wanted);
            }
        }
    }

    // Enemies around the player: hostile or already fighting it, attackable, in line of sight.
    std::vector<Unit*> EnemiesAround(Player* player, float radius, WorldObject* center = nullptr)
    {
        if (!center)
            center = player;
        std::list<Unit*> found;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(center, player, radius);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(center, found, check);
        Cell::VisitObjects(center, searcher, radius);

        std::vector<Unit*> enemies;
        // Like a normal area spell: anything the player could attack (hostile or neutral), never friends.
        for (Unit* target : found)
            if (player->IsValidAttackTarget(target) && center->IsWithinLOSInMap(target))
                enemies.push_back(target);
        return enemies;
    }

    // Casts a spell for a rune without leaving it on cooldown. A cooldown the player already had from
    // their own cast is left alone.
    void CastWithoutCooldown(Unit* caster, Unit* target, uint32 spell)
    {
        Player* player = caster->ToPlayer();
        bool const hadCooldown = player && player->HasSpellCooldown(spell);
        caster->CastSpell(target, spell, true);
        if (player && !hadCooldown && player->HasSpellCooldown(spell))
            player->RemoveSpellCooldown(spell, true);
    }

    // Sends a visual-only "spell went off" message, the same packet the core sends for a real cast,
    // so clients draw the spell's arcs from the caster through the targets in order. Nothing is cast
    // on the server, so there is no damage, cost, cooldown or combat log from it.
    void SendVisualSpellGo(Player* caster, uint32 spellId, std::vector<Unit*> const& targets)
    {
        if (!spellId || targets.empty() || targets.size() > 255)
            return;

        WorldPacket data(SMSG_SPELL_GO, 40 + targets.size() * 8);
        data << caster->GetPackGUID();
        data << caster->GetPackGUID();
        data << uint8(0);                                           // cast count
        data << uint32(spellId);
        data << uint32(CAST_FLAG_UNKNOWN_9 | CAST_FLAG_PENDING | CAST_FLAG_NO_GCD);
        data << uint32(GameTime::GetGameTimeMS().count());
        data << uint8(targets.size());                              // hits
        for (Unit* target : targets)
            data << target->GetGUID();
        data << uint8(0);                                           // misses
        data << uint32(TARGET_FLAG_UNIT);
        data << targets.front()->GetPackGUID();
        caster->SendMessageToSet(&data, true);
    }

    void UpdateConduit(Player* player, LreData* data, uint32 diff)
    {
        AuraEffect* shield = player->GetAuraEffect(SPELL_AURA_PROC_TRIGGER_SPELL, SPELLFAMILY_SHAMAN,
            LIGHTNING_SHIELD_FAMILY_FLAG, 0, 0, player->GetGUID());
        if (!shield)
        {
            data->ConduitTimer = 0;
            return;
        }

        // Never expires, never runs out of charges (covers a shield cast before the rune was granted).
        Aura* aura = shield->GetBase();
        if (aura->GetMaxDuration() != -1)
        {
            aura->SetMaxDuration(-1);
            aura->SetDuration(-1);
        }
        uint8 const maxCharges = aura->CalcMaxCharges();
        if (aura->GetCharges() < maxCharges)
            aura->SetCharges(maxCharges);

        data->ConduitTimer += diff;
        if (data->ConduitTimer < CONDUIT_INTERVAL_MS)
            return;
        data->ConduitTimer = 0;

        if (!player->IsAlive())
            return;

        uint32 const damageSpell = sSpellMgr->GetSpellWithRank(LIGHTNING_SHIELD_DAMAGE_R1, shield->GetSpellInfo()->GetRank());
        if (!damageSpell)
            return;

        std::vector<Unit*> valid = EnemiesAround(player, CONDUIT_RADIUS);

        std::sort(valid.begin(), valid.end(), [player](Unit* a, Unit* b)
            { return player->GetExactDist2dSq(a) < player->GetExactDist2dSq(b); });
        if (valid.size() > CONDUIT_MAX_TARGETS)
            valid.resize(CONDUIT_MAX_TARGETS);

        if (valid.empty())
            return;

        SendVisualSpellGo(player, ConduitArcSpell.load(), valid);
        if (uint32 kit = ConduitSelfKit.load())
            player->SendPlaySpellVisual(kit);
        for (Unit* target : valid)
        {
            player->CastSpell(target, damageSpell, true);
            if (uint32 kit = ConduitTargetKit.load())
                target->SendPlaySpellVisual(kit);
        }
    }

    std::list<Player*> GroundViewers(DynamicObject* ground)
    {
        float const range = ground->GetVisibilityRange();
        std::list<Player*> viewers;
        Acore::AnyPlayerInObjectRangeCheck check(ground, range, false);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(ground, viewers, check);
        Cell::VisitObjects(ground, searcher, range);
        return viewers;
    }

    // Removes the ground glow from every client that can see it. The server keeps the area (it still
    // deals the damage and follows the caster); only the glow goes.
    void HideGround(DynamicObject* ground)
    {
        for (Player* viewer : GroundViewers(ground))
            ground->DestroyForPlayer(viewer);
    }

    // Removes the ground effect from every client that can see it and sends it again at its current
    // position, so the glow is redrawn (with its spawn pulse) where the area now is.
    void RedrawGround(DynamicObject* ground)
    {
        for (Player* viewer : GroundViewers(ground))
        {
            ground->DestroyForPlayer(viewer);
            ground->SendUpdateToPlayer(viewer);
        }
    }

    void UpdateHallowed(Player* player, LreData* data, uint32 diff)
    {
        data->FollowTimer += diff;
        data->PulseTimer += diff;
        if (data->FollowTimer < HALLOWED_FOLLOW_MS)
            return;
        data->FollowTimer = 0;

        DynamicObject* ground = player->GetDynObject(data->HallowedSpell);
        if (!ground || ground->GetMap() != player->GetMap())
        {
            data->HallowedSpell = 0;
            if (uint32 buff = HallowedBuffSpell.load())
                player->RemoveAurasDueToSpell(buff);
            return;
        }

        // The area follows the caster but keeps the facing it was cast with.
        if (ground->GetExactDist2d(player) >= HALLOWED_MIN_MOVE)
            player->GetMap()->DynamicObjectRelocation(ground, player->GetPositionX(), player->GetPositionY(),
                player->GetPositionZ(), ground->GetOrientation());

        // Once a second, in step with Consecration's own damage tick: the pulse plays at the caster's
        // feet, and the ground glow is either hidden (default) or redrawn where the area now is.
        if (data->PulseTimer < HALLOWED_PULSE_MS)
            return;
        data->PulseTimer = 0;
        if (HallowedGround.load())
            RedrawGround(ground);
        else
            HideGround(ground);
        if (uint32 kit = HallowedPulseKit.load())
            player->SendPlaySpellVisual(kit);
    }

    // A full-length Hurricane at the player's feet without the player channelling it: an invisible
    // world trigger channels it, and because the caster is the world trigger the core makes the
    // player the owner of the ground effect (Spell::EffectPersistentAA), so the damage is the
    // player's own (spell power, credit, threat). Several can be active at once.
    void SpawnHurricane(Player* player)
    {
        uint32 hurricane = HighestKnown(player, SPELL_HURRICANE_R1);
        if (!hurricane)
            hurricane = SPELL_HURRICANE_R1;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(hurricane);
        if (!info)
            return;
        int32 duration = info->GetMaxDuration();
        if (duration <= 0)
            duration = 10000;

        TempSummon* trigger = player->SummonCreature(WORLD_TRIGGER, player->GetPosition(), TEMPSUMMON_TIMED_DESPAWN,
            uint32(duration) + 2000);
        if (!trigger)
            return;
        trigger->SetFaction(player->GetFaction());
        trigger->SetLevel(player->GetLevel());
        trigger->CastSpell(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), hurricane, true,
            nullptr, nullptr, player->GetGUID());

        LreData::Storm storm;
        storm.Anchor = trigger->GetGUID();
        for (SpellEffectInfo const& effect : info->GetEffects())
        {
            if (effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL && effect.TriggerSpell)
            {
                storm.DamageSpell = effect.TriggerSpell;
                if (effect.Amplitude)
                    storm.Interval = effect.Amplitude;
            }
            if (effect.Effect == SPELL_EFFECT_PERSISTENT_AREA_AURA)
                if (float radius = effect.CalcRadius(player); radius > 0.0f)
                    storm.Radius = radius;
        }
        if (!storm.DamageSpell)
            return;
        storm.TicksLeft = uint32(duration) / storm.Interval;
        if (LreData* data = FindData(player))
            data->Storms.push_back(storm);
    }

    void DealRuneDamage(Player* player, Unit* target, SpellInfo const* info, uint32 schoolMask, uint32 amount)
    {
        if (!info || !amount)
            return;
        SpellNonMeleeDamage damage(player, target, info, schoolMask);
        player->CalculateSpellDamageTaken(&damage, int32(amount), info);
        Unit::DealDamageMods(damage.target, damage.damage, &damage.absorb);
        player->SendSpellNonMeleeDamageLog(&damage);
        player->DealSpellDamage(&damage, true);
    }

    // The gale's amount comes from a Thunder Clap hit that was already scaled, so it is not scaled again.
    void DealGaleDamage(Player* player, Unit* target, uint32 amount);

    // Hurricane ticks: the rank's own damage spell, with the player's spell power, on every enemy in
    // the storm's radius.
    void UpdateStorms(Player* player, LreData* data, uint32 diff)
    {
        for (LreData::Storm& storm : data->Storms)
        {
            storm.Timer += diff;
            if (storm.Timer < storm.Interval || !storm.TicksLeft)
                continue;
            storm.Timer = 0;
            --storm.TicksLeft;
            Creature* anchor = ObjectAccessor::GetCreature(*player, storm.Anchor);
            SpellInfo const* info = sSpellMgr->GetSpellInfo(storm.DamageSpell);
            if (!anchor || !info || anchor->GetMap() != player->GetMap())
            {
                storm.TicksLeft = 0;
                continue;
            }
            int32 const base = info->Effects[EFFECT_0].CalcValue(player);
            if (base <= 0)
                continue;
            for (Unit* target : EnemiesAround(player, storm.Radius, anchor))
            {
                uint32 amount = player->SpellDamageBonusDone(target, info, uint32(base), SPELL_DIRECT_DAMAGE, EFFECT_0);
                amount = target->SpellDamageBonusTaken(player, info, amount, SPELL_DIRECT_DAMAGE);
                DealRuneDamage(player, target, info, info->GetSchoolMask(), amount);
            }
        }
        data->Storms.erase(std::remove_if(data->Storms.begin(), data->Storms.end(),
            [](LreData::Storm const& storm) { return !storm.TicksLeft; }), data->Storms.end());
    }

    void UpdateGales(Player* player, LreData* data, uint32 diff)
    {
        for (LreData::Gale& gale : data->Gales)
        {
            gale.Timer += diff;
            if (gale.Timer < GALE_INTERVAL_MS || !gale.PulsesLeft)
                continue;
            gale.Timer = 0;
            --gale.PulsesLeft;
            if (!player->IsAlive())
                continue;
            uint32 const amount = uint32(gale.Damage * GALE_DAMAGE_PCT / 100.0f);
            for (Unit* target : EnemiesAround(player, gale.Radius))
                DealGaleDamage(player, target, amount);
            if (uint32 kit = GaleKit.load())
                player->SendPlaySpellVisual(kit);
        }
        data->Gales.erase(std::remove_if(data->Gales.begin(), data->Gales.end(),
            [](LreData::Gale const& gale) { return !gale.PulsesLeft; }), data->Gales.end());
    }

    // Walking Decay: Death and Decay follows the death knight; its own ground is redrawn at the new
    // spot every second so it is seen where it actually is.
    void UpdateDecay(Player* player, LreData* data, uint32 diff)
    {
        data->DecayFollowTimer += diff;
        data->DecayRedrawTimer += diff;
        if (data->DecayFollowTimer < HALLOWED_FOLLOW_MS)
            return;
        data->DecayFollowTimer = 0;
        DynamicObject* ground = player->GetDynObject(data->DecaySpell);
        if (!ground || ground->GetMap() != player->GetMap())
        {
            data->DecaySpell = 0;
            return;
        }
        if (ground->GetExactDist2d(player) >= HALLOWED_MIN_MOVE)
            player->GetMap()->DynamicObjectRelocation(ground, player->GetPositionX(), player->GetPositionY(),
                player->GetPositionZ(), ground->GetOrientation());
        if (data->DecayRedrawTimer < HALLOWED_PULSE_MS)
            return;
        data->DecayRedrawTimer = 0;
        // Same as Hallowed Path: the ground left at the cast spot is hidden (or redrawn at the feet with
        // .lre ground on), and a pulse plays at the death knight's feet.
        if (HallowedGround.load())
            RedrawGround(ground);
        else
            HideGround(ground);
        if (uint32 kit = DecayPulseKit.load())
            player->SendPlaySpellVisual(kit);
    }

    void DescribeSpell(ChatHandler* handler, SpellInfo const* info)
    {
        handler->PSendSysMessage("{} (id {}) rank {} family {} flags {:08X} {:08X} {:08X} duration {} ms",
            info->SpellName[0] ? info->SpellName[0] : "?", info->Id, info->GetRank(), info->SpellFamilyName,
            info->SpellFamilyFlags[0], info->SpellFamilyFlags[1], info->SpellFamilyFlags[2], info->GetMaxDuration());
        for (SpellEffectInfo const& effect : info->GetEffects())
        {
            if (!effect.Effect)
                continue;
            handler->PSendSysMessage("  effect {}: type {} aura {} amplitude {} trigger {} radius {:.1f}",
                effect.EffectIndex, effect.Effect, uint32(effect.ApplyAuraName), effect.Amplitude,
                effect.TriggerSpell, effect.CalcRadius());
        }
    }
}

// Lightning Shield, every rank: with the rune its charges are never spent and it never expires.
// ---------------------------------------------------------------------------------------------
// Level scaling for rune spells.
// Runes can give a spell rank above the character's level (rank 1 is always given, and at 60 the top
// rank). Damage from those ranks is scaled to what the spell's rank ladder gives at the character's
// level: between two ranks it is interpolated, below rank 1 it falls off in proportion to level, and
// at or above the top rank's level it is full. A rank at or below the character's level is never
// changed (the factor is capped at 1). Every rune's spells are included automatically, so future
// runes are covered as soon as they are added to RUNES.
// ---------------------------------------------------------------------------------------------
namespace
{
    struct ScaleChain
    {
        std::vector<std::pair<uint32, float>> Ranks; // (rank level, rank base damage)
    };
    std::vector<ScaleChain> ScaleChains;
    std::unordered_map<uint32, std::pair<std::size_t, std::size_t>> ScaleBySpell; // spell -> (chain, rank)
    thread_local bool SkipScaling = false;

    // The base damage of a rank, following trigger spells (Lightning Shield, Hurricane). Every damage
    // spell met on the way is recorded so its damage is scaled with the rank it belongs to.
    float RankDamage(SpellInfo const* info, std::vector<uint32>& damageSpells, int depth = 0)
    {
        if (!info || depth > 2)
            return 0.0f;
        for (SpellEffectInfo const& effect : info->GetEffects())
        {
            if (effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE ||
                ((effect.Effect == SPELL_EFFECT_APPLY_AURA || effect.Effect == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                 effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE))
            {
                damageSpells.push_back(info->Id);
                int32 const value = effect.CalcValue();
                if (value > 0)
                    return float(value);
            }
            if ((effect.ApplyAuraName == SPELL_AURA_PROC_TRIGGER_SPELL || effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL) &&
                effect.TriggerSpell)
                if (float value = RankDamage(sSpellMgr->GetSpellInfo(effect.TriggerSpell), damageSpells, depth + 1); value > 0.0f)
                    return value;
        }
        return 0.0f;
    }

    void BuildScaling()
    {
        ScaleChains.clear();
        ScaleBySpell.clear();
        std::vector<uint32> firsts;
        for (RuneDef const& rune : RUNES)
            for (uint32 first : rune.Spells)
                if (first && std::find(firsts.begin(), firsts.end(), first) == firsts.end())
                    firsts.push_back(first);

        for (uint32 first : firsts)
        {
            ScaleChain chain;
            std::vector<std::vector<uint32>> spellsPerRank;
            bool usable = true;
            for (uint32 id = first; id; id = sSpellMgr->GetNextSpellInChain(id))
            {
                SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
                if (!info)
                    break;
                std::vector<uint32> damageSpells{ id };
                float const base = RankDamage(info, damageSpells);
                uint32 const level = info->SpellLevel ? info->SpellLevel : info->BaseLevel;
                if (base <= 0.0f || !level)
                {
                    usable = false; // weapon-based or levelless spells (Crusader Strike, Divine Storm)
                    break;
                }
                chain.Ranks.emplace_back(level, base);
                spellsPerRank.push_back(damageSpells);
            }
            if (!usable || chain.Ranks.empty())
                continue;
            std::size_t const index = ScaleChains.size();
            ScaleChains.push_back(chain);
            for (std::size_t rank = 0; rank < spellsPerRank.size(); ++rank)
                for (uint32 spell : spellsPerRank[rank])
                    ScaleBySpell.emplace(spell, std::make_pair(index, rank));
        }
    }

    float ExpectedDamage(ScaleChain const& chain, uint8 level)
    {
        auto const& ranks = chain.Ranks;
        if (level <= ranks.front().first)
            return ranks.front().second * float(std::max<uint8>(level, 1)) / float(ranks.front().first);
        for (std::size_t i = 1; i < ranks.size(); ++i)
            if (level <= ranks[i].first)
            {
                float const t = float(level - ranks[i - 1].first) / float(ranks[i].first - ranks[i - 1].first);
                return ranks[i - 1].second + t * (ranks[i].second - ranks[i - 1].second);
            }
        return ranks.back().second;
    }

    template <typename T>
    void ScaleRuneDamage(Unit* attacker, T& damage, SpellInfo const* spellInfo)
    {
        if (SkipScaling || !attacker || !spellInfo || damage <= 0)
            return;
        Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!player)
            return;
        auto const found = ScaleBySpell.find(spellInfo->Id);
        if (found == ScaleBySpell.end())
            return;
        ScaleChain const& chain = ScaleChains[found->second.first];
        float const rankBase = chain.Ranks[found->second.second].second;
        float const factor = std::min(1.0f, ExpectedDamage(chain, player->GetLevel()) / rankBase);
        if (factor < 1.0f)
            damage = T(std::max(1.0f, float(damage) * factor));
    }
}

namespace
{
    void DealGaleDamage(Player* player, Unit* target, uint32 amount)
    {
        SkipScaling = true;
        DealRuneDamage(player, target, sSpellMgr->GetSpellInfo(SPELL_THUNDER_CLAP_R1), SPELL_SCHOOL_MASK_NATURE, amount);
        SkipScaling = false;
    }
}

class coa_lre_scaling : public UnitScript
{
public:
    coa_lre_scaling() : UnitScript("coa_lre_scaling", true,
        { UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN, UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK }) { }

    void ModifySpellDamageTaken(Unit* /*target*/, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        ScaleRuneDamage(attacker, damage, spellInfo);
    }

    void ModifyPeriodicDamageAurasTick(Unit* /*target*/, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
    {
        ScaleRuneDamage(attacker, damage, spellInfo);
    }
};

class spell_coa_lre_eternal_conduit : public AuraScript
{
    PrepareAuraScript(spell_coa_lre_eternal_conduit);

    // Lightning Shield must only answer hits taken. On this server it can also proc on the wearer's
    // own attacks, and the core's handler then makes the struck enemy "cast" the shield back at the
    // wearer (the "Young Wolf's Lightning Shield hits you" lines). Procs from the wearer's own actions
    // are refused for everyone with Lightning Shield, rune or not.
    bool HandleCheckProc(ProcEventInfo& eventInfo)
    {
        return eventInfo.GetActor() != GetTarget();
    }

    // Charges are spent normally (and the 3.5 s proc cooldown kept), then refilled before the core
    // would remove the shield for running out.
    void HandleAfterProc(ProcEventInfo& /*eventInfo*/)
    {
        if (!HasRune(GetCaster(), RUNE_ETERNAL_CONDUIT))
            return;
        uint8 const maxCharges = GetAura()->CalcMaxCharges();
        if (GetAura()->GetCharges() < maxCharges)
            GetAura()->SetCharges(maxCharges);
    }

    void HandleAfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (!HasRune(GetCaster(), RUNE_ETERNAL_CONDUIT))
            return;
        GetAura()->SetMaxDuration(-1);
        GetAura()->SetDuration(-1);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_coa_lre_eternal_conduit::HandleCheckProc);
        AfterProc += AuraProcFn(spell_coa_lre_eternal_conduit::HandleAfterProc);
        AfterEffectApply += AuraEffectApplyFn(spell_coa_lre_eternal_conduit::HandleAfterApply, EFFECT_0,
            SPELL_AURA_PROC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};

// Consecration, every rank: with the rune its ground area follows the caster.
class spell_coa_lre_hallowed_path : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_hallowed_path);

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!HasRune(caster, RUNE_HALLOWED_PATH))
            return;
        LreData* data = FindData(caster->ToPlayer());
        data->HallowedSpell = GetSpellInfo()->Id;
        data->FollowTimer = 0;
        if (uint32 buff = HallowedBuffSpell.load())
        {
            Player* player = caster->ToPlayer();
            player->RemoveAurasDueToSpell(buff);
            if (Aura* aura = player->AddAura(buff, player))
            {
                int32 const duration = GetSpellInfo()->GetMaxDuration();
                if (duration > 0)
                {
                    aura->SetMaxDuration(duration);
                    aura->SetDuration(duration);
                }
            }
        }
        data->PulseTimer = HALLOWED_PULSE_MS; // first redraw on the next follow tick
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_coa_lre_hallowed_path::HandleAfterCast);
    }
};

// Divine Storm: with the rune, every enemy it hits also takes a Crusader Strike.
class spell_coa_lre_crusading_storm : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_crusading_storm);

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target || !HasRune(caster, RUNE_CRUSADING_STORM))
            return;
        CastWithoutCooldown(caster, target, SPELL_CRUSADER_STRIKE);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_coa_lre_crusading_storm::HandleAfterHit);
    }
};

// Crusader Strike: with the rune, its target is engulfed in the caster's highest Holy Fire.
class spell_coa_lre_purifying_fires : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_purifying_fires);

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target || !HasRune(caster, RUNE_PURIFYING_FIRES))
            return;
        uint32 holyFire = HighestKnown(caster->ToPlayer(), SPELL_HOLY_FIRE_R1);
        if (!holyFire)
            holyFire = SPELL_HOLY_FIRE_R1;
        CastWithoutCooldown(caster, target, holyFire);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_coa_lre_purifying_fires::HandleAfterHit);
    }
};

// Thunder Clap, every rank: with the rune it drops a full Hurricane at the caster's feet and starts
// a gale of Nature pulses scaled from the Thunder Clap's own damage.
class spell_coa_lre_thunderous_gale : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_thunderous_gale);

    bool _galeStarted = false;
    std::size_t _gale = 0;

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (HasRune(caster, RUNE_THUNDEROUS_GALE))
            SpawnHurricane(caster->ToPlayer());
    }

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        if (!GetHitUnit() || !HasRune(caster, RUNE_THUNDEROUS_GALE))
            return;
        int32 const hit = GetHitDamage();
        if (hit <= 0)
            return;
        LreData* data = FindData(caster->ToPlayer());
        if (!_galeStarted)
        {
            LreData::Gale gale;
            gale.Damage = uint32(hit);
            float const radius = GetSpellInfo()->Effects[EFFECT_0].CalcRadius(caster);
            if (radius > 0.0f)
                gale.Radius = radius;
            data->Gales.push_back(gale);
            _gale = data->Gales.size() - 1;
            _galeStarted = true;
        }
        else if (_gale < data->Gales.size())
            data->Gales[_gale].Damage = std::max(data->Gales[_gale].Damage, uint32(hit));
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_coa_lre_thunderous_gale::HandleAfterCast);
        AfterHit += SpellHitFn(spell_coa_lre_thunderous_gale::HandleAfterHit);
    }
};

namespace
{
    // Set while a rune is casting extra copies of a spell, so those copies never trigger the rune again.
    thread_local bool InRuneTrigger = false;

    struct RuneTriggerGuard
    {
        RuneTriggerGuard() { InRuneTrigger = true; }
        ~RuneTriggerGuard() { InRuneTrigger = false; }
    };

    std::vector<Unit*> OthersNear(Player* player, Unit* center, float radius, Unit* exclude, std::size_t cap)
    {
        std::vector<Unit*> found = EnemiesAround(player, radius, center);
        found.erase(std::remove(found.begin(), found.end(), exclude), found.end());
        std::sort(found.begin(), found.end(), [center](Unit* a, Unit* b)
            { return center->GetExactDist2dSq(a) < center->GetExactDist2dSq(b); });
        if (cap && found.size() > cap)
            found.resize(cap);
        return found;
    }
}

// Earth, Flame and Frost Shock, every rank.
//   Arcing Shocks: the shock also hits the 2 closest enemies within 10 yards of its target, with arcs.
//   Spreading Shocks: around its target (8 yards) Flame Shock spreads, Frost Shock slows and Earth
//   Shock interrupts. Both runes can be active together.
class spell_coa_lre_shocks : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_shocks);

    void HandleAfterHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        Player* player = caster ? caster->ToPlayer() : nullptr;
        // Copies a rune casts are triggered; they must never trigger the rune again (some land later,
        // after any in-call guard is gone).
        if (InRuneTrigger || GetSpell()->IsTriggered() || !player || !target)
            return;
        uint32 const spell = GetSpellInfo()->Id;
        RuneTriggerGuard guard;

        if (HasRune(player, RUNE_ARCING_SHOCKS))
        {
            std::vector<Unit*> extra = OthersNear(player, target, 10.0f, target, 2);
            if (!extra.empty())
            {
                std::vector<Unit*> arc{ target };
                arc.insert(arc.end(), extra.begin(), extra.end());
                SendVisualSpellGo(player, ConduitArcSpell.load(), arc);
                for (Unit* unit : extra)
                    CastWithoutCooldown(player, unit, spell);
            }
        }

        if (HasRune(player, RUNE_SPREADING_SHOCKS))
        {
            uint32 const first = sSpellMgr->GetFirstSpellInChain(spell);
            for (Unit* unit : OthersNear(player, target, 8.0f, target, 0))
            {
                if (first == SPELL_FLAME_SHOCK_R1)
                    CastWithoutCooldown(player, unit, spell);
                else if (first == SPELL_FROST_SHOCK_R1)
                    player->AddAura(spell, unit);          // the slow only, no extra damage
                else if (first == SPELL_EARTH_SHOCK_R1)
                    unit->InterruptNonMeleeSpells(false);  // the interrupt only, no extra damage
            }
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_coa_lre_shocks::HandleAfterHit);
    }
};

// Stormstrike (and its weapon hits): Crashing Storm discharges 30% of each hit as Nature damage into
// every enemy within 8 yards, with arcs.
constexpr float CRASHING_STORM_PCT = 30.0f;
constexpr float CRASHING_STORM_RADIUS = 8.0f;

class spell_coa_lre_crashing_storm : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_crashing_storm);

    void HandleAfterHit()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        int32 const hit = GetHitDamage();
        if (!player || hit <= 0 || !HasRune(player, RUNE_CRASHING_STORM))
            return;
        std::vector<Unit*> enemies = EnemiesAround(player, CRASHING_STORM_RADIUS);
        if (enemies.empty())
            return;
        std::sort(enemies.begin(), enemies.end(), [player](Unit* a, Unit* b)
            { return player->GetExactDist2dSq(a) < player->GetExactDist2dSq(b); });
        SendVisualSpellGo(player, ConduitArcSpell.load(), enemies);
        uint32 const amount = uint32(hit * CRASHING_STORM_PCT / 100.0f);
        SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_STORMSTRIKE);
        for (Unit* unit : enemies)
            DealRuneDamage(player, unit, info, SPELL_SCHOOL_MASK_NATURE, amount);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_coa_lre_crashing_storm::HandleAfterHit);
    }
};

// Death and Decay, every rank: with Walking Decay it follows the death knight.
class spell_coa_lre_walking_decay : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_walking_decay);

    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!HasRune(caster, RUNE_WALKING_DECAY))
            return;
        LreData* data = FindData(caster->ToPlayer());
        data->DecaySpell = GetSpellInfo()->Id;
        data->DecayFollowTimer = 0;
        data->DecayRedrawTimer = HALLOWED_PULSE_MS; // hide the cast-spot ground on the next follow tick
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_coa_lre_walking_decay::HandleAfterCast);
    }
};

// Death Grip: with Mass Grip, up to 5 other enemies within 5 yards of the target are gripped too.
class spell_coa_lre_mass_grip : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_mass_grip);

    void HandleAfterHit()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        Unit* target = GetHitUnit();
        if (InRuneTrigger || GetSpell()->IsTriggered() || !player || !target || !HasRune(player, RUNE_MASS_GRIP))
            return;
        RuneTriggerGuard guard;
        for (Unit* unit : OthersNear(player, target, 5.0f, target, 5))
            CastWithoutCooldown(player, unit, SPELL_DEATH_GRIP);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_coa_lre_mass_grip::HandleAfterHit);
    }
};

// Searing Bolt (the Searing Totem's attack), every rank: with Searing Swarm the totem also fires at the
// 2 closest other enemies in its range.
class spell_coa_lre_searing_swarm : public SpellScript
{
    PrepareSpellScript(spell_coa_lre_searing_swarm);

    void HandleAfterHit()
    {
        Unit* totem = GetCaster();
        Unit* target = GetHitUnit();
        // Searing Bolt travels, so the extra bolts hit after this call returns: only the totem's own
        // (untriggered) bolts may fan out, or every extra bolt would fire two more, without end.
        if (InRuneTrigger || GetSpell()->IsTriggered() || !totem || !target)
            return;
        Player* owner = totem->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!owner || owner == totem || !HasRune(owner, RUNE_SEARING_SWARM))
            return;
        RuneTriggerGuard guard;
        uint32 const spell = GetSpellInfo()->Id;
        for (Unit* unit : OthersNear(owner, totem, 20.0f, target, 2))
            totem->CastSpell(unit, spell, true);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_coa_lre_searing_swarm::HandleAfterHit);
    }
};

// The rune items: using one grants its rune, teaches its spells and is used up.
class coa_lre_rune_item : public ItemScript
{
public:
    coa_lre_rune_item() : ItemScript("coa_lre_rune_item") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        RuneDef const* rune = RuneByItem(item->GetEntry());
        if (!rune)
            return false;

        // The client waits for an answer to the use request; give it while the item still exists.
        player->SendEquipError(EQUIP_ERR_NONE, item, nullptr);

        if (!GrantRune(player, rune->Bit))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("You already know the {}.", rune->Name);
            return true;
        }

        ChatHandler(player->GetSession()).PSendSysMessage("You have learned the {}.", rune->Name);
        // Last thing that touches the item: DestroyItemCount removes it.
        uint32 count = 1;
        player->DestroyItemCount(item, count, true);
        return true;
    }
};

// The reusable unlearning item: opens the unlearning menu, never used up.
class coa_lre_unlearn_item : public ItemScript
{
public:
    coa_lre_unlearn_item() : ItemScript("coa_lre_unlearn_item") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        player->SendEquipError(EQUIP_ERR_NONE, item, nullptr);
        ShowUnlearnMenu(player, item->GetGUID());
        return true;
    }

    void OnGossipSelect(Player* player, Item* /*item*/, uint32 sender, uint32 action) override
    {
        HandleUnlearnChoice(player, sender, action);
    }
};

// The unlearning NPC: the same menu as the item.
class npc_coa_lre_unlearner : public CreatureScript
{
public:
    npc_coa_lre_unlearner() : CreatureScript("npc_coa_lre_unlearner") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ShowUnlearnMenu(player, creature->GetGUID(), true);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (sender == GOSSIP_SENDER_VENDOR)
        {
            CloseGossipMenuFor(player);
            player->GetSession()->SendListInventory(creature->GetGUID());
            return true;
        }
        HandleUnlearnChoice(player, sender, action);
        return true;
    }
};

// Adaptive Training Dummy (9900110): the stock training dummy (takes no damage, leaves combat 5 s after
// the last hit) that changes its level to the level of whoever attacks it, so hit, crit and resist
// chances are always those of an even-level target.
constexpr uint32 NPC_ADAPTIVE_DUMMY = 9900110;
constexpr char const* DUMMY_KEY = "coa_lre_dummy";

struct AdaptiveDummyLevel : public DataMap::Base
{
    uint8 Level = 1;
};

struct npc_coa_lre_adaptive_dummy : public NullCreatureAI
{
    explicit npc_coa_lre_adaptive_dummy(Creature* creature) : NullCreatureAI(creature) { }

    void ScaleTo(Unit* attacker)
    {
        Player* player = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player || player->GetLevel() == me->GetLevel())
            return;
        me->CustomData.GetDefault<AdaptiveDummyLevel>(DUMMY_KEY)->Level = player->GetLevel();
        me->SelectLevel(true); // level, health, armor and damage for the new level
    }

    void JustEnteredCombat(Unit* who) override
    {
        ScaleTo(who);
        _combatTimer[who->GetGUID()] = 5s;
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType damageType, SpellSchoolMask) override
    {
        damage = 0;
        if (!attacker)
            return;
        ScaleTo(attacker);
        if (damageType == DOT)
            return;
        _combatTimer[attacker->GetGUID()] = 5s;
        if (Unit* owner = attacker->GetCharmerOrOwner())
            if (me->GetCombatManager().IsInCombatWith(owner))
                _combatTimer[owner->GetGUID()] = 5s;
    }

    void UpdateAI(uint32 diff) override
    {
        for (auto itr = _combatTimer.begin(); itr != _combatTimer.end();)
        {
            itr->second -= Milliseconds(diff);
            if (itr->second <= 0s)
            {
                auto const& pveRefs = me->GetCombatManager().GetPvECombatRefs();
                auto it = pveRefs.find(itr->first);
                if (it != pveRefs.end())
                    it->second->EndCombat();
                itr = _combatTimer.erase(itr);
            }
            else
                ++itr;
        }
    }

private:
    std::unordered_map<ObjectGuid, Milliseconds> _combatTimer;
};

// Makes the core build the dummy's stats for the level it was set to, not its template level.
class coa_lre_adaptive_dummy_level : public AllCreatureScript
{
public:
    coa_lre_adaptive_dummy_level() : AllCreatureScript("coa_lre_adaptive_dummy_level") { }

    void OnBeforeCreatureSelectLevel(CreatureTemplate const* cinfo, Creature* creature, uint8& level) override
    {
        if (cinfo->Entry != NPC_ADAPTIVE_DUMMY)
            return;
        if (AdaptiveDummyLevel* wanted = creature->CustomData.Get<AdaptiveDummyLevel>(DUMMY_KEY))
            level = wanted->Level;
    }
};

class coa_lre_player : public PlayerScript
{
public:
    coa_lre_player() : PlayerScript("coa_lre_player", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_LEVEL_CHANGED }) { }

    void OnPlayerLogin(Player* player) override
    {
        SendRuneItemRows(player);
        LoadRunes(player);
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (LreData* data = FindData(player))
            SyncRuneSpells(player, data->Runes);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        LreData* data = FindData(player);
        if (!data || !data->Runes || !player->IsInWorld())
            return;

        if (data->Runes & RUNE_ETERNAL_CONDUIT)
            UpdateConduit(player, data, diff);
        if ((data->Runes & RUNE_HALLOWED_PATH) && data->HallowedSpell)
            UpdateHallowed(player, data, diff);
        if (!data->Gales.empty())
            UpdateGales(player, data, diff);
        if (!data->Storms.empty())
            UpdateStorms(player, data, diff);
        if ((data->Runes & RUNE_WALKING_DECAY) && data->DecaySpell)
            UpdateDecay(player, data, diff);

        data->SyncTimer += diff;
        if (data->SyncTimer >= SPELL_SYNC_MS)
        {
            data->SyncTimer = 0;
            SyncRuneSpells(player, data->Runes);
        }
    }
};

class coa_lre_world : public WorldScript
{
public:
    coa_lre_world() : WorldScript("coa_lre_world", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        BuildScaling();
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `coa_lre_runes` ("
            "`guid` INT UNSIGNED NOT NULL, `rune` INT UNSIGNED NOT NULL, PRIMARY KEY (`guid`, `rune`)) "
            "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
        // Tables made by earlier versions used TINYINT, too small for the newer runes.
        CharacterDatabase.DirectExecute("ALTER TABLE `coa_lre_runes` MODIFY `rune` INT UNSIGNED NOT NULL");
    }
};

class coa_lre_commands : public CommandScript
{
public:
    coa_lre_commands() : CommandScript("coa_lre_commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable lreTable =
        {
            { "add",    HandleAdd,    SEC_GAMEMASTER, Console::No },
            { "remove", HandleRemove, SEC_GAMEMASTER, Console::No },
            { "list",   HandleList,   SEC_GAMEMASTER, Console::No },
            { "info",   HandleInfo,   SEC_GAMEMASTER, Console::No },
            { "kit",    HandleKit,    SEC_GAMEMASTER, Console::No },
            { "ground", HandleGround, SEC_GAMEMASTER, Console::No },
            { "findbuff", HandleFindBuff, SEC_GAMEMASTER, Console::No },
            { "scale",  HandleScale,  SEC_GAMEMASTER, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "lre", lreTable },
        };
        return commandTable;
    }

    static bool HandleAdd(ChatHandler* handler, std::string runeName)
    {
        uint32 rune = 0;
        if (!ParseRune(runeName, rune))
        {
            handler->SendSysMessage("Unknown rune. Use: hallowed, conduit, storm, gale, fires, arcing, spreading, crashing, decay, grip, searing, maelstrom or thassarian");
            return false;
        }
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;
        if (!GrantRune(target, rune))
            handler->PSendSysMessage("{} already has the {}.", target->GetName(), RuneName(rune));
        else
            handler->PSendSysMessage("{} granted to {}.", RuneName(rune), target->GetName());
        return true;
    }

    static bool HandleRemove(ChatHandler* handler, std::string runeName)
    {
        uint32 rune = 0;
        if (!ParseRune(runeName, rune))
        {
            handler->SendSysMessage("Unknown rune. Use: hallowed, conduit, storm, gale, fires, arcing, spreading, crashing, decay, grip, searing, maelstrom or thassarian");
            return false;
        }
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;
        if (!UnlearnRune(target, rune))
            handler->PSendSysMessage("{} does not have the {}.", target->GetName(), RuneName(rune));
        else
            handler->PSendSysMessage("{} removed from {}, with its spells.", RuneName(rune), target->GetName());
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;
        LreData* data = FindData(target);
        uint32 const runes = data ? data->Runes : 0;
        handler->PSendSysMessage("{}: runes", target->GetName());
        for (RuneDef const& rune : RUNES)
            handler->PSendSysMessage("  {} {}", (runes & rune.Bit) ? "ON " : "off", rune.Name);
        return true;
    }

    // .lre kit <conduit|self|hallowed> <SpellVisualKit id> or .lre kit arc <spell id>, 0 = none: try visuals
    // live, no rebuild.
    // Changes last until the worldserver restarts.
    static bool HandleKit(ChatHandler* handler, std::string slot, uint32 kit)
    {
        std::transform(slot.begin(), slot.end(), slot.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (slot == "conduit")
            ConduitTargetKit = kit;
        else if (slot == "self")
            ConduitSelfKit = kit;
        else if (slot == "hallowed")
            HallowedPulseKit = kit;
        else if (slot == "gale")
            GaleKit = kit;
        else if (slot == "decay")
            DecayPulseKit = kit;
        else if (slot == "buff")
            HallowedBuffSpell = kit; // a spell id here, not a kit id
        else if (slot == "arc")
            ConduitArcSpell = kit; // a spell id here, not a kit id
        else
        {
            handler->SendSysMessage("Use: .lre kit <conduit|self|hallowed|gale|decay> <kit id>, .lre kit <arc|buff> <spell id>, 0 = none");
            return false;
        }
        handler->PSendSysMessage("Kits now: conduit {}, self {}, hallowed {}, gale {}, decay {}, arc spell {}, buff spell {}",
            ConduitTargetKit.load(), ConduitSelfKit.load(), HallowedPulseKit.load(), GaleKit.load(), DecayPulseKit.load(),
            ConduitArcSpell.load(), HallowedBuffSpell.load());
        return true;
    }

    // .lre scale: for each scaled rune spell the target knows, the damage factor at their level.
    static bool HandleScale(ChatHandler* handler)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;
        uint32 shown = 0;
        for (auto const& [spell, where] : ScaleBySpell)
        {
            if (!target->HasSpell(spell))
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
            ScaleChain const& chain = ScaleChains[where.first];
            float const factor = std::min(1.0f, ExpectedDamage(chain, target->GetLevel()) / chain.Ranks[where.second].second);
            handler->PSendSysMessage("{} (id {}, rank {} of {}, rank level {}): {:.0f}% damage at level {}",
                info && info->SpellName[0] ? info->SpellName[0] : "?", spell, where.second + 1, chain.Ranks.size(),
                chain.Ranks[where.second].first, factor * 100.0f, target->GetLevel());
            ++shown;
        }
        if (!shown)
            handler->SendSysMessage("No scaled rune spell known.");
        return true;
    }

    // .lre findbuff [spell id]: lists harmless buff spells that share the given spell's icon
    // (default Consecration; 0 = any icon): positive, not passive, shown in the aura bar, no visual on
    // the character, every effect a dummy aura, and no script attached. Any of them can be shown as the
    // Hallowed Path tracker with .lre kit buff <id>.
    static bool HandleFindBuff(ChatHandler* handler, Optional<uint32> iconOf)
    {
        bool const anyIcon = iconOf && *iconOf == 0;
        SpellInfo const* source = sSpellMgr->GetSpellInfo(anyIcon ? SPELL_CONSECRATION_R1 : (iconOf ? *iconOf : SPELL_CONSECRATION_R1));
        if (!source)
        {
            handler->SendSysMessage("Unknown spell.");
            return false;
        }
        uint32 shown = 0;
        for (uint32 id = 1; id < sSpellMgr->GetSpellInfoStoreSize() && shown < 25; ++id)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info || (!anyIcon && info->SpellIconID != source->SpellIconID) || !info->IsPositive() || info->IsPassive())
                continue;
            // Must show as a normal buff and draw nothing on the character (no spotlights or beams).
            if (info->SpellVisual[0] || info->SpellVisual[1] || info->HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY) ||
                info->HasAttribute(SPELL_ATTR1_NO_AURA_ICON))
                continue;
            bool harmless = true;
            bool hasAura = false;
            for (SpellEffectInfo const& effect : info->GetEffects())
            {
                if (!effect.Effect)
                    continue;
                if (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_DUMMY)
                    hasAura = true;
                else
                    harmless = false;
            }
            auto const scripts = sObjectMgr->GetSpellScriptsBounds(id);
            if (!harmless || !hasAura || scripts.first != scripts.second)
                continue;
            handler->PSendSysMessage("{} - {} (duration {} ms, icon {})", id, info->SpellName[0] ? info->SpellName[0] : "?",
                info->GetMaxDuration(), info->SpellIconID);
            ++shown;
        }
        if (!shown)
            handler->SendSysMessage("No harmless buff spell with that icon was found.");
        return true;
    }

    // .lre ground <on|off>: show Consecration's own ground glow under Hallowed Path, or only the pulse.
    // Lasts until the worldserver restarts.
    static bool HandleGround(ChatHandler* handler, std::string state)
    {
        std::transform(state.begin(), state.end(), state.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (state != "on" && state != "off")
        {
            handler->SendSysMessage("Use: .lre ground <on|off>");
            return false;
        }
        HallowedGround = state == "on";
        handler->PSendSysMessage("Ground glow for Hallowed Path and Walking Decay: {}", HallowedGround.load() ? "on" : "off");
        return true;
    }

    // Prints the real spell data this server uses for the runes' spells.
    static bool HandleInfo(ChatHandler* handler)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            return false;
        uint32 shown = 0;
        for (auto const& [spellId, playerSpell] : target->GetSpellMap())
        {
            if (playerSpell->State == PLAYERSPELL_REMOVED)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (!info || !info->SpellName[0])
                continue;
            std::string const name = info->SpellName[0];
            if (name != "Consecration" && name != "Lightning Shield" && name != "Crusader Strike" &&
                name != "Divine Storm" && name != "Thunder Clap" && name != "Hurricane" && name != "Holy Fire")
                continue;
            DescribeSpell(handler, info);
            ++shown;
        }
        if (!shown)
            handler->SendSysMessage("This character knows none of the runes' spells.");
        return true;
    }
};

void AddSC_mod_coa_lre()
{
    RegisterSpellScript(spell_coa_lre_eternal_conduit);
    RegisterSpellScript(spell_coa_lre_hallowed_path);
    RegisterSpellScript(spell_coa_lre_crusading_storm);
    RegisterSpellScript(spell_coa_lre_purifying_fires);
    RegisterSpellScript(spell_coa_lre_thunderous_gale);
    RegisterSpellScript(spell_coa_lre_shocks);
    RegisterSpellScript(spell_coa_lre_crashing_storm);
    RegisterSpellScript(spell_coa_lre_walking_decay);
    RegisterSpellScript(spell_coa_lre_mass_grip);
    RegisterSpellScript(spell_coa_lre_searing_swarm);
    new coa_lre_rune_item();
    new coa_lre_scaling();
    new coa_lre_unlearn_item();
    new npc_coa_lre_unlearner();
    RegisterCreatureAI(npc_coa_lre_adaptive_dummy);
    new coa_lre_adaptive_dummy_level();
    new coa_lre_player();
    new coa_lre_world();
    new coa_lre_commands();
}
