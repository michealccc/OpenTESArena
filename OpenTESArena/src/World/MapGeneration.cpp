#include <algorithm>
#include <unordered_map>

#include "ArenaCityUtils.h"
#include "ArenaInteriorUtils.h"
#include "ArenaLevelUtils.h"
#include "ArenaVoxelUtils.h"
#include "ArenaWildUtils.h"
#include "ChunkUtils.h"
#include "InteriorType.h"
#include "InteriorUtils.h"
#include "LevelDefinition.h"
#include "LevelInfoDefinition.h"
#include "LocationUtils.h"
#include "LockDefinition.h"
#include "MapDefinition.h"
#include "MapGeneration.h"
#include "TransitionDefinition.h"
#include "TriggerDefinition.h"
#include "VoxelDataType.h"
#include "VoxelDefinition.h"
#include "VoxelFacing2D.h"
#include "WorldType.h"
#include "../Assets/ArenaAnimUtils.h"
#include "../Assets/ArenaTypes.h"
#include "../Assets/BinaryAssetLibrary.h"
#include "../Assets/MIFUtils.h"
#include "../Assets/TextAssetLibrary.h"
#include "../Entities/EntityDefinition.h"
#include "../Entities/EntityDefinitionLibrary.h"
#include "../Entities/EntityType.h"
#include "../Math/Random.h"

#include "components/debug/Debug.h"
#include "components/utilities/BufferView2D.h"
#include "components/utilities/String.h"

namespace MapGeneration
{
	// Mapping caches of .MIF/.RMD voxels, etc. to modern level info entries.
	using ArenaVoxelMappingCache = std::unordered_map<ArenaTypes::VoxelID, LevelDefinition::VoxelDefID>;
	using ArenaEntityMappingCache = std::unordered_map<ArenaTypes::VoxelID, LevelDefinition::EntityDefID>;
	using ArenaLockMappingCache = std::vector<std::pair<ArenaTypes::MIFLock, LevelDefinition::LockDefID>>;
	using ArenaTriggerMappingCache = std::vector<std::pair<ArenaTypes::MIFTrigger, LevelDefinition::TriggerDefID>>;
	using ArenaTransitionMappingCache = std::unordered_map<ArenaTypes::VoxelID, LevelDefinition::TransitionDefID>;
	using ArenaBuildingNameMappingCache = std::unordered_map<std::string, LevelDefinition::BuildingNameID>;

	static_assert(sizeof(ArenaTypes::VoxelID) == sizeof(uint16_t));

	// .INF flat index for determining if a flat is a transition to a wild dungeon.
	constexpr ArenaTypes::FlatIndex WildDenFlatIndex = 37;

	uint8_t getVoxelMostSigByte(ArenaTypes::VoxelID voxelID)
	{
		return (voxelID & 0x7F00) >> 8;
	}

	uint8_t getVoxelLeastSigByte(ArenaTypes::VoxelID voxelID)
	{
		return voxelID & 0x007F;
	}

	// Whether the Arena *MENU ID is for a city gate left/right voxel.
	bool isCityGateMenuIndex(int menuIndex, WorldType worldType)
	{
		if (worldType == WorldType::Interior)
		{
			// No city gates in interiors.
			return false;
		}
		else if (worldType == WorldType::City)
		{
			return (menuIndex == 7) || (menuIndex == 8);
		}
		else if (worldType == WorldType::Wilderness)
		{
			return (menuIndex == 6) || (menuIndex == 7);
		}
		else
		{
			DebugUnhandledReturnMsg(bool, std::to_string(static_cast<int>(worldType)));
		}
	}

	// Converts the given Arena *MENU ID to a modern interior type, if any.
	std::optional<InteriorType> tryGetInteriorTypeFromMenuIndex(int menuIndex, WorldType worldType)
	{
		if (worldType == WorldType::City)
		{
			// Mappings of Arena city *MENU IDs to interiors.
			constexpr std::array<std::pair<int, InteriorType>, 11> CityMenuMappings =
			{
				{
					{ 0, InteriorType::Equipment },
					{ 1, InteriorType::Tavern },
					{ 2, InteriorType::MagesGuild },
					{ 3, InteriorType::Temple },
					{ 4, InteriorType::House },
					{ 5, InteriorType::House },
					{ 6, InteriorType::House },
					// 7 - city gate
					// 8 - city gate
					{ 9, InteriorType::Noble },
					// 10 - none
					{ 11, InteriorType::Palace },
					{ 12, InteriorType::Palace },
					{ 13, InteriorType::Palace }
				}
			};

			const auto iter = std::find_if(CityMenuMappings.begin(), CityMenuMappings.end(),
				[menuIndex](const auto &pair)
			{
				return pair.first == menuIndex;
			});

			if (iter != CityMenuMappings.end())
			{
				return iter->second;
			}
			else
			{
				return std::nullopt;
			}
		}
		else if (worldType == WorldType::Wilderness)
		{
			// Mappings of Arena wilderness *MENU IDs to interiors.
			constexpr std::array<std::pair<int, InteriorType>, 7> WildMenuMappings =
			{
				{
					// 0 - none
					{ 1, InteriorType::Crypt },
					{ 2, InteriorType::House },
					{ 3, InteriorType::Tavern },
					{ 4, InteriorType::Temple },
					{ 5, InteriorType::Tower },
					// 6 - city gate
					// 7 - city gate
					{ 8, InteriorType::Dungeon },
					{ 9, InteriorType::Dungeon }
				}
			};

			const auto iter = std::find_if(WildMenuMappings.begin(), WildMenuMappings.end(),
				[menuIndex](const auto &pair)
			{
				return pair.first == menuIndex;
			});

			if (iter != WildMenuMappings.end())
			{
				return iter->second;
			}
			else
			{
				return std::nullopt;
			}
		}
		else
		{
			DebugUnhandledReturnMsg(std::optional<InteriorType>, std::to_string(static_cast<int>(worldType)));
		}
	}

	MapGeneration::InteriorGenInfo makeInteriorGenInfo(InteriorType interiorType,
		const std::optional<bool> &rulerIsMale)
	{
		// @todo: probably need to have LevelInt3 or similar in TransitionGenInfo so this can properly
		// make the menuID and .MIF name w/ LevelUtils::getDoorVoxelMifName() for the InteriorGenInfo.
		MapGeneration::InteriorGenInfo interiorGenInfo;

		if (InteriorUtils::isPrefabInterior(interiorType))
		{
			std::string mifName; // @todo: get from LevelUtils::getDoorVoxelMifName()
			DebugNotImplemented();
			interiorGenInfo.initPrefab(std::move(mifName), interiorType, rulerIsMale);
		}
		else if (InteriorUtils::isProceduralInterior(interiorType))
		{
			const uint32_t dungeonSeed = -1; // @todo: see existing InteriorLevelData functions I think?
			const WEInt widthChunks = -1; // @todo
			const SNInt depthChunks = -1; // @todo
			const bool isArtifactDungeon = false; // Can't have wild den artifact dungeons.
			DebugNotImplemented();
			interiorGenInfo.initDungeon(dungeonSeed, widthChunks, depthChunks, isArtifactDungeon);
		}
		else
		{
			DebugNotImplementedMsg(std::to_string(static_cast<int>(interiorType)));
		}

		return interiorGenInfo;
	}

	// Makes a modern entity definition from the given Arena FLAT index.
	// @todo: probably want this to be some 'LevelEntityDefinition' with no dependencies on runtime
	// textures and animations handles, instead using texture filenames for the bulk of things.
	bool tryMakeEntityDefFromArenaFlat(ArenaTypes::FlatIndex flatIndex, WorldType worldType,
		const std::optional<InteriorType> &interiorType, const std::optional<bool> &rulerIsMale,
		const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
		const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
		TextureManager &textureManager, EntityDefinition *outDef)
	{
		const INFFile::FlatData &flatData = inf.getFlat(flatIndex);
		const EntityType entityType = ArenaAnimUtils::getEntityTypeFromFlat(flatIndex, inf);
		const std::optional<ArenaTypes::ItemIndex> &optItemIndex = flatData.itemIndex;

		bool isFinalBoss;
		const bool isCreature = optItemIndex.has_value() &&
			ArenaAnimUtils::isCreatureIndex(*optItemIndex, &isFinalBoss);
		const bool isHumanEnemy = optItemIndex.has_value() &&
			ArenaAnimUtils::isHumanEnemyIndex(*optItemIndex);

		// Add entity animation data. Static entities have only idle animations (and maybe on/off
		// state for lampposts). Dynamic entities have several animation states and directions.
		//auto &entityAnimData = newEntityDef.getAnimationData();
		EntityAnimationDefinition entityAnimDef;
		EntityAnimationInstance entityAnimInst;
		if (entityType == EntityType::Static)
		{
			if (!ArenaAnimUtils::tryMakeStaticEntityAnims(flatIndex, worldType, interiorType,
				rulerIsMale, inf, textureManager, &entityAnimDef, &entityAnimInst))
			{
				DebugLogWarning("Couldn't make static entity anims for flat \"" +
					std::to_string(flatIndex) + "\".");
				return false;
			}

			// The entity can only be instantiated if there is at least an idle animation.
			int idleStateIndex;
			if (!entityAnimDef.tryGetStateIndex(EntityAnimationUtils::STATE_IDLE.c_str(), &idleStateIndex))
			{
				DebugLogWarning("Missing static entity idle anim state for flat \"" +
					std::to_string(flatIndex) + "\".");
				return false;
			}
		}
		else if (entityType == EntityType::Dynamic)
		{
			// Assume that human enemies in level data are male.
			const std::optional<bool> isMale = true;

			if (!ArenaAnimUtils::tryMakeDynamicEntityAnims(flatIndex, isMale, inf, charClassLibrary,
				binaryAssetLibrary, textureManager, &entityAnimDef, &entityAnimInst))
			{
				DebugLogWarning("Couldn't make dynamic entity anims for flat \"" +
					std::to_string(flatIndex) + "\".");
				return false;
			}

			// Must have at least an idle animation.
			int idleStateIndex;
			if (!entityAnimDef.tryGetStateIndex(EntityAnimationUtils::STATE_IDLE.c_str(), &idleStateIndex))
			{
				DebugLogWarning("Missing dynamic entity idle anim state for flat \"" +
					std::to_string(flatIndex) + "\".");
				return false;
			}
		}
		else
		{
			DebugCrash("Unrecognized entity type \"" +
				std::to_string(static_cast<int>(entityType)) + "\".");
		}

		// @todo: replace isCreature/etc. with some flatIndex -> EntityDefinition::Type function.
		// - Most likely also need location type, etc. because flatIndex is level-dependent.
		if (isCreature)
		{
			const ArenaTypes::ItemIndex itemIndex = *optItemIndex;
			const int creatureID = isFinalBoss ?
				ArenaAnimUtils::getFinalBossCreatureID() :
				ArenaAnimUtils::getCreatureIDFromItemIndex(itemIndex);
			const int creatureIndex = creatureID - 1;

			// @todo: read from EntityDefinitionLibrary instead, and don't make anim def above.
			// Currently these are just going to be duplicates of defs in the library.
			EntityDefinitionLibrary::Key entityDefKey;
			entityDefKey.initCreature(creatureIndex, isFinalBoss);

			EntityDefID entityDefID;
			if (!entityDefLibrary.tryGetDefinitionID(entityDefKey, &entityDefID))
			{
				DebugLogWarning("Couldn't get creature definition " +
					std::to_string(creatureIndex) + " from library.");
				return false;
			}

			*outDef = entityDefLibrary.getDefinition(entityDefID);
		}
		else if (isHumanEnemy)
		{
			const bool male = true; // Always male from map data.
			const int charClassID = ArenaAnimUtils::getCharacterClassIndexFromItemIndex(*optItemIndex);
			outDef->initEnemyHuman(male, charClassID, std::move(entityAnimDef));
		}
		else // @todo: handle other entity definition types.
		{
			// Doodad.
			const bool streetLight = ArenaAnimUtils::isStreetLightFlatIndex(flatIndex, worldType);
			const double scale = ArenaAnimUtils::getDimensionModifier(flatData);
			const int lightIntensity = flatData.lightIntensity.has_value() ? *flatData.lightIntensity : 0;

			// @todo: TransitionDefID from flatIndex -- use MapGeneration::isMap1TransitionEntity().
			DebugNotImplemented();

			outDef->initDoodad(flatData.yOffset, scale, flatData.collider,
				flatData.transparent, flatData.ceiling, streetLight, flatData.puddle,
				lightIntensity, std::move(entityAnimDef));
		}

		return true;
	}

	VoxelDefinition makeVoxelDefFromFLOR(ArenaTypes::VoxelID florVoxel, const INFFile &inf)
	{
		const int textureID = (florVoxel & 0xFF00) >> 8;

		// Determine if the floor voxel is either solid or a chasm.
		if (!MIFUtils::isChasm(textureID))
		{
			return VoxelDefinition::makeFloor(textureID);
		}
		else
		{
			int chasmID;
			VoxelDefinition::ChasmData::Type chasmType;
			if (textureID == MIFUtils::DRY_CHASM)
			{
				const std::optional<int> &dryChasmIndex = inf.getDryChasmIndex();
				if (dryChasmIndex.has_value())
				{
					chasmID = *dryChasmIndex;
				}
				else
				{
					DebugLogWarning("Missing *DRYCHASM ID.");
					chasmID = 0;
				}

				chasmType = VoxelDefinition::ChasmData::Type::Dry;
			}
			else if (textureID == MIFUtils::LAVA_CHASM)
			{
				const std::optional<int> &lavaChasmIndex = inf.getLavaChasmIndex();
				if (lavaChasmIndex.has_value())
				{
					chasmID = *lavaChasmIndex;
				}
				else
				{
					DebugLogWarning("Missing *LAVACHASM ID.");
					chasmID = 0;
				}

				chasmType = VoxelDefinition::ChasmData::Type::Lava;
			}
			else if (textureID == MIFUtils::WET_CHASM)
			{
				const std::optional<int> &wetChasmIndex = inf.getWetChasmIndex();
				if (wetChasmIndex.has_value())
				{
					chasmID = *wetChasmIndex;
				}
				else
				{
					DebugLogWarning("Missing *WETCHASM ID.");
					chasmID = 0;
				}

				chasmType = VoxelDefinition::ChasmData::Type::Wet;
			}
			else
			{
				DebugCrash("Unsupported chasm type \"" + std::to_string(textureID) + "\".");
			}

			return VoxelDefinition::makeChasm(chasmID, chasmType);
		}
	}

	VoxelDefinition makeVoxelDefFromMAP1(ArenaTypes::VoxelID map1Voxel, uint8_t mostSigNibble,
		WorldType worldType, const INFFile &inf, const ExeData &exeData)
	{
		DebugAssert(map1Voxel != 0);
		DebugAssert(mostSigNibble != 0x8);

		if ((map1Voxel & 0x8000) == 0)
		{
			// A voxel of some kind.
			const uint8_t mostSigByte = MapGeneration::getVoxelMostSigByte(map1Voxel);
			const uint8_t leastSigByte = MapGeneration::getVoxelLeastSigByte(map1Voxel);
			const bool voxelIsSolid = mostSigByte == leastSigByte;

			if (voxelIsSolid)
			{
				// Regular solid wall.
				const int textureIndex = mostSigByte - 1;

				// Menu index if the voxel has the *MENU tag, or -1 if it is not a *MENU voxel.
				const std::optional<int> &menuIndex = inf.getMenuIndex(textureIndex);
				const bool isMenu = menuIndex.has_value();

				// Determine what the type of the wall is (level up/down, menu, or just plain solid).
				const VoxelDefinition::WallData::Type type = [&inf, textureIndex, isMenu]()
				{
					// Returns whether the given index pointer is non-null and matches the current
					// texture index.
					auto matchesIndex = [textureIndex](const std::optional<int> &index)
					{
						return index.has_value() && (*index == textureIndex);
					};

					if (matchesIndex(inf.getLevelUpIndex()))
					{
						return VoxelDefinition::WallData::Type::LevelUp;
					}
					else if (matchesIndex(inf.getLevelDownIndex()))
					{
						return VoxelDefinition::WallData::Type::LevelDown;
					}
					else if (isMenu)
					{
						return VoxelDefinition::WallData::Type::Menu;
					}
					else
					{
						return VoxelDefinition::WallData::Type::Solid;
					}
				}();

				return VoxelDefinition::makeWall(textureIndex, textureIndex, textureIndex,
					menuIndex, type);
			}
			else
			{
				// Raised platform.
				const uint8_t wallTextureID = map1Voxel & 0x000F;
				const uint8_t capTextureID = (map1Voxel & 0x00F0) >> 4;

				const int sideID = [&inf, wallTextureID]()
				{
					const std::optional<int> &id = inf.getBoxSide(wallTextureID);
					if (id.has_value())
					{
						return *id;
					}
					else
					{
						DebugLogWarning("Missing *BOXSIDE ID \"" + std::to_string(wallTextureID) +
							"\" for raised platform side.");
						return 0;
					}
				}();

				const int floorID = [&inf]()
				{
					const auto &id = inf.getCeiling().textureIndex;
					if (id.has_value())
					{
						return id.value();
					}
					else
					{
						DebugLogWarning("Missing *CEILING texture ID for raised platform floor.");
						return 0;
					}
				}();

				const int ceilingID = [&inf, capTextureID]()
				{
					const std::optional<int> &id = inf.getBoxCap(capTextureID);
					if (id.has_value())
					{
						return *id;
					}
					else
					{
						DebugLogWarning("Missing *BOXCAP ID \"" + std::to_string(capTextureID) +
							"\" for raised platform ceiling.");
						return 0;
					}
				}();

				const auto &wallHeightTables = exeData.wallHeightTables;
				const int heightIndex = mostSigByte & 0x07;
				const int thicknessIndex = (mostSigByte & 0x78) >> 3;

				int baseOffset, baseSize;
				if (worldType == WorldType::Interior)
				{
					baseOffset = wallHeightTables.box1a.at(heightIndex);

					const int boxSize = wallHeightTables.box2a.at(thicknessIndex);
					const auto &boxScale = inf.getCeiling().boxScale;
					baseSize = boxScale.has_value() ?
						((boxSize * (*boxScale)) / 256) : boxSize;
				}
				else if (worldType == WorldType::City)
				{
					baseOffset = wallHeightTables.box1b.at(heightIndex);
					baseSize = wallHeightTables.box2b.at(thicknessIndex);
				}
				else if (worldType == WorldType::Wilderness)
				{
					baseOffset = wallHeightTables.box1c.at(heightIndex);

					constexpr int boxSize = 32;
					const auto &boxScale = inf.getCeiling().boxScale;
					baseSize = (boxSize * (boxScale.has_value() ? boxScale.value() : 192)) / 256;
				}
				else
				{
					DebugNotImplementedMsg(std::to_string(static_cast<int>(worldType)));
				}

				const double yOffset = static_cast<double>(baseOffset) / MIFUtils::ARENA_UNITS;
				const double ySize = static_cast<double>(baseSize) / MIFUtils::ARENA_UNITS;
				const double normalizedScale = static_cast<double>(inf.getCeiling().height) / MIFUtils::ARENA_UNITS;
				const double yOffsetNormalized = yOffset / normalizedScale;
				const double ySizeNormalized = ySize / normalizedScale;

				// @todo: might need some tweaking with box3/box4 values.
				const double vTop = std::max(0.0, 1.0 - yOffsetNormalized - ySizeNormalized);
				const double vBottom = std::min(vTop + ySizeNormalized, 1.0);

				return VoxelDefinition::makeRaised(sideID, floorID, ceilingID, yOffsetNormalized,
					ySizeNormalized, vTop, vBottom);
			}
		}
		else
		{
			if (mostSigNibble == 0x9)
			{
				// Transparent block with 1-sided texture on all sides, such as wooden arches in
				// dungeons. These do not have back-faces (especially when standing in the voxel
				// itself).
				const int textureIndex = (map1Voxel & 0x00FF) - 1;
				const bool collider = (map1Voxel & 0x0100) == 0;
				return VoxelDefinition::makeTransparentWall(textureIndex, collider);
			}
			else if (mostSigNibble == 0xA)
			{
				// Transparent block with 2-sided texture on one side (i.e. fence). Note that in
				// the center province's city, there is a temple voxel with zeroes for its texture
				// index, and it appears solid gray in the original game (presumably a silent bug).
				const int textureIndex = (map1Voxel & 0x003F) - 1;
				if (textureIndex < 0)
				{
					DebugLogWarning("Invalid texture index \"" + std::to_string(textureIndex) +
						"\" for type 0xA voxel.");
				}

				const double yOffset = [worldType, map1Voxel]()
				{
					const int baseOffset = (map1Voxel & 0x0E00) >> 9;
					const int fullOffset = (worldType == WorldType::Interior) ?
						(baseOffset * 8) : ((baseOffset * 32) - 8);

					return static_cast<double>(fullOffset) / MIFUtils::ARENA_UNITS;
				}();

				const bool collider = (map1Voxel & 0x0100) != 0;

				// "Flipped" is not present in the original game, but has been added
				// here so that all edge voxel texture coordinates (i.e., palace
				// graphics, store signs) can be correct. Currently only palace
				// graphics and gates are type 0xA colliders, I believe.
				const bool flipped = collider;

				const VoxelFacing2D facing = [map1Voxel]()
				{
					// Orientation is a multiple of 4 (0, 4, 8, C), where 0 is north
					// and C is east. It is stored in two bits above the texture index.
					const int orientation = (map1Voxel & 0x00C0) >> 4;
					if (orientation == 0x0)
					{
						return VoxelFacing2D::NegativeX;
					}
					else if (orientation == 0x4)
					{
						return VoxelFacing2D::PositiveZ;
					}
					else if (orientation == 0x8)
					{
						return VoxelFacing2D::PositiveX;
					}
					else
					{
						return VoxelFacing2D::NegativeZ;
					}
				}();

				return VoxelDefinition::makeEdge(textureIndex, yOffset, collider, flipped, facing);
			}
			else if (mostSigNibble == 0xB)
			{
				// Door voxel.
				const int textureIndex = (map1Voxel & 0x003F) - 1;
				const VoxelDefinition::DoorData::Type doorType = [map1Voxel]()
				{
					const int type = (map1Voxel & 0x00C0) >> 4;
					if (type == 0x0)
					{
						return VoxelDefinition::DoorData::Type::Swinging;
					}
					else if (type == 0x4)
					{
						return VoxelDefinition::DoorData::Type::Sliding;
					}
					else if (type == 0x8)
					{
						return VoxelDefinition::DoorData::Type::Raising;
					}
					else
					{
						// Arena doesn't seem to have splitting doors, but they are supported.
						DebugLogWarning("Unrecognized door type \"" + std::to_string(type) +
							"\", treating as splitting.");
						return VoxelDefinition::DoorData::Type::Splitting;
					}
				}();

				return VoxelDefinition::makeDoor(textureIndex, doorType);
			}
			else if (mostSigNibble == 0xC)
			{
				// Unknown.
				DebugLogWarning("Unrecognized voxel type 0xC.");
				return VoxelDefinition();
			}
			else if (mostSigNibble == 0xD)
			{
				// Diagonal wall.
				const int textureIndex = (map1Voxel & 0x00FF) - 1;
				const bool isRightDiag = (map1Voxel & 0x0100) == 0;
				return VoxelDefinition::makeDiagonal(textureIndex, isRightDiag);
			}
			else
			{
				DebugUnhandledReturnMsg(VoxelDefinition, std::to_string(mostSigNibble));
			}
		}
	}

	VoxelDefinition makeVoxelDefFromMAP2(ArenaTypes::VoxelID map2Voxel)
	{
		const int textureIndex = (map2Voxel & 0x007F) - 1;
		constexpr std::optional<int> menuID; // MAP2 cannot have a *MENU ID.
		return VoxelDefinition::makeWall(textureIndex, textureIndex, textureIndex, menuID,
			VoxelDefinition::WallData::Type::Solid);
	}

	LockDefinition makeLockDefFromArenaLock(const ArenaTypes::MIFLock &lock)
	{
		const OriginalInt2 lockPos(lock.x, lock.y);
		const LevelInt2 newLockPos = VoxelUtils::originalVoxelToNewVoxel(lockPos);
		return LockDefinition::makeLeveledLock(newLockPos.x, 1, newLockPos.y, lock.lockLevel);
	}

	TriggerDefinition makeTriggerDefFromArenaTrigger(const ArenaTypes::MIFTrigger &trigger,
		const INFFile &inf)
	{
		const OriginalInt2 triggerPos(trigger.x, trigger.y);
		const LevelInt2 newTriggerPos = VoxelUtils::originalVoxelToNewVoxel(triggerPos);

		TriggerDefinition triggerDef;
		triggerDef.init(newTriggerPos.x, 1, newTriggerPos.y);

		// There can be a text trigger and sound trigger in the same voxel.
		const bool isTextTrigger = trigger.textIndex != -1;
		const bool isSoundTrigger = trigger.soundIndex != -1;

		// Make sure the text index points to a text value (i.e., not a key or riddle).
		if (isTextTrigger && inf.hasTextIndex(trigger.textIndex))
		{
			const INFFile::TextData &textData = inf.getText(trigger.textIndex);
			triggerDef.setTextDef(std::string(textData.text), textData.displayedOnce);
		}

		if (isSoundTrigger)
		{
			const char *soundName = inf.getSound(trigger.soundIndex);
			triggerDef.setSoundDef(String::toUppercase(soundName));
		}

		return triggerDef;
	}

	std::optional<MapGeneration::TransitionDefGenInfo> tryMakeVoxelTransitionDefGenInfo(
		ArenaTypes::VoxelID map1Voxel, WorldType worldType, const INFFile &inf)
	{
		// @todo: needs to handle palace voxel too here (type 0xA voxel, menuID 11?).
		const uint8_t mostSigByte = MapGeneration::getVoxelMostSigByte(map1Voxel);
		const uint8_t leastSigByte = MapGeneration::getVoxelLeastSigByte(map1Voxel);
		const bool isWall = mostSigByte == leastSigByte;
		if (!isWall)
		{
			// Raised platforms cannot be transitions.
			return std::nullopt;
		}

		const int textureIndex = mostSigByte - 1;
		const std::optional<int> menuIndex = inf.getMenuIndex(textureIndex);

		if (worldType == WorldType::Interior)
		{
			const std::optional<int> &levelUpIndex = inf.getLevelUpIndex();
			const std::optional<int> &levelDownIndex = inf.getLevelDownIndex();
			const bool matchesLevelUp = levelUpIndex.has_value() && (*levelUpIndex == textureIndex);
			const bool matchesLevelDown = levelDownIndex.has_value() && (*levelDownIndex == textureIndex);
			const bool isMenu = menuIndex.has_value();
			const bool isValid = matchesLevelUp || matchesLevelDown || isMenu;

			if (isValid)
			{
				const bool isLevelChange = matchesLevelUp || matchesLevelDown;
				const TransitionType transitionType = isLevelChange ?
					TransitionType::LevelChange : TransitionType::ExitInterior;

				constexpr std::optional<InteriorType> interiorType; // Can't have interiors in interiors.
				const std::optional<bool> isLevelUp = [matchesLevelUp, isLevelChange]() -> std::optional<bool>
				{
					if (isLevelChange)
					{
						return matchesLevelUp;
					}
					else
					{
						return std::nullopt;
					}
				}();

				MapGeneration::TransitionDefGenInfo transitionDefGenInfo;
				transitionDefGenInfo.init(transitionType, interiorType, isLevelUp);
				return transitionDefGenInfo;
			}
			else
			{
				return std::nullopt;
			}
		}
		else if ((worldType == WorldType::City) || (worldType == WorldType::Wilderness))
		{
			const bool isValid = menuIndex.has_value();

			if (isValid)
			{
				// Either city gates or an interior entrance.
				const bool isCityGate = MapGeneration::isCityGateMenuIndex(*menuIndex, worldType);

				// Can't guarantee that an Arena *MENU block that isn't a city gate is a valid transition?
				// I thought there were some wild dungeon voxels that resulted in bad values or something.
				const std::optional<InteriorType> interiorType =
					MapGeneration::tryGetInteriorTypeFromMenuIndex(*menuIndex, worldType);

				// This is optional because of the interior type issue above.
				const std::optional<TransitionType> transitionType = [isCityGate, &interiorType]()
					-> std::optional<TransitionType>
				{
					if (isCityGate)
					{
						return TransitionType::CityGate;
					}
					else if (interiorType.has_value())
					{
						return TransitionType::EnterInterior;
					}
					else
					{
						return std::nullopt;
					}
				}();

				if (transitionType.has_value())
				{
					constexpr std::optional<bool> isLevelUp; // No level changes outside of interiors.

					MapGeneration::TransitionDefGenInfo transitionDefGenInfo;
					transitionDefGenInfo.init(*transitionType, interiorType, isLevelUp);
					return transitionDefGenInfo;
				}
				else
				{
					return std::nullopt;
				}
			}
			else
			{
				return std::nullopt;
			}
		}
		else
		{
			DebugUnhandledReturnMsg(std::optional<MapGeneration::TransitionDefGenInfo>,
				std::to_string(static_cast<int>(worldType)));
		}
	}

	// Returns transition gen info if the MAP1 flat index is a transition entity for the given world type.
	std::optional<MapGeneration::TransitionDefGenInfo> tryMakeEntityTransitionGenInfo(
		ArenaTypes::FlatIndex flatIndex, WorldType worldType)
	{
		// Only wild dens are entities with transition data.
		if (worldType != WorldType::Wilderness)
		{
			return std::nullopt;
		}

		const bool isWildDen = flatIndex == MapGeneration::WildDenFlatIndex;
		if (!isWildDen)
		{
			return std::nullopt;
		}

		MapGeneration::TransitionDefGenInfo transitionDefGenInfo;
		transitionDefGenInfo.init(TransitionType::EnterInterior, InteriorType::Dungeon, std::nullopt);
		return transitionDefGenInfo;
	}

	TransitionDefinition makeTransitionDef(const MapGeneration::TransitionDefGenInfo &transitionDefGenInfo,
		const std::optional<bool> &rulerIsMale)
	{
		TransitionDefinition transitionDef;

		if (transitionDefGenInfo.transitionType == TransitionType::CityGate)
		{
			transitionDef.initCityGate();
		}
		else if (transitionDefGenInfo.transitionType == TransitionType::EnterInterior)
		{
			DebugAssert(transitionDefGenInfo.interiorType.has_value());
			const InteriorType interiorType = *transitionDefGenInfo.interiorType;
			MapGeneration::InteriorGenInfo interiorGenInfo =
				MapGeneration::makeInteriorGenInfo(interiorType, rulerIsMale);
			transitionDef.initInteriorEntrance(std::move(interiorGenInfo));
		}
		else if (transitionDefGenInfo.transitionType == TransitionType::ExitInterior)
		{
			transitionDef.initInteriorExit();
		}
		else if (transitionDefGenInfo.transitionType == TransitionType::LevelChange)
		{
			DebugAssert(transitionDefGenInfo.isLevelUp.has_value());
			transitionDef.initLevelChange(*transitionDefGenInfo.isLevelUp);
		}
		else
		{
			DebugNotImplementedMsg(std::to_string(
				static_cast<int>(transitionDefGenInfo.transitionType)));
		}

		return transitionDef;
	}

	// Converts .MIF/.RMD FLOR voxels to modern voxel + entity format.
	void readArenaFLOR(const BufferView2D<const ArenaTypes::VoxelID> &flor, WorldType worldType,
		const std::optional<InteriorType> &interiorType, const std::optional<bool> &rulerIsMale,
		const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
		const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
		TextureManager &textureManager, LevelDefinition *outLevelDef, LevelInfoDefinition *outLevelInfoDef,
		ArenaVoxelMappingCache *voxelCache, ArenaEntityMappingCache *entityCache)
	{
		for (SNInt florZ = 0; florZ < flor.getHeight(); florZ++)
		{
			for (WEInt florX = 0; florX < flor.getWidth(); florX++)
			{
				const ArenaTypes::VoxelID florVoxel = flor.get(florX, florZ);

				// Get voxel def ID from cache or create a new one.
				LevelDefinition::VoxelDefID voxelDefID;
				const auto iter = voxelCache->find(florVoxel);
				if (iter != voxelCache->end())
				{
					voxelDefID = iter->second;
				}
				else
				{
					VoxelDefinition voxelDef = MapGeneration::makeVoxelDefFromFLOR(florVoxel, inf);
					voxelDefID = outLevelInfoDef->addVoxelDef(std::move(voxelDef));
					voxelCache->insert(std::make_pair(florVoxel, voxelDefID));
				}

				const SNInt levelX = florZ;
				const int levelY = 0;
				const WEInt levelZ = florX;
				outLevelDef->setVoxel(levelX, levelY, levelZ, voxelDefID);

				// Floor voxels can also contain data for raised platform flats.
				const int floorFlatID = florVoxel & 0x00FF;
				if (floorFlatID > 0)
				{
					// Get entity def ID from cache or create a new one.
					LevelDefinition::EntityDefID entityDefID;
					const auto iter = entityCache->find(florVoxel);
					if (iter != entityCache->end())
					{
						entityDefID = iter->second;
					}
					else
					{
						const ArenaTypes::FlatIndex flatIndex = floorFlatID - 1;
						EntityDefinition entityDef;
						if (!MapGeneration::tryMakeEntityDefFromArenaFlat(flatIndex, worldType,
							interiorType, rulerIsMale, inf, charClassLibrary, entityDefLibrary,
							binaryAssetLibrary, textureManager, &entityDef))
						{
							DebugLogWarning("Couldn't make entity definition from FLAT \"" +
								std::to_string(flatIndex) + "\" with .INF \"" + inf.getName() + "\".");
							continue;
						}

						entityDefID = outLevelInfoDef->addEntityDef(std::move(entityDef));
						entityCache->insert(std::make_pair(florVoxel, entityDefID));
					}

					const LevelDouble3 entityPos(
						static_cast<SNDouble>(levelX) + 0.50,
						1.0, // Will probably be ignored in favor of raised platform top face.
						static_cast<WEDouble>(levelZ) + 0.50);
					outLevelDef->addEntity(entityDefID, entityPos);
				}
			}
		}
	}

	// Converts .MIF/.RMD MAP1 voxels to modern voxel + entity format.
	void readArenaMAP1(const BufferView2D<const ArenaTypes::VoxelID> &map1, WorldType worldType,
		const std::optional<InteriorType> &interiorType, const std::optional<bool> &rulerIsMale,
		const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
		const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
		TextureManager &textureManager, LevelDefinition *outLevelDef, LevelInfoDefinition *outLevelInfoDef,
		ArenaVoxelMappingCache *voxelCache, ArenaEntityMappingCache *entityCache,
		ArenaTransitionMappingCache *transitionCache)
	{
		for (SNInt map1Z = 0; map1Z < map1.getHeight(); map1Z++)
		{
			for (WEInt map1X = 0; map1X < map1.getWidth(); map1X++)
			{
				const ArenaTypes::VoxelID map1Voxel = map1.get(map1X, map1Z);

				// Skip air voxels.
				if (map1Voxel == 0)
				{
					continue;
				}

				const SNInt levelX = map1Z;
				const int levelY = 1;
				const WEInt levelZ = map1X;

				// Determine if this MAP1 voxel is for a voxel or entity.
				const uint8_t mostSigNibble = (map1Voxel & 0xF000) >> 12;
				const bool isVoxel = mostSigNibble != 0x8;

				if (isVoxel)
				{
					// Get voxel def ID from cache or create a new one.
					LevelDefinition::VoxelDefID voxelDefID;
					const auto iter = voxelCache->find(map1Voxel);
					if (iter != voxelCache->end())
					{
						voxelDefID = iter->second;
					}
					else
					{
						VoxelDefinition voxelDef = MapGeneration::makeVoxelDefFromMAP1(
							map1Voxel, mostSigNibble, worldType, inf, binaryAssetLibrary.getExeData());
						voxelDefID = outLevelInfoDef->addVoxelDef(std::move(voxelDef));
						voxelCache->insert(std::make_pair(map1Voxel, voxelDefID));
					}

					outLevelDef->setVoxel(levelX, levelY, levelZ, voxelDefID);

					// Try to make transition info if this MAP1 voxel is a transition.
					const std::optional<MapGeneration::TransitionDefGenInfo> transitionDefGenInfo =
						MapGeneration::tryMakeVoxelTransitionDefGenInfo(map1Voxel, worldType, inf);

					if (transitionDefGenInfo.has_value())
					{
						// Get transition def ID from cache or create a new one.
						LevelDefinition::TransitionDefID transitionDefID;
						const auto iter = transitionCache->find(map1Voxel);
						if (iter != transitionCache->end())
						{
							transitionDefID = iter->second;
						}
						else
						{
							TransitionDefinition transitionDef =
								MapGeneration::makeTransitionDef(*transitionDefGenInfo, rulerIsMale);
							transitionDefID = outLevelInfoDef->addTransitionDef(std::move(transitionDef));
							transitionCache->insert(std::make_pair(map1Voxel, transitionDefID));
						}

						const LevelInt3 transitionPos(levelX, levelY, levelZ);
						outLevelDef->addTransition(transitionDefID, transitionPos);
					}
				}
				else
				{
					// Get entity def ID from cache or create a new one.
					LevelDefinition::EntityDefID entityDefID;
					const auto iter = entityCache->find(map1Voxel);
					if (iter != entityCache->end())
					{
						entityDefID = iter->second;
					}
					else
					{
						const ArenaTypes::FlatIndex flatIndex = map1Voxel & 0x00FF;
						EntityDefinition entityDef;
						if (!MapGeneration::tryMakeEntityDefFromArenaFlat(flatIndex, worldType,
							interiorType, rulerIsMale, inf, charClassLibrary, entityDefLibrary,
							binaryAssetLibrary, textureManager, &entityDef))
						{
							DebugLogWarning("Couldn't make entity definition from FLAT \"" +
								std::to_string(flatIndex) + "\" with .INF \"" + inf.getName() + "\".");
							continue;
						}

						entityDefID = outLevelInfoDef->addEntityDef(std::move(entityDef));
						entityCache->insert(std::make_pair(map1Voxel, entityDefID));
					}

					const LevelDouble3 entityPos(
						static_cast<SNDouble>(levelX) + 0.50,
						1.0,
						static_cast<WEDouble>(levelZ) + 0.50);
					outLevelDef->addEntity(entityDefID, entityPos);
				}
			}
		}
	}

	// Converts .MIF/.RMD MAP2 voxels to modern voxel + entity format.
	void readArenaMAP2(const BufferView2D<const ArenaTypes::VoxelID> &map2, const INFFile &inf,
		LevelDefinition *outLevelDef, LevelInfoDefinition *outLevelInfoDef,
		ArenaVoxelMappingCache *voxelCache)
	{
		for (SNInt map2Z = 0; map2Z < map2.getHeight(); map2Z++)
		{
			for (WEInt map2X = 0; map2X < map2.getWidth(); map2X++)
			{
				const ArenaTypes::VoxelID map2Voxel = map2.get(map2X, map2Z);

				// Skip air voxels.
				if (map2Voxel == 0)
				{
					continue;
				}

				// Get voxel def ID from cache or create a new one.
				LevelDefinition::VoxelDefID voxelDefID;
				const auto iter = voxelCache->find(map2Voxel);
				if (iter != voxelCache->end())
				{
					voxelDefID = iter->second;
				}
				else
				{
					VoxelDefinition voxelDef = MapGeneration::makeVoxelDefFromMAP2(map2Voxel);
					voxelDefID = outLevelInfoDef->addVoxelDef(std::move(voxelDef));
					voxelCache->insert(std::make_pair(map2Voxel, voxelDefID));
				}

				// Duplicate voxels upward based on calculated height.
				const int yStart = 2;
				const int yEnd = yStart + ArenaLevelUtils::getMap2VoxelHeight(map2Voxel);
				for (int y = yStart; y < yEnd; y++)
				{
					const SNInt levelX = map2Z;
					const WEInt levelZ = map2X;
					outLevelDef->setVoxel(levelX, y, levelZ, voxelDefID);
				}
			}
		}
	}

	// Fills the equivalent MAP2 layer with duplicates of the ceiling block for a .MIF level
	// without MAP2 data.
	void readArenaCeiling(const INFFile &inf, LevelDefinition *outLevelDef,
		LevelInfoDefinition *outLevelInfoDef)
	{
		const INFFile::CeilingData &ceiling = inf.getCeiling();

		// @todo: get ceiling from .INFs without *CEILING (like START.INF). Maybe
		// hardcoding index 1 is enough?
		const int textureIndex = ceiling.textureIndex.value_or(1);

		VoxelDefinition voxelDef = VoxelDefinition::makeCeiling(textureIndex);
		LevelDefinition::VoxelDefID voxelDefID = outLevelInfoDef->addVoxelDef(std::move(voxelDef));

		for (SNInt levelX = 0; levelX < outLevelDef->getWidth(); levelX++)
		{
			for (WEInt levelZ = 0; levelZ < outLevelDef->getDepth(); levelZ++)
			{
				outLevelDef->setVoxel(levelX, 2, levelZ, voxelDefID);
			}
		}
	}

	void readArenaLock(const ArenaTypes::MIFLock &lock, const INFFile &inf, LevelDefinition *outLevelDef,
		LevelInfoDefinition *outLevelInfoDef, ArenaLockMappingCache *lockMappings)
	{
		// @todo: see if .INF file key data is relevant here.

		// Get lock def ID from cache or create a new one.
		LevelDefinition::LockDefID lockDefID;
		const auto iter = std::find_if(lockMappings->begin(), lockMappings->end(),
			[&lock](const std::pair<ArenaTypes::MIFLock, LevelDefinition::LockDefID> &pair)
		{
			const ArenaTypes::MIFLock &mifLock = pair.first;
			return (mifLock.x == lock.x) && (mifLock.y == lock.y) && (mifLock.lockLevel == lock.lockLevel);
		});

		if (iter != lockMappings->end())
		{
			lockDefID = iter->second;
		}
		else
		{
			LockDefinition lockDef = MapGeneration::makeLockDefFromArenaLock(lock);
			lockDefID = outLevelInfoDef->addLockDef(std::move(lockDef));
			lockMappings->push_back(std::make_pair(lock, lockDefID));
		}

		const LockDefinition &lockDef = outLevelInfoDef->getLockDef(lockDefID);
		const SNInt x = lockDef.getX();
		const int y = lockDef.getY();
		const WEInt z = lockDef.getZ();
		outLevelDef->addLock(lockDefID, LevelInt3(x, y, z));
	}

	void readArenaTrigger(const ArenaTypes::MIFTrigger &trigger, const INFFile &inf,
		LevelDefinition *outLevelDef, LevelInfoDefinition *outLevelInfoDef,
		ArenaTriggerMappingCache *triggerMappings)
	{
		// Get trigger def ID from cache or create a new one.
		LevelDefinition::TriggerDefID triggerDefID;
		const auto iter = std::find_if(triggerMappings->begin(), triggerMappings->end(),
			[&trigger](const std::pair<ArenaTypes::MIFTrigger, LevelDefinition::TriggerDefID> &pair)
		{
			const ArenaTypes::MIFTrigger &mifTrigger = pair.first;
			return (mifTrigger.x == trigger.x) && (mifTrigger.y == trigger.y) &&
				(mifTrigger.textIndex == trigger.textIndex) && (mifTrigger.soundIndex == trigger.soundIndex);
		});

		if (iter != triggerMappings->end())
		{
			triggerDefID = iter->second;
		}
		else
		{
			TriggerDefinition triggerDef = MapGeneration::makeTriggerDefFromArenaTrigger(trigger, inf);
			triggerDefID = outLevelInfoDef->addTriggerDef(std::move(triggerDef));
			triggerMappings->push_back(std::make_pair(trigger, triggerDefID));
		}

		const TriggerDefinition &triggerDef = outLevelInfoDef->getTriggerDef(triggerDefID);
		const SNInt x = triggerDef.getX();
		const int y = triggerDef.getY();
		const WEInt z = triggerDef.getZ();
		outLevelDef->addTrigger(triggerDefID, LevelInt3(x, y, z));
	}

	void generateArenaDungeonLevel(const MIFFile &mif, WEInt widthChunks, SNInt depthChunks,
		int levelUpBlock, const std::optional<int> &levelDownBlock, ArenaRandom &random,
		WorldType worldType, InteriorType interiorType, const std::optional<bool> &rulerIsMale,
		const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
		const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
		TextureManager &textureManager, LevelDefinition *outLevelDef, LevelInfoDefinition *outLevelInfoDef,
		ArenaVoxelMappingCache *florMappings, ArenaVoxelMappingCache *map1Mappings,
		ArenaEntityMappingCache *entityMappings, ArenaLockMappingCache *lockMappings,
		ArenaTriggerMappingCache *triggerMappings, ArenaTransitionMappingCache *transitionMappings)
	{
		// Create buffers for level blocks.
		Buffer2D<ArenaTypes::VoxelID> levelFLOR(mif.getWidth() * widthChunks, mif.getDepth() * depthChunks);
		Buffer2D<ArenaTypes::VoxelID> levelMAP1(levelFLOR.getWidth(), levelFLOR.getHeight());
		levelFLOR.fill(0);
		levelMAP1.fill(0);

		const int tileSet = random.next() % 4;

		for (SNInt row = 0; row < depthChunks; row++)
		{
			const SNInt zOffset = row * ArenaInteriorUtils::DUNGEON_CHUNK_DIM;
			for (WEInt column = 0; column < widthChunks; column++)
			{
				const WEInt xOffset = column * ArenaInteriorUtils::DUNGEON_CHUNK_DIM;

				// Get the selected level from the random chunks .MIF file.
				const int blockIndex = (tileSet * 8) + (random.next() % 8);
				const auto &blockLevel = mif.getLevel(blockIndex);
				const BufferView2D<const ArenaTypes::VoxelID> &blockFLOR = blockLevel.getFLOR();
				const BufferView2D<const ArenaTypes::VoxelID> &blockMAP1 = blockLevel.getMAP1();

				// Copy block data to temp buffers.
				for (SNInt z = 0; z < ArenaInteriorUtils::DUNGEON_CHUNK_DIM; z++)
				{
					for (WEInt x = 0; x < ArenaInteriorUtils::DUNGEON_CHUNK_DIM; x++)
					{
						const ArenaTypes::VoxelID srcFlorVoxel = blockFLOR.get(x, z);
						const ArenaTypes::VoxelID srcMap1Voxel = blockMAP1.get(x, z);
						const WEInt dstX = xOffset + x;
						const SNInt dstZ = zOffset + z;
						levelFLOR.set(dstX, dstZ, srcFlorVoxel);
						levelMAP1.set(dstX, dstZ, srcMap1Voxel);
					}
				}

				// Assign locks to the current block.
				const BufferView<const ArenaTypes::MIFLock> &blockLOCK = blockLevel.getLOCK();
				for (int i = 0; i < blockLOCK.getCount(); i++)
				{
					const auto &lock = blockLOCK.get(i);

					ArenaTypes::MIFLock tempLock;
					tempLock.x = xOffset + lock.x;
					tempLock.y = zOffset + lock.y;
					tempLock.lockLevel = lock.lockLevel;

					MapGeneration::readArenaLock(tempLock, inf, outLevelDef, outLevelInfoDef, lockMappings);
				}

				// Assign text/sound triggers to the current block.
				const BufferView<const ArenaTypes::MIFTrigger> &blockTRIG = blockLevel.getTRIG();
				for (int i = 0; i < blockTRIG.getCount(); i++)
				{
					const auto &trigger = blockTRIG.get(i);

					ArenaTypes::MIFTrigger tempTrigger;
					tempTrigger.x = xOffset + trigger.x;
					tempTrigger.y = zOffset + trigger.y;
					tempTrigger.textIndex = trigger.textIndex;
					tempTrigger.soundIndex = trigger.soundIndex;

					MapGeneration::readArenaTrigger(tempTrigger, inf, outLevelDef, outLevelInfoDef,
						triggerMappings);
				}
			}
		}

		// Draw perimeter blocks. First top and bottom, then right and left.
		constexpr ArenaTypes::VoxelID perimeterVoxel = 0x7800;
		for (WEInt x = 0; x < levelMAP1.getWidth(); x++)
		{
			levelMAP1.set(x, 0, perimeterVoxel);
			levelMAP1.set(x, levelMAP1.getHeight() - 1, perimeterVoxel);
		}

		for (SNInt z = 1; z < (levelMAP1.getHeight() - 1); z++)
		{
			levelMAP1.set(0, z, perimeterVoxel);
			levelMAP1.set(levelMAP1.getWidth() - 1, z, perimeterVoxel);
		}

		// Put transition block(s).
		const uint8_t levelUpVoxelByte = *inf.getLevelUpIndex() + 1;
		WEInt levelUpX;
		SNInt levelUpZ;
		ArenaInteriorUtils::unpackLevelChangeVoxel(levelUpBlock, &levelUpX, &levelUpZ);
		levelMAP1.set(ArenaInteriorUtils::offsetLevelChangeVoxel(levelUpX),
			ArenaInteriorUtils::offsetLevelChangeVoxel(levelUpZ),
			ArenaInteriorUtils::convertLevelChangeVoxel(levelUpVoxelByte));

		if (levelDownBlock.has_value())
		{
			const uint8_t levelDownVoxelByte = *inf.getLevelDownIndex() + 1;
			WEInt levelDownX;
			SNInt levelDownZ;
			ArenaInteriorUtils::unpackLevelChangeVoxel(*levelDownBlock, &levelDownX, &levelDownZ);
			levelMAP1.set(ArenaInteriorUtils::offsetLevelChangeVoxel(levelDownX),
				ArenaInteriorUtils::offsetLevelChangeVoxel(levelDownZ),
				ArenaInteriorUtils::convertLevelChangeVoxel(levelDownVoxelByte));
		}

		// Convert temp voxel buffers to the modern format.
		const BufferView2D<const ArenaTypes::VoxelID> levelFlorView(
			levelFLOR.get(), levelFLOR.getWidth(), levelFLOR.getHeight());
		const BufferView2D<const ArenaTypes::VoxelID> levelMap1View(
			levelMAP1.get(), levelMAP1.getWidth(), levelMAP1.getHeight());
		MapGeneration::readArenaFLOR(levelFlorView, worldType, interiorType, rulerIsMale, inf,
			charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, outLevelDef,
			outLevelInfoDef, florMappings, entityMappings);
		MapGeneration::readArenaMAP1(levelMap1View, worldType, interiorType, rulerIsMale, inf,
			charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, outLevelDef,
			outLevelInfoDef, map1Mappings, entityMappings, transitionMappings);

		// Generate ceiling (if any).
		if (!inf.getCeiling().outdoorDungeon)
		{
			MapGeneration::readArenaCeiling(inf, outLevelDef, outLevelInfoDef);
		}
	}

	void generateArenaCityBuildingNames(uint32_t citySeed, int raceID, bool coastal,
		const std::string_view &cityTypeName,
		const LocationDefinition::CityDefinition::MainQuestTempleOverride *mainQuestTempleOverride,
		ArenaRandom &random, const BinaryAssetLibrary &binaryAssetLibrary,
		const TextAssetLibrary &textAssetLibrary, LevelDefinition *outLevelDef,
		LevelInfoDefinition *outLevelInfoDef)
	{
		const auto &exeData = binaryAssetLibrary.getExeData();
		const Int2 localCityPoint = LocationUtils::getLocalCityPoint(citySeed);

		// Lambda for looping through main-floor voxels and generating names for *MENU blocks that
		// match the given menu type.
		auto generateNames = [&citySeed, raceID, coastal, &cityTypeName, mainQuestTempleOverride,
			&random, &textAssetLibrary, outLevelDef, outLevelInfoDef, &exeData, &localCityPoint](
				ArenaTypes::MenuType menuType)
		{
			if ((menuType == ArenaTypes::MenuType::Equipment) ||
				(menuType == ArenaTypes::MenuType::Temple))
			{
				citySeed = (localCityPoint.x << 16) + localCityPoint.y;
				random.srand(citySeed);
			}

			std::vector<int> seen;
			auto hashInSeen = [&seen](int hash)
			{
				return std::find(seen.begin(), seen.end(), hash) != seen.end();
			};

			// Lambdas for creating tavern, equipment store, and temple building names.
			auto createTavernName = [coastal, &exeData](int prefixIndex, int suffixIndex)
			{
				const auto &tavernPrefixes = exeData.cityGen.tavernPrefixes;
				const auto &tavernSuffixes = coastal ?
					exeData.cityGen.tavernMarineSuffixes : exeData.cityGen.tavernSuffixes;
				DebugAssertIndex(tavernPrefixes, prefixIndex);
				DebugAssertIndex(tavernSuffixes, suffixIndex);
				return tavernPrefixes[prefixIndex] + ' ' + tavernSuffixes[suffixIndex];
			};

			auto createEquipmentName = [raceID, &cityTypeName, &random, &textAssetLibrary, &exeData](
				int prefixIndex, int suffixIndex, SNInt x, WEInt z)
			{
				const auto &equipmentPrefixes = exeData.cityGen.equipmentPrefixes;
				const auto &equipmentSuffixes = exeData.cityGen.equipmentSuffixes;

				// Equipment store names can have variables in them.
				DebugAssertIndex(equipmentPrefixes, prefixIndex);
				DebugAssertIndex(equipmentSuffixes, suffixIndex);
				std::string str = equipmentPrefixes[prefixIndex] + ' ' + equipmentSuffixes[suffixIndex];

				// Replace %ct with city type name.
				size_t index = str.find("%ct");
				if (index != std::string::npos)
				{
					str.replace(index, 3, cityTypeName);
				}

				// Replace %ef with generated male first name from (y<<16)+x seed. Use a local RNG for
				// modifications to building names. Swap and reverse the XZ dimensions so they fit the
				// original XY values in Arena.
				index = str.find("%ef");
				if (index != std::string::npos)
				{
					ArenaRandom nameRandom((x << 16) + z);
					const std::string maleFirstName = [raceID, &textAssetLibrary, &nameRandom]()
					{
						constexpr bool isMale = true;
						const std::string name = textAssetLibrary.generateNpcName(raceID, isMale, nameRandom);
						const std::string firstName = String::split(name).front();
						return firstName;
					}();

					str.replace(index, 3, maleFirstName);
				}

				// Replace %n with generated male name from (x<<16)+y seed.
				index = str.find("%n");
				if (index != std::string::npos)
				{
					ArenaRandom nameRandom((z << 16) + x);
					constexpr bool isMale = true;
					const std::string maleName = textAssetLibrary.generateNpcName(raceID, isMale, nameRandom);
					str.replace(index, 2, maleName);
				}

				return str;
			};

			auto createTempleName = [&exeData](int model, int suffixIndex)
			{
				const auto &templePrefixes = exeData.cityGen.templePrefixes;
				const auto &temple1Suffixes = exeData.cityGen.temple1Suffixes;
				const auto &temple2Suffixes = exeData.cityGen.temple2Suffixes;
				const auto &temple3Suffixes = exeData.cityGen.temple3Suffixes;

				const std::string &templeSuffix = [&temple1Suffixes, &temple2Suffixes, &temple3Suffixes,
					model, suffixIndex]() -> const std::string&
				{
					if (model == 0)
					{
						DebugAssertIndex(temple1Suffixes, suffixIndex);
						return temple1Suffixes[suffixIndex];
					}
					else if (model == 1)
					{
						DebugAssertIndex(temple2Suffixes, suffixIndex);
						return temple2Suffixes[suffixIndex];
					}
					else
					{
						DebugAssertIndex(temple3Suffixes, suffixIndex);
						return temple3Suffixes[suffixIndex];
					}
				}();

				DebugAssertIndex(templePrefixes, model);
				return templePrefixes[model] + templeSuffix;
			};

			// The lambda called for each main-floor voxel in the area.
			auto tryGenerateBlockName = [menuType, &random, outLevelDef, outLevelInfoDef, &seen, &hashInSeen,
				&createTavernName, &createEquipmentName, &createTempleName](SNInt x, WEInt z)
			{
				// See if the current voxel is a *MENU block and matches the target menu type.
				const bool matchesTargetType = [x, z, menuType, outLevelDef, outLevelInfoDef]()
				{
					const LevelDefinition::VoxelDefID voxelDefID = outLevelDef->getVoxel(x, 1, z);
					const VoxelDefinition &voxelDef = outLevelInfoDef->getVoxelDef(voxelDefID);
					constexpr WorldType worldType = WorldType::City;
					return (voxelDef.dataType == VoxelDataType::Wall) && voxelDef.wall.isMenu() &&
						(ArenaVoxelUtils::getMenuType(voxelDef.wall.menuID, worldType) == menuType);
				}();

				if (matchesTargetType)
				{
					// Get the *MENU block's display name.
					int hash;
					std::string name;

					if (menuType == ArenaTypes::MenuType::Tavern)
					{
						// Tavern.
						int prefixIndex, suffixIndex;
						do
						{
							prefixIndex = random.next() % 23;
							suffixIndex = random.next() % 23;
							hash = (prefixIndex << 8) + suffixIndex;
						} while (hashInSeen(hash));

						name = createTavernName(prefixIndex, suffixIndex);
					}
					else if (menuType == ArenaTypes::MenuType::Equipment)
					{
						// Equipment store.
						int prefixIndex, suffixIndex;
						do
						{
							prefixIndex = random.next() % 20;
							suffixIndex = random.next() % 10;
							hash = (prefixIndex << 8) + suffixIndex;
						} while (hashInSeen(hash));

						name = createEquipmentName(prefixIndex, suffixIndex, x, z);
					}
					else
					{
						// Temple.
						int model, suffixIndex;
						do
						{
							model = random.next() % 3;
							const std::array<int, 3> ModelVars = { 5, 9, 10 };
							const int vars = ModelVars.at(model);
							suffixIndex = random.next() % vars;
							hash = (model << 8) + suffixIndex;
						} while (hashInSeen(hash));

						name = createTempleName(model, suffixIndex);
					}

					const LevelDefinition::BuildingNameID buildingNameID =
						outLevelInfoDef->addBuildingName(std::move(name));
					outLevelDef->addBuildingName(buildingNameID, LevelInt3(x, 1, z));
					seen.push_back(hash);
				}
			};

			// Start at the top-right corner of the map, running right to left and top to bottom.
			for (SNInt x = 0; x < outLevelDef->getWidth(); x++)
			{
				for (WEInt z = 0; z < outLevelDef->getDepth(); z++)
				{
					tryGenerateBlockName(x, z);
				}
			}

			// Fix some edge cases with main quest cities.
			if ((menuType == ArenaTypes::MenuType::Temple) &&
				(mainQuestTempleOverride != nullptr))
			{
				const int modelIndex = mainQuestTempleOverride->modelIndex;
				const int suffixIndex = mainQuestTempleOverride->suffixIndex;

				// Added an index variable in this solution since the original game seems to store
				// its building names in a way other than with a vector.
				const LevelDefinition::BuildingNameID buildingNameID =
					mainQuestTempleOverride->menuNamesIndex;

				std::string buildingName = createTempleName(modelIndex, suffixIndex);
				outLevelInfoDef->setBuildingNameOverride(buildingNameID, std::move(buildingName));
			}
		};

		generateNames(ArenaTypes::MenuType::Tavern);
		generateNames(ArenaTypes::MenuType::Equipment);
		generateNames(ArenaTypes::MenuType::Temple);
	}

	// Using a separate building name info struct because the same level definition might be
	// used in multiple places in the wild, so it can't store the building name IDs.
	void generateArenaWildChunkBuildingNames(uint32_t wildChunkSeed, const LevelDefinition &levelDef,
		const BinaryAssetLibrary &binaryAssetLibrary,
		MapGeneration::WildChunkBuildingNameInfo *outBuildingNameInfo,
		LevelInfoDefinition *outLevelInfoDef, ArenaBuildingNameMappingCache *buildingNameMappings)
	{
		const auto &exeData = binaryAssetLibrary.getExeData();

		// Lambda for searching for a *MENU voxel of the given type in the chunk and generating
		// a name for it if found.
		auto tryGenerateChunkBuildingName = [wildChunkSeed, &levelDef, outBuildingNameInfo,
			outLevelInfoDef, buildingNameMappings, &exeData](ArenaTypes::MenuType menuType)
		{
			auto createTavernName = [&exeData](int prefixIndex, int suffixIndex)
			{
				const auto &tavernPrefixes = exeData.cityGen.tavernPrefixes;
				const auto &tavernSuffixes = exeData.cityGen.tavernSuffixes;
				DebugAssertIndex(tavernPrefixes, prefixIndex);
				DebugAssertIndex(tavernSuffixes, suffixIndex);
				return tavernPrefixes[prefixIndex] + ' ' + tavernSuffixes[suffixIndex];
			};

			auto createTempleName = [&exeData](int model, int suffixIndex)
			{
				const auto &templePrefixes = exeData.cityGen.templePrefixes;
				const auto &temple1Suffixes = exeData.cityGen.temple1Suffixes;
				const auto &temple2Suffixes = exeData.cityGen.temple2Suffixes;
				const auto &temple3Suffixes = exeData.cityGen.temple3Suffixes;

				const std::string &templeSuffix = [&temple1Suffixes, &temple2Suffixes,
					&temple3Suffixes, model, suffixIndex]() -> const std::string&
				{
					if (model == 0)
					{
						DebugAssertIndex(temple1Suffixes, suffixIndex);
						return temple1Suffixes[suffixIndex];
					}
					else if (model == 1)
					{
						DebugAssertIndex(temple2Suffixes, suffixIndex);
						return temple2Suffixes[suffixIndex];
					}
					else
					{
						DebugAssertIndex(temple3Suffixes, suffixIndex);
						return temple3Suffixes[suffixIndex];
					}
				}();

				DebugAssertIndex(templePrefixes, model);
				return templePrefixes[model] + templeSuffix;
			};

			// The lambda called for each main-floor voxel in the chunk.
			auto tryGenerateBlockName = [wildChunkSeed, &levelDef, outBuildingNameInfo, outLevelInfoDef,
				buildingNameMappings, menuType, &createTavernName, &createTempleName](SNInt x, WEInt z) -> bool
			{
				ArenaRandom random(wildChunkSeed);

				// See if the current voxel is a *MENU block and matches the target menu type.
				const bool matchesTargetType = [&levelDef, outLevelInfoDef, menuType, x, z]()
				{
					const LevelDefinition::VoxelDefID voxelDefID = levelDef.getVoxel(x, 1, z);
					const VoxelDefinition &voxelDef = outLevelInfoDef->getVoxelDef(voxelDefID);
					constexpr WorldType worldType = WorldType::Wilderness;
					return (voxelDef.dataType == VoxelDataType::Wall) && voxelDef.wall.isMenu() &&
						(ArenaVoxelUtils::getMenuType(voxelDef.wall.menuID, worldType) == menuType);
				}();

				if (matchesTargetType)
				{
					// Get the *MENU block's display name.
					std::string name = [menuType, &random, &createTavernName, &createTempleName]()
					{
						if (menuType == ArenaTypes::MenuType::Tavern)
						{
							const int prefixIndex = random.next() % 23;
							const int suffixIndex = random.next() % 23;
							return createTavernName(prefixIndex, suffixIndex);
						}
						else if (menuType == ArenaTypes::MenuType::Temple)
						{
							const int model = random.next() % 3;
							constexpr std::array<int, 3> ModelVars = { 5, 9, 10 };
							DebugAssertIndex(ModelVars, model);
							const int vars = ModelVars[model];
							const int suffixIndex = random.next() % vars;
							return createTempleName(model, suffixIndex);
						}
						else
						{
							DebugUnhandledReturnMsg(std::string, std::to_string(static_cast<int>(menuType)));
						}
					}();

					// Set building name info for the given menu type.
					const auto iter = buildingNameMappings->find(name);
					if (iter != buildingNameMappings->end())
					{
						outBuildingNameInfo->setBuildingNameID(menuType, iter->second);
					}
					else
					{
						const LevelDefinition::BuildingNameID buildingNameID =
							outLevelInfoDef->addBuildingName(std::string(name));
						outBuildingNameInfo->setBuildingNameID(menuType, buildingNameID);
						buildingNameMappings->emplace(std::move(name), buildingNameID);
					}

					return true;
				}
				else
				{
					return false;
				}
			};

			// Iterate blocks in the chunk in any order and stop once a relevant voxel for
			// generating the name has been found.
			for (SNInt x = 0; x < RMDFile::DEPTH; x++)
			{
				for (WEInt z = 0; z < RMDFile::WIDTH; z++)
				{
					if (tryGenerateBlockName(x, z))
					{
						break;
					}
				}
			}
		};

		tryGenerateChunkBuildingName(ArenaTypes::MenuType::Tavern);
		tryGenerateChunkBuildingName(ArenaTypes::MenuType::Temple);
	}
}

void MapGeneration::InteriorGenInfo::Prefab::init(std::string &&mifName, InteriorType interiorType,
	const std::optional<bool> &rulerIsMale)
{
	this->mifName = std::move(mifName);
	this->interiorType = interiorType;
	this->rulerIsMale = rulerIsMale;
}

void MapGeneration::InteriorGenInfo::Dungeon::init(uint32_t dungeonSeed, WEInt widthChunks,
	SNInt depthChunks, bool isArtifactDungeon)
{
	this->dungeonSeed = dungeonSeed;
	this->widthChunks = widthChunks;
	this->depthChunks = depthChunks;
	this->isArtifactDungeon = isArtifactDungeon;
}

MapGeneration::InteriorGenInfo::InteriorGenInfo()
{
	this->type = static_cast<InteriorGenInfo::Type>(-1);
}

void MapGeneration::InteriorGenInfo::init(InteriorGenInfo::Type type)
{
	this->type = type;
}

void MapGeneration::InteriorGenInfo::initPrefab(std::string &&mifName, InteriorType interiorType,
	const std::optional<bool> &rulerIsMale)
{
	this->init(InteriorGenInfo::Type::Prefab);
	this->prefab.init(std::move(mifName), interiorType, rulerIsMale);
}

void MapGeneration::InteriorGenInfo::initDungeon(uint32_t dungeonSeed, WEInt widthChunks,
	SNInt depthChunks, bool isArtifactDungeon)
{
	this->init(InteriorGenInfo::Type::Dungeon);
	this->dungeon.init(dungeonSeed, widthChunks, depthChunks, isArtifactDungeon);
}

MapGeneration::InteriorGenInfo::Type MapGeneration::InteriorGenInfo::getType() const
{
	return this->type;
}

const MapGeneration::InteriorGenInfo::Prefab &MapGeneration::InteriorGenInfo::getPrefab() const
{
	DebugAssert(this->type == InteriorGenInfo::Type::Prefab);
	return this->prefab;
}

const MapGeneration::InteriorGenInfo::Dungeon &MapGeneration::InteriorGenInfo::getDungeon() const
{
	DebugAssert(this->type == InteriorGenInfo::Type::Dungeon);
	return this->dungeon;
}

void MapGeneration::CityGenInfo::init(std::string &&mifName, std::string &&cityTypeName,
	uint32_t citySeed, int raceID, bool isPremade, bool coastal, Buffer<uint8_t> &&reservedBlocks,
	const std::optional<LocationDefinition::CityDefinition::MainQuestTempleOverride> *mainQuestTempleOverride,
	WEInt blockStartPosX, SNInt blockStartPosY, int cityBlocksPerSide)
{
	this->mifName = std::move(mifName);
	this->cityTypeName = std::move(cityTypeName);
	this->citySeed = citySeed;
	this->raceID = raceID;
	this->isPremade = isPremade;
	this->coastal = coastal;
	this->reservedBlocks = std::move(reservedBlocks);
	this->mainQuestTempleOverride = (mainQuestTempleOverride != nullptr) ? *mainQuestTempleOverride : std::nullopt;
	this->blockStartPosX = blockStartPosX;
	this->blockStartPosY = blockStartPosY;
	this->cityBlocksPerSide = cityBlocksPerSide;
}

void MapGeneration::WildGenInfo::init(Buffer2D<ArenaWildUtils::WildBlockID> &&wildBlockIDs,
	uint32_t fallbackSeed)
{
	this->wildBlockIDs = std::move(wildBlockIDs);
	this->fallbackSeed = fallbackSeed;
}

void MapGeneration::WildChunkBuildingNameInfo::init(const ChunkInt2 &chunk)
{
	this->chunk = chunk;
}

const ChunkInt2 &MapGeneration::WildChunkBuildingNameInfo::getChunk() const
{
	return this->chunk;
}

bool MapGeneration::WildChunkBuildingNameInfo::hasBuildingNames() const
{
	return this->ids.size() > 0;
}

bool MapGeneration::WildChunkBuildingNameInfo::tryGetBuildingNameID(
	ArenaTypes::MenuType menuType, LevelDefinition::BuildingNameID *outID) const
{
	const auto iter = this->ids.find(menuType);
	if (iter != this->ids.end())
	{
		*outID = iter->second;
		return true;
	}
	else
	{
		return false;
	}
}

void MapGeneration::WildChunkBuildingNameInfo::setBuildingNameID(
	ArenaTypes::MenuType menuType, LevelDefinition::BuildingNameID id)
{
	const auto iter = this->ids.find(menuType);
	if (iter != this->ids.end())
	{
		iter->second = id;
	}
	else
	{
		this->ids.emplace(menuType, id);
	}
}

void MapGeneration::TransitionDefGenInfo::init(TransitionType transitionType,
	const std::optional<InteriorType> &interiorType, const std::optional<bool> &isLevelUp)
{
	this->transitionType = transitionType;
	this->interiorType = interiorType;
	this->isLevelUp = isLevelUp;
}

void MapGeneration::readMifVoxels(const BufferView<const MIFFile::Level> &levels, WorldType worldType,
	const std::optional<InteriorType> &interiorType, const std::optional<bool> &rulerIsMale,
	const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
	const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
	TextureManager &textureManager, BufferView<LevelDefinition> &outLevelDefs,
	LevelInfoDefinition *outLevelInfoDef)
{
	// Each .MIF level voxel is unpacked into either a voxel or entity. These caches point to
	// previously-added definitions in the level info def.
	ArenaVoxelMappingCache florMappings, map1Mappings, map2Mappings;
	ArenaEntityMappingCache entityMappings;
	ArenaTransitionMappingCache transitionMappings;

	for (int i = 0; i < levels.getCount(); i++)
	{
		const MIFFile::Level &level = levels.get(i);
		LevelDefinition &levelDef = outLevelDefs.get(i);
		MapGeneration::readArenaFLOR(level.getFLOR(), worldType, interiorType, rulerIsMale, inf,
			charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, &levelDef,
			outLevelInfoDef, &florMappings, &entityMappings);
		MapGeneration::readArenaMAP1(level.getMAP1(), worldType, interiorType, rulerIsMale, inf,
			charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, &levelDef,
			outLevelInfoDef, &map1Mappings, &entityMappings, &transitionMappings);

		// If there is MAP2 data, use it for the ceiling layer, otherwise replicate a single ceiling
		// block across the whole ceiling if not in an outdoor dungeon.
		if (level.getMAP2().isValid())
		{
			MapGeneration::readArenaMAP2(level.getMAP2(), inf, &levelDef, outLevelInfoDef, &map2Mappings);
		}
		else if (!inf.getCeiling().outdoorDungeon)
		{
			MapGeneration::readArenaCeiling(inf, &levelDef, outLevelInfoDef);
		}
	}
}

void MapGeneration::generateMifDungeon(const MIFFile &mif, int levelCount, WEInt widthChunks,
	SNInt depthChunks, const INFFile &inf, ArenaRandom &random, WorldType worldType,
	InteriorType interiorType, const std::optional<bool> &rulerIsMale,
	const CharacterClassLibrary &charClassLibrary, const EntityDefinitionLibrary &entityDefLibrary,
	const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager,
	BufferView<LevelDefinition> &outLevelDefs, LevelInfoDefinition *outLevelInfoDef,
	LevelInt2 *outStartPoint)
{
	ArenaVoxelMappingCache florMappings, map1Mappings;
	ArenaEntityMappingCache entityMappings;
	ArenaLockMappingCache lockMappings;
	ArenaTriggerMappingCache triggerMappings;
	ArenaTransitionMappingCache transitionMappings;

	// Store the seed for later, to be used with block selection.
	const uint32_t seed2 = random.getSeed();

	// Determine transition blocks (*LEVELUP/*LEVELDOWN) that will appear in the dungeon.
	auto getNextTransBlock = [widthChunks, depthChunks, &random]()
	{
		const SNInt tY = random.next() % depthChunks;
		const WEInt tX = random.next() % widthChunks;
		return ArenaInteriorUtils::packLevelChangeVoxel(tX, tY);
	};

	// Packed coordinates for transition blocks.
	// @todo: maybe this could be an int pair so packing is not required.
	std::vector<int> transitions;

	// Handle initial case where transitions list is empty (for i == 0).
	transitions.push_back(getNextTransBlock());

	// Handle general case for transitions list additions.
	for (int i = 1; i < levelCount; i++)
	{
		int transBlock;
		do
		{
			transBlock = getNextTransBlock();
		} while (transBlock == transitions.back());

		transitions.push_back(transBlock);
	}

	// Generate each level, deciding which dungeon blocks to use.
	for (int i = 0; i < levelCount; i++)
	{
		random.srand(seed2 + i);

		// Determine level up/down blocks.
		DebugAssertIndex(transitions, i);
		const int levelUpBlock = transitions[i];
		const std::optional<int> levelDownBlock = [&transitions, levelCount, i]() -> std::optional<int>
		{
			if (i < (levelCount - 1))
			{
				const int index = DebugMakeIndex(transitions, i + 1);
				return transitions[index];
			}
			else
			{
				// No *LEVELDOWN block on the lowest level.
				return std::nullopt;
			}
		}();

		LevelDefinition &levelDef = outLevelDefs.get(i);
		MapGeneration::generateArenaDungeonLevel(mif, widthChunks, depthChunks, levelUpBlock,
			levelDownBlock, random, worldType, interiorType, rulerIsMale, inf, charClassLibrary,
			entityDefLibrary, binaryAssetLibrary, textureManager, &levelDef, outLevelInfoDef,
			&florMappings, &map1Mappings, &entityMappings, &lockMappings, &triggerMappings,
			&transitionMappings);
	}

	// The start point depends on where the level up voxel is on the first level.
	DebugAssertIndex(transitions, 0);
	const int firstTransition = transitions[0];
	WEInt firstTransitionChunkX;
	SNInt firstTransitionChunkZ;
	ArenaInteriorUtils::unpackLevelChangeVoxel(
		firstTransition, &firstTransitionChunkX, &firstTransitionChunkZ);

	// Convert it from the old coordinate system to the new one.
	const OriginalInt2 startPoint(
		ArenaInteriorUtils::offsetLevelChangeVoxel(firstTransitionChunkX),
		ArenaInteriorUtils::offsetLevelChangeVoxel(firstTransitionChunkZ));
	*outStartPoint = VoxelUtils::originalVoxelToNewVoxel(startPoint);
}

void MapGeneration::generateMifCity(const MIFFile &mif, uint32_t citySeed, int raceID, bool isPremade,
	const BufferView<const uint8_t> &reservedBlocks, WEInt blockStartPosX, SNInt blockStartPosY,
	int cityBlocksPerSide, bool coastal, const std::string_view &cityTypeName,
	const LocationDefinition::CityDefinition::MainQuestTempleOverride *mainQuestTempleOverride,
	const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
	const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
	const TextAssetLibrary &textAssetLibrary, TextureManager &textureManager,
	LevelDefinition *outLevelDef, LevelInfoDefinition *outLevelInfoDef)
{
	ArenaVoxelMappingCache florMappings, map1Mappings, map2Mappings;
	ArenaEntityMappingCache entityMappings;
	ArenaTransitionMappingCache transitionMappings;

	// Only one level in a city .MIF.
	const MIFFile::Level &mifLevel = mif.getLevel(0);

	// Create temp voxel data buffers and write the city skeleton data to them.
	Buffer2D<ArenaTypes::VoxelID> tempFlor(mif.getWidth(), mif.getDepth());
	Buffer2D<ArenaTypes::VoxelID> tempMap1(mif.getWidth(), mif.getDepth());
	Buffer2D<ArenaTypes::VoxelID> tempMap2(mif.getWidth(), mif.getDepth());
	BufferView2D<ArenaTypes::VoxelID> tempFlorView(tempFlor.get(), tempFlor.getWidth(), tempFlor.getHeight());
	BufferView2D<ArenaTypes::VoxelID> tempMap1View(tempMap1.get(), tempMap1.getWidth(), tempMap1.getHeight());
	BufferView2D<ArenaTypes::VoxelID> tempMap2View(tempMap2.get(), tempMap2.getWidth(), tempMap2.getHeight());
	ArenaCityUtils::writeSkeleton(mifLevel, tempFlorView, tempMap1View, tempMap2View);

	// Use the city's seed for random chunk generation. It is modified later during building
	// name generation.
	ArenaRandom random(citySeed);

	if (!isPremade)
	{
		// Generate procedural city data and write it into the temp buffers.
		const OriginalInt2 blockStartPosition(blockStartPosX, blockStartPosY);
		ArenaCityUtils::generateCity(citySeed, cityBlocksPerSide, mif.getWidth(), reservedBlocks,
			blockStartPosition, random, binaryAssetLibrary, tempFlor, tempMap1, tempMap2);
	}

	// Run the palace gate graphic algorithm over the perimeter of the MAP1 data.
	ArenaCityUtils::revisePalaceGraphics(tempMap1, mif.getDepth(), mif.getWidth());

	const BufferView2D<const ArenaTypes::VoxelID> tempFlorConstView(
		tempFlor.get(), tempFlor.getWidth(), tempFlor.getHeight());
	const BufferView2D<const ArenaTypes::VoxelID> tempMap1ConstView(
		tempMap1.get(), tempMap1.getWidth(), tempMap1.getHeight());
	const BufferView2D<const ArenaTypes::VoxelID> tempMap2ConstView(
		tempMap2.get(), tempMap2.getWidth(), tempMap2.getHeight());

	constexpr WorldType worldType = WorldType::City;
	constexpr std::optional<InteriorType> interiorType; // City is not an interior.
	constexpr std::optional<bool> rulerIsMale; // Not necessary for city.

	MapGeneration::readArenaFLOR(tempFlorConstView, worldType, interiorType, rulerIsMale, inf,
		charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, outLevelDef,
		outLevelInfoDef, &florMappings, &entityMappings);
	MapGeneration::readArenaMAP1(tempMap1ConstView, worldType, interiorType, rulerIsMale, inf,
		charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, outLevelDef,
		outLevelInfoDef, &map1Mappings, &entityMappings, &transitionMappings);
	MapGeneration::readArenaMAP2(tempMap2ConstView, inf, outLevelDef, outLevelInfoDef, &map2Mappings);
	MapGeneration::generateArenaCityBuildingNames(citySeed, raceID, coastal, cityTypeName,
		mainQuestTempleOverride, random, binaryAssetLibrary, textAssetLibrary, outLevelDef,
		outLevelInfoDef);
}

void MapGeneration::generateRmdWilderness(const BufferView<const ArenaWildUtils::WildBlockID> &uniqueWildBlockIDs,
	const BufferView2D<const int> &levelDefIndices, const INFFile &inf,
	const CharacterClassLibrary &charClassLibrary, const EntityDefinitionLibrary &entityDefLibrary,
	const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager,
	BufferView<LevelDefinition> &outLevelDefs, LevelInfoDefinition *outLevelInfoDef,
	std::vector<MapGeneration::WildChunkBuildingNameInfo> *outBuildingNameInfos)
{
	DebugAssert(uniqueWildBlockIDs.getCount() == outLevelDefs.getCount());

	ArenaVoxelMappingCache florMappings, map1Mappings, map2Mappings;
	ArenaEntityMappingCache entityMappings;
	ArenaTransitionMappingCache transitionMappings;
	ArenaBuildingNameMappingCache buildingNameMappings;

	// Create temp voxel data buffers to be used by each wilderness chunk.
	constexpr int chunkDim = ChunkUtils::CHUNK_DIM;
	Buffer2D<ArenaTypes::VoxelID> tempFlor(chunkDim, chunkDim);
	Buffer2D<ArenaTypes::VoxelID> tempMap1(chunkDim, chunkDim);
	Buffer2D<ArenaTypes::VoxelID> tempMap2(chunkDim, chunkDim);

	for (int i = 0; i < uniqueWildBlockIDs.getCount(); i++)
	{
		const ArenaWildUtils::WildBlockID wildBlockID = uniqueWildBlockIDs.get(i);
		const auto &rmdFiles = binaryAssetLibrary.getWildernessChunks();
		const int rmdIndex = DebugMakeIndex(rmdFiles, wildBlockID - 1);
		const RMDFile &rmd = rmdFiles[rmdIndex];
		const BufferView2D<const ArenaTypes::VoxelID> rmdFLOR = rmd.getFLOR();
		const BufferView2D<const ArenaTypes::VoxelID> rmdMAP1 = rmd.getMAP1();
		const BufferView2D<const ArenaTypes::VoxelID> rmdMAP2 = rmd.getMAP2();

		// Copy .RMD voxels into temp buffers.
		for (int y = 0; y < tempFlor.getHeight(); y++)
		{
			for (int x = 0; x < tempFlor.getWidth(); x++)
			{
				const ArenaTypes::VoxelID rmdFlorID = rmdFLOR.get(x, y);
				const ArenaTypes::VoxelID rmdMap1ID = rmdMAP1.get(x, y);
				const ArenaTypes::VoxelID rmdMap2ID = rmdMAP2.get(x, y);
				tempFlor.set(x, y, rmdFlorID);
				tempMap1.set(x, y, rmdMap1ID);
				tempMap2.set(x, y, rmdMap2ID);
			}
		}

		const bool isCityBlockID = (wildBlockID >= 1) && (wildBlockID <= 4);
		if (isCityBlockID)
		{
			// Change the placeholder WILD00{1..4}.RMD block to the one for the given city.
			BufferView2D<ArenaTypes::VoxelID> tempFlorView(
				tempFlor.get(), tempFlor.getWidth(), tempFlor.getHeight());
			BufferView2D<ArenaTypes::VoxelID> tempMap1View(
				tempMap1.get(), tempMap1.getWidth(), tempMap1.getHeight());
			BufferView2D<ArenaTypes::VoxelID> tempMap2View(
				tempMap2.get(), tempMap2.getWidth(), tempMap2.getHeight());

			// @todo: change this to take wild block ID instead of assuming it's the whole wilderness
			// and rename to reviseWildCityBlock() maybe.
			/*WildLevelUtils::reviseWildernessCity(locationDef, tempFlorView, tempMap1View,
				tempMap2View, binaryAssetLibrary);*/
			DebugNotImplemented();
		}

		LevelDefinition &levelDef = outLevelDefs.get(i);

		const BufferView2D<const ArenaTypes::VoxelID> tempFlorConstView(
			tempFlor.get(), tempFlor.getWidth(), tempFlor.getHeight());
		const BufferView2D<const ArenaTypes::VoxelID> tempMap1ConstView(
			tempMap1.get(), tempMap1.getWidth(), tempMap1.getHeight());
		const BufferView2D<const ArenaTypes::VoxelID> tempMap2ConstView(
			tempMap2.get(), tempMap2.getWidth(), tempMap2.getHeight());

		constexpr WorldType worldType = WorldType::Wilderness;
		constexpr std::optional<InteriorType> interiorType; // Wilderness is not an interior.
		constexpr std::optional<bool> rulerIsMale; // Not necessary for wild.

		MapGeneration::readArenaFLOR(tempFlorConstView, worldType, interiorType, rulerIsMale, inf,
			charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, &levelDef,
			outLevelInfoDef, &florMappings, &entityMappings);
		MapGeneration::readArenaMAP1(tempMap1ConstView, worldType, interiorType, rulerIsMale, inf,
			charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager, &levelDef,
			outLevelInfoDef, &map1Mappings, &entityMappings, &transitionMappings);
		MapGeneration::readArenaMAP2(tempMap2ConstView, inf, &levelDef, outLevelInfoDef, &map2Mappings);
	}

	// Generate chunk-wise building names for the wilderness.
	for (WEInt z = 0; z < levelDefIndices.getHeight(); z++)
	{
		for (SNInt x = 0; x < levelDefIndices.getWidth(); x++)
		{
			const int levelDefIndex = levelDefIndices.get(x, z);
			const LevelDefinition &levelDef = outLevelDefs.get(levelDefIndex);
			const ChunkInt2 chunk(x, z); // @todo: verify
			const uint32_t chunkSeed = ArenaWildUtils::makeWildChunkSeed(chunk.x, chunk.y);
			MapGeneration::WildChunkBuildingNameInfo buildingNameInfo;
			buildingNameInfo.init(chunk);

			MapGeneration::generateArenaWildChunkBuildingNames(chunkSeed, levelDef, binaryAssetLibrary,
				&buildingNameInfo, outLevelInfoDef, &buildingNameMappings);

			// Register the chunk if it has any buildings with names.
			if (buildingNameInfo.hasBuildingNames())
			{
				outBuildingNameInfos->emplace_back(std::move(buildingNameInfo));
			}
		}
	}
}

void MapGeneration::readMifLocks(const BufferView<const MIFFile::Level> &levels, const INFFile &inf,
	BufferView<LevelDefinition> &outLevelDefs, LevelInfoDefinition *outLevelInfoDef)
{
	ArenaLockMappingCache lockMappings;

	for (int i = 0; i < levels.getCount(); i++)
	{
		const MIFFile::Level &level = levels.get(i);
		LevelDefinition &levelDef = outLevelDefs.get(i);
		const BufferView<const ArenaTypes::MIFLock> locks = level.getLOCK();

		for (int j = 0; j < locks.getCount(); j++)
		{
			const ArenaTypes::MIFLock &lock = locks.get(j);
			MapGeneration::readArenaLock(lock, inf, &levelDef, outLevelInfoDef, &lockMappings);
		}
	}
}

void MapGeneration::readMifTriggers(const BufferView<const MIFFile::Level> &levels, const INFFile &inf,
	BufferView<LevelDefinition> &outLevelDefs, LevelInfoDefinition *outLevelInfoDef)
{
	ArenaTriggerMappingCache triggerMappings;

	for (int i = 0; i < levels.getCount(); i++)
	{
		const MIFFile::Level &level = levels.get(i);
		LevelDefinition &levelDef = outLevelDefs.get(i);
		const BufferView<const ArenaTypes::MIFTrigger> triggers = level.getTRIG();

		for (int j = 0; j < triggers.getCount(); j++)
		{
			const ArenaTypes::MIFTrigger &trigger = triggers.get(j);
			MapGeneration::readArenaTrigger(trigger, inf, &levelDef, outLevelInfoDef, &triggerMappings);
		}
	}
}
