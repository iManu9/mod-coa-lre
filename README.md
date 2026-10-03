# mod-coa-lre: Runic Enhancements for Conquest of Azeroth

An AzerothCore module that adds **Runic Enhancements (runes)** to a Conquest of Azeroth (CoA) server: items that change how existing spells and abilities work. It is written for CoA's AzerothCore fork and runs on both a normal CoA realm and a Wildcard (classless, level 60) realm. **No client patch is needed.**

Inspired by the Legendary Runic Enhancements on Grimfall. All code and values here are original.

## Runes

### Legendary
| Rune | Item | Effect | Teaches |
|---|---|---|---|
| Hallowed Path | 9900001 Sigil of the Wandering Sanctum | Consecration follows you; a holy pulse plays at your feet every second | Consecration |
| Eternal Conduit | 9900002 Heart of the Endless Storm | Lightning Shield never expires or runs out of charges; every 3 sec it hits up to 5 enemies within 8 yd, with Chain Lightning arcs | Lightning Shield |
| Righteous Crusading Storm | 9900003 Writ of the Crusading Tempest | Every enemy hit by Divine Storm also takes a Crusader Strike | Crusader Strike, Divine Storm |
| Thunderous Gale | 9900004 Thunderhead Runestone | Thunder Clap drops a full Hurricane at your feet (you keep moving) plus 3 Nature pulses at 30% of the Thunder Clap's damage; Hurricanes stack | Thunder Clap, Hurricane |
| Purifying Fires of the Silver Hand | 9900005 Ashes of the Silver Hand | Crusader Strike applies your highest Holy Fire | Crusader Strike, Holy Fire |
| Arcing Shocks | 9900011 Stormlinked Totem Shard | Each shock also hits the 2 closest enemies within 10 yd of its target, with arcs | Earth, Flame, Frost Shock |
| Spreading Shocks | 9900012 Wildfire Totem Shard | Around the target (8 yd): Flame Shock spreads, Frost Shock slows, Earth Shock interrupts | Earth, Flame, Frost Shock |
| Crashing Storm | 9900013 Eye of the Crashing Storm | Each Stormstrike hit discharges 30% of its damage as Nature into every enemy within 8 yd, with arcs | Stormstrike |
| Walking Decay | 9900014 Shroud of the Walking Rot | Death and Decay follows you, with a pulse at your feet | Death and Decay |
| Mass Grip | 9900015 Chains of the Lich | Death Grip also pulls up to 5 enemies within 5 yd of the target | Death Grip |
| Searing Swarm | 9900016 Ember of the Searing Swarm | Searing Totem also fires at the 2 closest other enemies in range | Searing Totem |

### Epic (talents as runes, always full rank)
| Rune | Item | Grants |
|---|---|---|
| Maelstrom Weapon | 9900017 Codex of the Maelstrom | Maelstrom Weapon 5/5 |
| Threat of Thassarian | 9900018 Codex of Thassarian | Threat of Thassarian 3/3 |

## How runes work
- **Using a rune item** grants the rune (stored per character) and teaches the spells it needs. The item is used up.
- **Ranks follow your level.** Rank 1 is always given; higher ranks are learned automatically as you level; from level 60 the top rank is taught.
- **Level scaling.** Damage from a rune spell's ranks follows the spell's own rank ladder at your level (below rank 1's level it falls off in proportion; at 60 the top rank does what the level-60 rank would; full damage at 80). It only ever lowers damage, never raises it. New runes are covered automatically.
- **Spells a rune fires** (for example Crusader Strike from Divine Storm) never put that spell on cooldown, and copies a rune casts never trigger runes again.
- **Area runes** hit anything you could attack, hostile or neutral, never friends.
- **No limit** on runes per character yet (a tier and slot system is planned).

## Items and NPCs
| ID | What |
|---|---|
| 9900010 | Runebreaker's Chisel: reusable; opens a menu to unlearn a rune |
| 9900100 | Runekeeper Vaelith: sells every rune (Legendary 1,000 gold, Epic 250 gold) and unlearns them |
| 9900110 | Adaptive Training Dummy: takes its attacker's level, never dies |

Unlearning removes the rune and every rank of its spells, unless another rune you still own needs them. No refund.

## GM commands
| Command | Does |
|---|---|
| `.lre add <rune>` / `.lre remove <rune>` | Grant or unlearn a rune (names: hallowed, conduit, storm, gale, fires, arcing, spreading, crashing, decay, grip, searing, maelstrom, thassarian) |
| `.lre list` | Runes the selected player owns |
| `.lre info` | Server data for the runes' spells (effects, auras, triggers) |
| `.lre scale` | Damage scaling at the player's level for each rune spell they know |
| `.lre kit <conduit\|self\|hallowed\|gale\|decay> <kit id>` | Swap a visual live (`0` = none) |
| `.lre kit <arc\|buff> <spell id>` | Swap the lightning-arc spell or the Hallowed Path tracking buff |
| `.lre ground on\|off` | Show or hide the real ground glow under Hallowed Path and Walking Decay |
| `.lre findbuff [spell id]` | List harmless buff spells usable as a tracker (`0` = any icon) |

Visual settings reset when the server restarts.

## Other changes in this module
- **Lightning Shield** no longer procs from the wearer's own attacks (on this core the struck enemy then "cast" the shield back at the wearer). Applies to everyone.
- **Custom item icons**: the module sends Item.dbc rows for its items through CoA's client packet `SMSG_PATCH_ITEM` (0x932), so the icons show without a client patch. Needs CoA's client `Extensions.dll`.

## Requirements
- CoA's AzerothCore fork. Built and tested against `Corfirean/azerothcore-wotlk-coa` at `df5bcdf260924fd9f5f178ffc6075bec5dc206ed`.
- The CoA game client with its `Extensions.dll` (for item icons).
- Items are copied from CoA's Class Change Potion (97858); NPCs from Silas Darkmoon (14823) and the Grandmaster's Training Dummy (31144). These must exist in the world database.

## Install
1. Copy this folder into your source tree as `modules/mod-coa-lre`.
2. Rebuild the worldserver (see [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md)).
3. Run `sql/mod_coa_lre_world.sql` on **each** world database (it is safe to run again).
4. Start the worldserver. The characters table `coa_lre_runes` is created automatically.

## Docs
- [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md): building CoA from source on Windows, including the module.
- [docs/WILDCARD-REALM.md](docs/WILDCARD-REALM.md): adding a Wildcard realm next to a CoA realm.
- [docs/ideas/](docs/ideas/): rune idea lists (126 Legendary ideas; Common, Rare and Epic tier lists).
- [CHANGELOG.md](CHANGELOG.md)

## License
GNU Affero General Public License v3.0 or later. See [LICENSE](LICENSE).
