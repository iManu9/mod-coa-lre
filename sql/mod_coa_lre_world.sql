-- mod-coa-lre world data. Safe to run again: it replaces its own rows.

-- 1. Attach the rune scripts. They sit next to the core's own scripts for these spells.
DELETE FROM `spell_script_names` WHERE `ScriptName` LIKE 'spell_coa_lre_%';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(-324,   'spell_coa_lre_eternal_conduit'),   -- Lightning Shield, every rank
(-26573, 'spell_coa_lre_hallowed_path'),     -- Consecration, every rank
(53385,  'spell_coa_lre_crusading_storm'),   -- Divine Storm
(35395,  'spell_coa_lre_purifying_fires'),   -- Crusader Strike
(-6343,  'spell_coa_lre_thunderous_gale'),   -- Thunder Clap, every rank
(-8042,  'spell_coa_lre_shocks'),            -- Earth Shock, every rank
(-8050,  'spell_coa_lre_shocks'),            -- Flame Shock, every rank
(-8056,  'spell_coa_lre_shocks'),            -- Frost Shock, every rank
(17364,  'spell_coa_lre_crashing_storm'),    -- Stormstrike
(32175,  'spell_coa_lre_crashing_storm'),    -- Stormstrike main-hand hit
(32176,  'spell_coa_lre_crashing_storm'),    -- Stormstrike off-hand hit
(-43265, 'spell_coa_lre_walking_decay'),     -- Death and Decay, every rank
(49576,  'spell_coa_lre_mass_grip'),         -- Death Grip
(-3606,  'spell_coa_lre_searing_swarm');     -- Searing Bolt (Searing Totem), every rank

-- 2. The five rune items (9900001-9900005). Each is a copy of CoA's own usable Class Change Potion
-- (97858), so every column is valid for this database; only what differs is changed.
DELETE FROM `item_template` WHERE `entry` BETWEEN 9900001 AND 9900005;
DROP TEMPORARY TABLE IF EXISTS `lre_item`;
CREATE TEMPORARY TABLE `lre_item` SELECT * FROM `item_template` WHERE `entry` = 97858;
UPDATE `lre_item` SET `Quality` = 5, `bonding` = 0, `RequiredLevel` = 0, `ItemLevel` = 1, `stackable` = 1,
    `maxcount` = 0, `BuyPrice` = 0, `SellPrice` = 0, `ScriptName` = 'coa_lre_rune_item';

UPDATE `lre_item` SET `entry` = 9900001, `name` = 'Sigil of the Wandering Sanctum',
    `description` = 'Rune of the Hallowed Path: your Consecration follows you and pulses every second. Teaches Consecration.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;

UPDATE `lre_item` SET `entry` = 9900002, `name` = 'Heart of the Endless Storm',
    `description` = 'Rune of the Eternal Conduit: Lightning Shield never fades and strikes up to 5 enemies every 3 sec. Teaches Lightning Shield.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;

UPDATE `lre_item` SET `entry` = 9900003, `name` = 'Writ of the Crusading Tempest',
    `description` = 'Rune of the Righteous Crusading Storm: Divine Storm delivers a Crusader Strike to each enemy it hits. Teaches Crusader Strike and Divine Storm.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;

UPDATE `lre_item` SET `entry` = 9900004, `name` = 'Thunderhead Runestone',
    `description` = 'Rune of the Thunderous Gale: Thunder Clap unleashes a Hurricane at your feet and a raging gale. Teaches Thunder Clap and Hurricane.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;

UPDATE `lre_item` SET `entry` = 9900005, `name` = 'Ashes of the Silver Hand',
    `description` = 'Rune of the Purifying Fires: Crusader Strike engulfs its target in Holy Fire. Teaches Crusader Strike and Holy Fire.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;

DROP TEMPORARY TABLE IF EXISTS `lre_item`;

-- Icons, from the server's ItemDisplayInfo.dbc: each rune shows its spell's own icon.
UPDATE `item_template` SET `displayid` = 18235  WHERE `entry` = 9900001; -- spell_holy_innerfire (Consecration)
UPDATE `item_template` SET `displayid` = 14706  WHERE `entry` = 9900002; -- spell_nature_lightningshield
UPDATE `item_template` SET `displayid` = 137814 WHERE `entry` = 9900003; -- ability_paladin_divinestorm
UPDATE `item_template` SET `displayid` = 137448 WHERE `entry` = 9900004; -- spell_nature_thunderclap
UPDATE `item_template` SET `displayid` = 137435 WHERE `entry` = 9900005; -- spell_holy_searinglight (Holy Fire)

-- 2b. The reusable unlearning item (9900010): opens a menu to unlearn a rune; never used up.
DELETE FROM `item_template` WHERE `entry` = 9900010;
DROP TEMPORARY TABLE IF EXISTS `lre_item`;
CREATE TEMPORARY TABLE `lre_item` SELECT * FROM `item_template` WHERE `entry` = 97858;
UPDATE `lre_item` SET `entry` = 9900010, `name` = 'Runebreaker''s Chisel', `Quality` = 4, `bonding` = 0,
    `RequiredLevel` = 0, `ItemLevel` = 1, `stackable` = 1, `maxcount` = 0, `BuyPrice` = 0, `SellPrice` = 0,
    `displayid` = 24590, `ScriptName` = 'coa_lre_unlearn_item',
    `description` = 'Unlearn one of your Runic Enhancements, removing its spells. Reusable.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
DROP TEMPORARY TABLE IF EXISTS `lre_item`;

-- 2c. The unlearning NPC (9900100), a copy of Silas Darkmoon (14823) made friendly to everyone,
-- gossip only. Place one with .npc add 9900100
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (9900100, 9900110);
DELETE FROM `creature_template` WHERE `entry` IN (9900100, 9900110);
DROP TEMPORARY TABLE IF EXISTS `lre_npc`;
CREATE TEMPORARY TABLE `lre_npc` SELECT * FROM `creature_template` WHERE `entry` = 14823;
UPDATE `lre_npc` SET `entry` = 9900100, `name` = 'Runekeeper Vaelith', `subname` = 'Runic Enhancements',
    `npcflag` = 1, `gossip_menu_id` = 0, `faction` = 35, `AIName` = '', `ScriptName` = 'npc_coa_lre_unlearner';
INSERT INTO `creature_template` SELECT * FROM `lre_npc`;
DROP TEMPORARY TABLE IF EXISTS `lre_npc`;
DROP TEMPORARY TABLE IF EXISTS `lre_npc_model`;
CREATE TEMPORARY TABLE `lre_npc_model` SELECT * FROM `creature_template_model` WHERE `CreatureID` = 14823;
UPDATE `lre_npc_model` SET `CreatureID` = 9900100;
INSERT INTO `creature_template_model` SELECT * FROM `lre_npc_model`;
DROP TEMPORARY TABLE IF EXISTS `lre_npc_model`;

-- 2d. Adaptive Training Dummy (9900110): a copy of the Grandmaster's Training Dummy (31144) that takes
-- no damage and changes its level to its attacker's. Place them with .npc add 9900110
DELETE FROM `creature_template_model` WHERE `CreatureID` = 9900110;
DELETE FROM `creature_template` WHERE `entry` = 9900110;
DROP TEMPORARY TABLE IF EXISTS `lre_npc`;
CREATE TEMPORARY TABLE `lre_npc` SELECT * FROM `creature_template` WHERE `entry` = 31144;
UPDATE `lre_npc` SET `entry` = 9900110, `name` = 'Adaptive Training Dummy', `subname` = 'Matches your level',
    `minlevel` = 1, `maxlevel` = 1, `ScriptName` = 'npc_coa_lre_adaptive_dummy';
INSERT INTO `creature_template` SELECT * FROM `lre_npc`;
DROP TEMPORARY TABLE IF EXISTS `lre_npc`;
DROP TEMPORARY TABLE IF EXISTS `lre_npc_model`;
CREATE TEMPORARY TABLE `lre_npc_model` SELECT * FROM `creature_template_model` WHERE `CreatureID` = 31144;
UPDATE `lre_npc_model` SET `CreatureID` = 9900110;
INSERT INTO `creature_template_model` SELECT * FROM `lre_npc_model`;
DROP TEMPORARY TABLE IF EXISTS `lre_npc_model`;

-- 2e. Batch 2 rune items (9900011-9900018): six Legendary, two Epic.
DELETE FROM `item_template` WHERE `entry` BETWEEN 9900011 AND 9900018;
DROP TEMPORARY TABLE IF EXISTS `lre_item`;
CREATE TEMPORARY TABLE `lre_item` SELECT * FROM `item_template` WHERE `entry` = 97858;
UPDATE `lre_item` SET `Quality` = 5, `bonding` = 0, `RequiredLevel` = 0, `ItemLevel` = 1, `stackable` = 1,
    `maxcount` = 0, `BuyPrice` = 0, `SellPrice` = 0, `ScriptName` = 'coa_lre_rune_item';
UPDATE `lre_item` SET `entry` = 9900011, `name` = 'Stormlinked Totem Shard', `displayid` = 137522,
    `description` = 'Rune of Arcing Shocks: your shocks also hit the 2 closest enemies near the target. Teaches Earth, Flame and Frost Shock.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
UPDATE `lre_item` SET `entry` = 9900012, `name` = 'Wildfire Totem Shard', `displayid` = 22457,
    `description` = 'Rune of Spreading Shocks: Flame Shock spreads, Frost Shock slows and Earth Shock interrupts all enemies near the target. Teaches Earth, Flame and Frost Shock.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
UPDATE `lre_item` SET `entry` = 9900013, `name` = 'Eye of the Crashing Storm', `displayid` = 137699,
    `description` = 'Rune of the Crashing Storm: Stormstrike discharges lightning into every enemy within 8 yards. Teaches Stormstrike.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
UPDATE `lre_item` SET `entry` = 9900014, `name` = 'Shroud of the Walking Rot', `displayid` = 137418,
    `description` = 'Rune of the Walking Decay: your Death and Decay follows you. Teaches Death and Decay.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
UPDATE `lre_item` SET `entry` = 9900015, `name` = 'Chains of the Lich', `displayid` = 137713,
    `description` = 'Rune of the Mass Grip: Death Grip also pulls up to 5 enemies near the target. Teaches Death Grip.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
UPDATE `lre_item` SET `entry` = 9900016, `name` = 'Ember of the Searing Swarm', `displayid` = 18511,
    `description` = 'Rune of the Searing Swarm: your Searing Totem fires at 3 targets at once. Teaches Searing Totem.';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
UPDATE `lre_item` SET `Quality` = 4;
UPDATE `lre_item` SET `entry` = 9900017, `name` = 'Codex of the Maelstrom', `displayid` = 137840,
    `description` = 'Rune of Maelstrom Weapon: grants the talent Maelstrom Weapon (5/5).';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
UPDATE `lre_item` SET `entry` = 9900018, `name` = 'Codex of Thassarian', `displayid` = 137606,
    `description` = 'Rune of the Threat of Thassarian: grants the talent Threat of Thassarian (3/3).';
INSERT INTO `item_template` SELECT * FROM `lre_item`;
DROP TEMPORARY TABLE IF EXISTS `lre_item`;

-- 2f. Vendor: Runekeeper Vaelith sells every rune item. Prices: Legendary 1,000 gold, Epic 250 gold.
UPDATE `item_template` SET `BuyCount` = 1, `BuyPrice` = 10000000
    WHERE `ScriptName` = 'coa_lre_rune_item' AND `Quality` = 5 AND `entry` BETWEEN 9900001 AND 9900099;
UPDATE `item_template` SET `BuyCount` = 1, `BuyPrice` = 2500000
    WHERE `ScriptName` = 'coa_lre_rune_item' AND `Quality` = 4 AND `entry` BETWEEN 9900001 AND 9900099;
UPDATE `creature_template` SET `npcflag` = 129 WHERE `entry` = 9900100;   -- gossip + vendor
DELETE FROM `npc_vendor` WHERE `entry` = 9900100;
INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `ExtendedCost`, `VerifiedBuild`)
SELECT 9900100, 0, `entry`, 0, 0, 0, 0 FROM `item_template`
    WHERE `ScriptName` = 'coa_lre_rune_item' AND `entry` BETWEEN 9900001 AND 9900099 ORDER BY `entry`;

-- 3. Check: five scripts and five items.
SELECT `spell_id`, `ScriptName` FROM `spell_script_names` WHERE `ScriptName` LIKE 'spell_coa_lre_%';
SELECT `entry`, `name`, `displayid`, `Quality`, `BuyPrice` DIV 10000 AS gold, `ScriptName` FROM `item_template` WHERE `entry` BETWEEN 9900001 AND 9900018;
SELECT COUNT(*) AS vendor_items FROM `npc_vendor` WHERE `entry` = 9900100;
SELECT `entry`, `name`, `subname`, `npcflag`, `faction`, `ScriptName` FROM `creature_template` WHERE `entry` IN (9900100, 9900110);
SELECT `CreatureID`, `CreatureDisplayID` FROM `creature_template_model` WHERE `CreatureID` IN (9900100, 9900110);
