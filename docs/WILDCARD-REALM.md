# Adding a Wildcard realm next to a CoA realm

CoA's core already contains Wildcard (classless). A Wildcard realm is a **second worldserver** with its own world and characters databases, sharing the login database. Both realms use the same `worldserver.exe`, so modules such as mod-coa-lre work on both.

## 1. Databases (cmd)
Copy the world database in full, and the characters database as **structure only** (a fresh realm), plus the tables that hold server data rather than characters. Without those the worldserver stops at start-up (for example `active_arena_season can't be empty`).
```bat
set "M=<server>\mysql" & set "INI=--defaults-extra-file=<server>\mysql\admin-client.ini"
"%M%\bin\mysql.exe" "%INI%" -h 127.0.0.1 -P 3307 -e "CREATE DATABASE acore_world_wildcard; CREATE DATABASE acore_characters_wildcard; GRANT ALL PRIVILEGES ON acore_world_wildcard.* TO 'acore'@'127.0.0.1', 'acore'@'localhost'; GRANT ALL PRIVILEGES ON acore_characters_wildcard.* TO 'acore'@'127.0.0.1', 'acore'@'localhost'; FLUSH PRIVILEGES;"
"%M%\bin\mysqldump.exe" "%INI%" -h 127.0.0.1 -P 3307 --single-transaction --routines --triggers --events --hex-blob acore_world | "%M%\bin\mysql.exe" "%INI%" -h 127.0.0.1 -P 3307 acore_world_wildcard
"%M%\bin\mysqldump.exe" "%INI%" -h 127.0.0.1 -P 3307 --no-data --routines --triggers acore_characters | "%M%\bin\mysql.exe" "%INI%" -h 127.0.0.1 -P 3307 acore_characters_wildcard
for %t in (updates updates_include worldstates active_arena_season addons instance_reset pool_quest_save warden_action world_state mail_server_template mail_server_template_items mail_server_template_conditions) do @("%M%\bin\mysqldump.exe" "%INI%" -h 127.0.0.1 -P 3307 --no-create-info --hex-blob acore_characters %t | "%M%\bin\mysql.exe" "%INI%" -h 127.0.0.1 -P 3307 acore_characters_wildcard)
```

## 2. Configs
Copy the CoA realm's `configs` folder to a new folder, for example `<server>\Wildcard\configs`, and copy `src\server\coa\conf\wildcard.conf.dist` from the source tree to `configs\modules\wildcard.conf`. Module configs are read from `configs\modules` **relative to the folder the worldserver starts in**, so the realm needs its own folder.

`worldserver.conf`:
| Setting | Value |
|---|---|
| `WorldDatabaseInfo` / `CharacterDatabaseInfo` | point at `acore_world_wildcard` / `acore_characters_wildcard` |
| `RealmID` | `2` |
| `WorldServerPort` | `8086` |
| `LogsDir` | the realm's own `Logs` folder |
| `Ra.Enable` | `0` (or another port) |
| `PlayerStart.CustomSpells` | `1` |
| `AlwaysMaxSkillForLevel` | `1` |
| `MaxPlayerLevel` | `60` |

Module configs (from the header of `wildcard.conf.dist`):
| File | Setting | Value |
|---|---|---|
| `coa.conf` | `CoA.GameModeMask` | `64` |
| `coa.conf` | `CoA.ClassModel` | `"hero"` |
| `coa.conf` | `CoA.MapClass10ToWarrior` | `0` |
| `coa.conf` | `CoA.RealmType` | `"live seasonal"` |
| `coa.conf` | `CoA.ClientBooleanConfigs` | `"CONFIG_LEGACY_CHARACTER_ADVANCEMENT_ENABLED=0"` |
| `mod-coa-challenges.conf` | `CoAChallenges.GameModes.Realm` | `WildCard` |
| `mod_coa_playerbots.conf` | `CoaBots.Enable` | `0` for the first start; later a different `CoaBots.RandomSpawn.AccountPrefix` than the CoA realm |

## 3. Realm list
The CoA client lists realms only with an "info" row per realm (port 1). Name format: `Name!Expansion!GameMode!Image!Unlocked!Page!Index!Description`, game mode 11 = CoA, 6 = Wildcard. The info row's name must start with the realm's exact name.
```sql
INSERT INTO realmlist (id, name, address, localAddress, localSubnetMask, port, icon, flag, timezone, allowedSecurityLevel, population, gamebuild)
  SELECT 2, 'Wildcard', address, localAddress, localSubnetMask, 8086, icon, flag, timezone, allowedSecurityLevel, 0, gamebuild FROM realmlist WHERE id = 1;
INSERT IGNORE INTO realmlist (name, address, localAddress, localSubnetMask, port, icon, flag, timezone, allowedSecurityLevel, population, gamebuild)
  SELECT '<CoA realm name>!0!11!Default!1!1!1!0', address, localAddress, localSubnetMask, 1, icon, 2, 2, 0, 0, gamebuild FROM realmlist WHERE id = 1;
INSERT IGNORE INTO realmlist (name, address, localAddress, localSubnetMask, port, icon, flag, timezone, allowedSecurityLevel, population, gamebuild)
  SELECT 'Wildcard!0!6!Default!1!1!2!0', address, localAddress, localSubnetMask, 1, icon, 2, 2, 0, 0, gamebuild FROM realmlist WHERE id = 1;
```

## 4. Start (cmd)
```bat
cd /d "<server>\Wildcard" && "<server>\Core\worldserver.exe" -c "<server>\Wildcard\configs\worldserver.conf"
```
Running it inside a cmd window keeps its last lines on screen if it stops.
