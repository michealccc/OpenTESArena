#include <algorithm>
#include <functional>
#include <optional>

#include "ArenaLevelUtils.h"
#include "ExteriorWorldData.h"
#include "InteriorWorldData.h"
#include "InteriorUtils.h"
#include "LevelData.h"
#include "LocationUtils.h"
#include "ProvinceDefinition.h"
#include "VoxelDataType.h"
#include "VoxelDefinition.h"
#include "VoxelFacing2D.h"
#include "WorldData.h"
#include "WorldType.h"
#include "../Assets/ArenaAnimUtils.h"
#include "../Assets/ArenaTypes.h"
#include "../Assets/BinaryAssetLibrary.h"
#include "../Assets/CFAFile.h"
#include "../Assets/COLFile.h"
#include "../Assets/DFAFile.h"
#include "../Assets/ExeData.h"
#include "../Assets/IMGFile.h"
#include "../Assets/INFFile.h"
#include "../Assets/MIFUtils.h"
#include "../Assets/RCIFile.h"
#include "../Assets/SETFile.h"
#include "../Entities/CharacterClassLibrary.h"
#include "../Entities/CitizenManager.h"
#include "../Entities/EntityDefinitionLibrary.h"
#include "../Entities/EntityType.h"
#include "../Entities/StaticEntity.h"
#include "../Game/CardinalDirection.h"
#include "../Items/ArmorMaterialType.h"
#include "../Math/Constants.h"
#include "../Math/Random.h"
#include "../Media/PaletteFile.h"
#include "../Media/PaletteName.h"
#include "../Media/TextureManager.h"
#include "../Rendering/Renderer.h"

#include "components/debug/Debug.h"
#include "components/utilities/Bytes.h"
#include "components/utilities/String.h"
#include "components/utilities/StringView.h"

LevelData::FlatDef::FlatDef(ArenaTypes::FlatIndex flatIndex)
{
	this->flatIndex = flatIndex;
}

ArenaTypes::FlatIndex LevelData::FlatDef::getFlatIndex() const
{
	return this->flatIndex;
}

const std::vector<NewInt2> &LevelData::FlatDef::getPositions() const
{
	return this->positions;
}

void LevelData::FlatDef::addPosition(const NewInt2 &position)
{
	this->positions.push_back(position);
}

LevelData::Lock::Lock(const NewInt2 &position, int lockLevel)
	: position(position)
{
	this->lockLevel = lockLevel;
}

const NewInt2 &LevelData::Lock::getPosition() const
{
	return this->position;
}

int LevelData::Lock::getLockLevel() const
{
	return this->lockLevel;
}

LevelData::TextTrigger::TextTrigger(const std::string &text, bool displayedOnce)
	: text(text)
{
	this->displayedOnce = displayedOnce;
	this->previouslyDisplayed = false;
}

const std::string &LevelData::TextTrigger::getText() const
{
	return this->text;
}

bool LevelData::TextTrigger::isSingleDisplay() const
{
	return this->displayedOnce;
}

bool LevelData::TextTrigger::hasBeenDisplayed() const
{
	return this->previouslyDisplayed;
}

void LevelData::TextTrigger::setPreviouslyDisplayed(bool previouslyDisplayed)
{
	this->previouslyDisplayed = previouslyDisplayed;
}

LevelData::DoorState::DoorState(const NewInt2 &voxel, double percentOpen,
	DoorState::Direction direction)
	: voxel(voxel)
{
	this->percentOpen = percentOpen;
	this->direction = direction;
}

LevelData::DoorState::DoorState(const NewInt2 &voxel)
	: DoorState(voxel, 0.0, DoorState::Direction::Opening) { }

const NewInt2 &LevelData::DoorState::getVoxel() const
{
	return this->voxel;
}

double LevelData::DoorState::getPercentOpen() const
{
	return this->percentOpen;
}

bool LevelData::DoorState::isClosing() const
{
	return this->direction == Direction::Closing;
}

bool LevelData::DoorState::isClosed() const
{
	return this->percentOpen == 0.0;
}

void LevelData::DoorState::setDirection(DoorState::Direction direction)
{
	this->direction = direction;
}

void LevelData::DoorState::update(double dt)
{
	const double delta = DoorState::DEFAULT_SPEED * dt;

	// Decide how to change the door state depending on its current direction.
	if (this->direction == DoorState::Direction::Opening)
	{
		this->percentOpen = std::min(this->percentOpen + delta, 1.0);
		const bool isOpen = this->percentOpen == 1.0;

		if (isOpen)
		{
			this->direction = DoorState::Direction::None;
		}
	}
	else if (this->direction == DoorState::Direction::Closing)
	{
		this->percentOpen = std::max(this->percentOpen - delta, 0.0);

		if (this->isClosed())
		{
			this->direction = DoorState::Direction::None;
		}
	}
}

LevelData::FadeState::FadeState(const Int3 &voxel, double targetSeconds)
	: voxel(voxel)
{
	this->currentSeconds = 0.0;
	this->targetSeconds = targetSeconds;
}

LevelData::FadeState::FadeState(const Int3 &voxel)
	: FadeState(voxel, FadeState::DEFAULT_SECONDS) { }

const Int3 &LevelData::FadeState::getVoxel() const
{
	return this->voxel;
}

double LevelData::FadeState::getPercentDone() const
{
	return std::clamp(this->currentSeconds / this->targetSeconds, 0.0, 1.0);
}

bool LevelData::FadeState::isDoneFading() const
{
	return this->getPercentDone() == 1.0;
}

void LevelData::FadeState::update(double dt)
{
	this->currentSeconds = std::min(this->currentSeconds + dt, this->targetSeconds);
}

LevelData::ChasmState::ChasmState(const NewInt2 &voxel, bool north, bool east, bool south, bool west)
	: voxel(voxel)
{
	this->north = north;
	this->east = east;
	this->south = south;
	this->west = west;
}

const NewInt2 &LevelData::ChasmState::getVoxel() const
{
	return this->voxel;
}

bool LevelData::ChasmState::getNorth() const
{
	return this->north;
}

bool LevelData::ChasmState::getEast() const
{
	return this->east;
}

bool LevelData::ChasmState::getSouth() const
{
	return this->south;
}

bool LevelData::ChasmState::getWest() const
{
	return this->west;
}

bool LevelData::ChasmState::faceIsVisible(VoxelFacing2D facing) const
{
	switch (facing)
	{
	case VoxelFacing2D::PositiveX:
		return this->south;
	case VoxelFacing2D::PositiveZ:
		return this->west;
	case VoxelFacing2D::NegativeX:
		return this->north;
	case VoxelFacing2D::NegativeZ:
		return this->east;
	default:
		DebugNotImplementedMsg(std::to_string(static_cast<int>(facing)));
		return false;
	}
}

int LevelData::ChasmState::getFaceCount() const
{
	// Add one for floor.
	return 1 + (this->north ? 1 : 0) + (this->east ? 1 : 0) +
		(this->south ? 1 : 0) + (this->west ? 1 : 0);
}

LevelData::LevelData(SNInt gridWidth, int gridHeight, WEInt gridDepth, const std::string &infName,
	const std::string &name)
	: voxelGrid(gridWidth, gridHeight, gridDepth), name(name)
{
	const int chunkCountX = (gridWidth + (RMDFile::WIDTH - 1)) / RMDFile::WIDTH;
	const int chunkCountY = (gridDepth + (RMDFile::DEPTH - 1)) / RMDFile::DEPTH;
	this->entityManager.init(chunkCountX, chunkCountY);

	if (!this->inf.init(infName.c_str()))
	{
		DebugCrash("Could not init .INF file \"" + infName + "\".");
	}
}

LevelData::~LevelData()
{

}

const std::string &LevelData::getName() const
{
	return this->name;
}

double LevelData::getCeilingHeight() const
{
	return static_cast<double>(this->inf.getCeiling().height) / MIFUtils::ARENA_UNITS;
}

std::vector<LevelData::FlatDef> &LevelData::getFlats()
{
	return this->flatsLists;
}

const std::vector<LevelData::FlatDef> &LevelData::getFlats() const
{
	return this->flatsLists;
}

std::vector<LevelData::DoorState> &LevelData::getOpenDoors()
{
	return this->openDoors;
}

const std::vector<LevelData::DoorState> &LevelData::getOpenDoors() const
{
	return this->openDoors;
}

std::vector<LevelData::FadeState> &LevelData::getFadingVoxels()
{
	return this->fadingVoxels;
}

const std::vector<LevelData::FadeState> &LevelData::getFadingVoxels() const
{
	return this->fadingVoxels;
}

LevelData::ChasmStates &LevelData::getChasmStates()
{
	return this->chasmStates;
}

const LevelData::ChasmStates &LevelData::getChasmStates() const
{
	return this->chasmStates;
}

const INFFile &LevelData::getInfFile() const
{
	return this->inf;
}

EntityManager &LevelData::getEntityManager()
{
	return this->entityManager;
}

const EntityManager &LevelData::getEntityManager() const
{
	return this->entityManager;
}

VoxelGrid &LevelData::getVoxelGrid()
{
	return this->voxelGrid;
}

const VoxelGrid &LevelData::getVoxelGrid() const
{
	return this->voxelGrid;
}

const LevelData::Lock *LevelData::getLock(const NewInt2 &voxel) const
{
	const auto lockIter = this->locks.find(voxel);
	return (lockIter != this->locks.end()) ? &lockIter->second : nullptr;
}

void LevelData::addFlatInstance(ArenaTypes::FlatIndex flatIndex, const NewInt2 &flatPosition)
{
	// Add position to instance list if the flat def has already been created.
	const auto iter = std::find_if(this->flatsLists.begin(), this->flatsLists.end(),
		[flatIndex](const FlatDef &flatDef)
	{
		return flatDef.getFlatIndex() == flatIndex;
	});

	if (iter != this->flatsLists.end())
	{
		iter->addPosition(flatPosition);
	}
	else
	{
		// Create new def.
		FlatDef flatDef(flatIndex);
		flatDef.addPosition(flatPosition);
		this->flatsLists.push_back(std::move(flatDef));
	}
}

void LevelData::setVoxel(SNInt x, int y, WEInt z, uint16_t id)
{
	this->voxelGrid.setVoxel(x, y, z, id);
}

void LevelData::readFLOR(const BufferView2D<const ArenaTypes::VoxelID> &flor, const INFFile &inf)
{
	const SNInt gridWidth = flor.getHeight();
	const WEInt gridDepth = flor.getWidth();

	// Lambda for obtaining a two-byte FLOR voxel.
	auto getFlorVoxel = [&flor, gridWidth, gridDepth](SNInt x, WEInt z)
	{
		const uint16_t voxel = flor.get(z, x);
		return voxel;
	};

	// Lambda for obtaining the voxel data index of a typical (non-chasm) FLOR voxel.
	auto getFlorDataIndex = [this](uint16_t florVoxel, int floorTextureID)
	{
		// See if the voxel already has a mapping.
		const auto floorIter = std::find_if(
			this->floorDataMappings.begin(), this->floorDataMappings.end(),
			[florVoxel](const std::pair<uint16_t, int> &pair)
		{
			return pair.first == florVoxel;
		});

		if (floorIter != this->floorDataMappings.end())
		{
			return floorIter->second;
		}
		else
		{
			// Insert new mapping.
			const int index = this->voxelGrid.addVoxelDef(VoxelDefinition::makeFloor(floorTextureID));
			this->floorDataMappings.push_back(std::make_pair(florVoxel, index));
			return index;
		}
	};

	using ChasmDataFunc = VoxelDefinition(*)(const INFFile &inf);

	// Lambda for obtaining the voxel data index of a chasm voxel. The given function argument
	// returns the created voxel data if there was no previous mapping.
	auto getChasmDataIndex = [this, &inf](uint16_t florVoxel, ChasmDataFunc chasmFunc)
	{
		const auto floorIter = std::find_if(
			this->floorDataMappings.begin(), this->floorDataMappings.end(),
			[florVoxel](const std::pair<uint16_t, int> &pair)
		{
			return pair.first == florVoxel;
		});

		if (floorIter != this->floorDataMappings.end())
		{
			return floorIter->second;
		}
		else
		{
			// Insert new mapping.
			const int index = this->voxelGrid.addVoxelDef(chasmFunc(inf));
			this->floorDataMappings.push_back(std::make_pair(florVoxel, index));
			return index;
		}
	};

	// Helper lambdas for creating each type of chasm voxel data.
	auto makeDryChasmVoxelDef = [](const INFFile &inf)
	{
		const int dryChasmID = [&inf]()
		{
			const std::optional<int> &index = inf.getDryChasmIndex();
			if (index.has_value())
			{
				return *index;
			}
			else
			{
				DebugLogWarning("Missing *DRYCHASM ID.");
				return 0;
			}
		}();

		return VoxelDefinition::makeChasm(dryChasmID, VoxelDefinition::ChasmData::Type::Dry);
	};

	auto makeLavaChasmVoxelDef = [](const INFFile &inf)
	{
		const int lavaChasmID = [&inf]()
		{
			const std::optional<int> &index = inf.getLavaChasmIndex();
			if (index.has_value())
			{
				return *index;
			}
			else
			{
				DebugLogWarning("Missing *LAVACHASM ID.");
				return 0;
			}
		}();

		return VoxelDefinition::makeChasm(lavaChasmID, VoxelDefinition::ChasmData::Type::Lava);
	};

	auto makeWetChasmVoxelDef = [](const INFFile &inf)
	{
		const int wetChasmID = [&inf]()
		{
			const std::optional<int> &index = inf.getWetChasmIndex();
			if (index.has_value())
			{
				return *index;
			}
			else
			{
				DebugLogWarning("Missing *WETCHASM ID.");
				return 0;
			}
		}();

		return VoxelDefinition::makeChasm(wetChasmID, VoxelDefinition::ChasmData::Type::Wet);
	};

	// Write the voxel IDs into the voxel grid.
	for (SNInt x = 0; x < gridWidth; x++)
	{
		for (WEInt z = 0; z < gridDepth; z++)
		{
			auto getFloorTextureID = [](uint16_t voxel)
			{
				return (voxel & 0xFF00) >> 8;
			};

			auto getFloorFlatID = [](uint16_t voxel)
			{
				return voxel & 0x00FF;
			};

			const uint16_t florVoxel = getFlorVoxel(x, z);
			const int floorTextureID = getFloorTextureID(florVoxel);

			// See if the floor voxel is either solid or a chasm.
			if (!MIFUtils::isChasm(floorTextureID))
			{
				// Get the voxel data index associated with the floor value, or add it
				// if it doesn't exist yet.
				const int dataIndex = getFlorDataIndex(florVoxel, floorTextureID);
				this->setVoxel(x, 0, z, dataIndex);
			}
			else
			{
				// Chasm of some type.
				ChasmDataFunc chasmDataFunc;
				if (floorTextureID == MIFUtils::DRY_CHASM)
				{
					chasmDataFunc = makeDryChasmVoxelDef;
				}
				else if (floorTextureID == MIFUtils::LAVA_CHASM)
				{
					chasmDataFunc = makeLavaChasmVoxelDef;
				}
				else if (floorTextureID == MIFUtils::WET_CHASM)
				{
					chasmDataFunc = makeWetChasmVoxelDef;
				}
				else
				{
					DebugNotImplementedMsg(std::to_string(floorTextureID));
				}

				const int dataIndex = getChasmDataIndex(florVoxel, chasmDataFunc);
				this->setVoxel(x, 0, z, dataIndex);
			}

			// See if the FLOR voxel contains a FLAT index (for raised platform flats).
			const int floorFlatID = getFloorFlatID(florVoxel);
			if (floorFlatID > 0)
			{
				const ArenaTypes::FlatIndex flatIndex = floorFlatID - 1;
				this->addFlatInstance(flatIndex, NewInt2(x, z));
			}
		}
	}

	// Set chasm faces based on adjacent voxels.
	for (SNInt x = 0; x < gridWidth; x++)
	{
		for (WEInt z = 0; z < gridDepth; z++)
		{
			const Int3 voxel(x, 0, z);

			// Ignore non-chasm voxels.
			const uint16_t voxelID = this->voxelGrid.getVoxel(voxel.x, voxel.y, voxel.z);
			const VoxelDefinition &voxelDef = this->voxelGrid.getVoxelDef(voxelID);
			if (voxelDef.dataType != VoxelDataType::Chasm)
			{
				continue;
			}

			// Query surrounding voxels to see which faces should be set.
			uint16_t northID, southID, eastID, westID;
			this->getAdjacentVoxelIDs(voxel, &northID, &southID, &eastID, &westID);

			const VoxelDefinition &northDef = this->voxelGrid.getVoxelDef(northID);
			const VoxelDefinition &southDef = this->voxelGrid.getVoxelDef(southID);
			const VoxelDefinition &eastDef = this->voxelGrid.getVoxelDef(eastID);
			const VoxelDefinition &westDef = this->voxelGrid.getVoxelDef(westID);

			// Booleans for each face of the new chasm voxel.
			const bool hasNorthFace = northDef.allowsChasmFace();
			const bool hasSouthFace = southDef.allowsChasmFace();
			const bool hasEastFace = eastDef.allowsChasmFace();
			const bool hasWestFace = westDef.allowsChasmFace();

			// Add chasm state if it is different from the default 0 faces chasm (don't need to
			// do update on existing chasms here because there should be no existing ones).
			const bool shouldAddChasmState = hasNorthFace || hasEastFace || hasSouthFace || hasWestFace;
			if (shouldAddChasmState)
			{
				const NewInt2 voxelXZ(x, z);
				ChasmState newChasmState(voxelXZ, hasNorthFace, hasEastFace, hasSouthFace, hasWestFace);
				this->chasmStates.emplace(voxelXZ, std::move(newChasmState));
			}
		}
	}
}

void LevelData::readMAP1(const BufferView2D<const ArenaTypes::VoxelID> &map1, const INFFile &inf,
	WorldType worldType, const ExeData &exeData)
{
	const SNInt gridWidth = map1.getHeight();
	const WEInt gridDepth = map1.getWidth();

	// Lambda for obtaining a two-byte MAP1 voxel.
	auto getMap1Voxel = [&map1, gridWidth, gridDepth](SNInt x, WEInt z)
	{
		const uint16_t voxel = map1.get(z, x);
		return voxel;
	};

	// Lambda for finding if there's an existing mapping for a MAP1 voxel.
	auto findWallMapping = [this](uint16_t map1Voxel)
	{
		return std::find_if(this->wallDataMappings.begin(), this->wallDataMappings.end(),
			[map1Voxel](const std::pair<uint16_t, int> &pair)
		{
			return pair.first == map1Voxel;
		});
	};

	// Lambda for obtaining the voxel data index of a general-case MAP1 object. The function
	// parameter returns the created voxel data if no previous mapping exists, and is intended
	// for general cases where the voxel data type does not need extra parameters.
	auto getDataIndex = [this, &findWallMapping](uint16_t map1Voxel, VoxelDefinition(*func)(uint16_t))
	{
		const auto wallIter = findWallMapping(map1Voxel);
		if (wallIter != this->wallDataMappings.end())
		{
			return wallIter->second;
		}
		else
		{
			const int index = this->voxelGrid.addVoxelDef(func(map1Voxel));
			this->wallDataMappings.push_back(std::make_pair(map1Voxel, index));
			return index;
		}
	};

	// Lambda for obtaining the voxel data index of a solid wall.
	auto getWallDataIndex = [this, &inf, &findWallMapping](uint16_t map1Voxel, uint8_t mostSigByte)
	{
		const auto wallIter = findWallMapping(map1Voxel);
		if (wallIter != this->wallDataMappings.end())
		{
			return wallIter->second;
		}
		else
		{
			// Lambda for creating a basic solid wall voxel data.
			auto makeWallVoxelData = [map1Voxel, &inf, mostSigByte]()
			{
				const int textureIndex = mostSigByte - 1;

				// Menu index if the voxel has the *MENU tag, or empty if it is not a *MENU voxel.
				const std::optional<int> &menuIndex = inf.getMenuIndex(textureIndex);
				const bool isMenu = menuIndex.has_value();

				// Determine what the type of the wall is (level up/down, menu, 
				// or just plain solid).
				const VoxelDefinition::WallData::Type type = [&inf, textureIndex, isMenu]()
				{
					// Returns whether the given index pointer is non-null and matches the
					// current texture index.
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

				VoxelDefinition voxelDef = VoxelDefinition::makeWall(
					textureIndex, textureIndex, textureIndex, menuIndex, type);

				// Set the *MENU index if it's a menu voxel.
				if (isMenu)
				{
					VoxelDefinition::WallData &wallData = voxelDef.wall;
					wallData.menuID = *menuIndex;
				}

				return voxelDef;
			};

			const int index = this->voxelGrid.addVoxelDef(makeWallVoxelData());
			this->wallDataMappings.push_back(std::make_pair(map1Voxel, index));
			return index;
		}
	};

	// Lambda for obtaining the voxel data index of a raised platform.
	auto getRaisedDataIndex = [this, &inf, worldType, &exeData, &findWallMapping](
		uint16_t map1Voxel, uint8_t mostSigByte, SNInt x, WEInt z)
	{
		const auto wallIter = findWallMapping(map1Voxel);
		if (wallIter != this->wallDataMappings.end())
		{
			return wallIter->second;
		}
		else
		{
			// Lambda for creating a raised voxel data.
			auto makeRaisedVoxelData = [worldType, &exeData, map1Voxel, &inf, mostSigByte, x, z]()
			{
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
						DebugLogWarning("Missing *BOXSIDE ID \"" +
							std::to_string(wallTextureID) + "\".");
						return 0;
					}
				}();

				const int floorID = [&inf, x, z]()
				{
					const auto &id = inf.getCeiling().textureIndex;

					if (id.has_value())
					{
						return id.value();
					}
					else
					{
						DebugLogWarning("Missing platform floor ID (" +
							std::to_string(x) + ", " + std::to_string(z) + ").");
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
						DebugLogWarning("Missing *BOXCAP ID \"" +
							std::to_string(capTextureID) + "\".");
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

					const int boxSize = 32;
					const auto &boxScale = inf.getCeiling().boxScale;
					baseSize = (boxSize *
						(boxScale.has_value() ? boxScale.value() : 192)) / 256;
				}
				else
				{
					throw DebugException("Invalid world type \"" +
						std::to_string(static_cast<int>(worldType)) + "\".");
				}

				const double yOffset = static_cast<double>(baseOffset) / MIFUtils::ARENA_UNITS;
				const double ySize = static_cast<double>(baseSize) / MIFUtils::ARENA_UNITS;
				const double normalizedScale = static_cast<double>(inf.getCeiling().height) / MIFUtils::ARENA_UNITS;
				const double yOffsetNormalized = yOffset / normalizedScale;
				const double ySizeNormalized = ySize / normalizedScale;

				// @todo: might need some tweaking with box3/box4 values.
				const double vTop = std::max(
					0.0, 1.0 - yOffsetNormalized - ySizeNormalized);
				const double vBottom = std::min(vTop + ySizeNormalized, 1.0);

				return VoxelDefinition::makeRaised(sideID, floorID, ceilingID,
					yOffsetNormalized, ySizeNormalized, vTop, vBottom);
			};

			const int index = this->voxelGrid.addVoxelDef(makeRaisedVoxelData());
			this->wallDataMappings.push_back(std::make_pair(map1Voxel, index));
			return index;
		}
	};

	// Lambda for creating type 0x9 voxel data.
	auto makeType9VoxelData = [](uint16_t map1Voxel)
	{
		const int textureIndex = (map1Voxel & 0x00FF) - 1;
		const bool collider = (map1Voxel & 0x0100) == 0;
		return VoxelDefinition::makeTransparentWall(textureIndex, collider);
	};

	// Lambda for obtaining the voxel data index of a type 0xA voxel.
	auto getTypeADataIndex = [this, worldType, &findWallMapping](
		uint16_t map1Voxel, int textureIndex)
	{
		const auto wallIter = findWallMapping(map1Voxel);
		if (wallIter != this->wallDataMappings.end())
		{
			return wallIter->second;
		}
		else
		{
			// Lambda for creating type 0xA voxel data.
			auto makeTypeAVoxelData = [worldType, map1Voxel, textureIndex]()
			{
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

				return VoxelDefinition::makeEdge(
					textureIndex, yOffset, collider, flipped, facing);
			};

			const int index = this->voxelGrid.addVoxelDef(makeTypeAVoxelData());
			this->wallDataMappings.push_back(std::make_pair(map1Voxel, index));
			return index;
		}
	};

	// Lambda for creating type 0xB voxel data.
	auto makeTypeBVoxelData = [](uint16_t map1Voxel)
	{
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
				// I don't believe any doors in Arena split (but they are
				// supported by the engine).
				DebugUnhandledReturnMsg(
					VoxelDefinition::DoorData::Type, std::to_string(type));
			}
		}();

		return VoxelDefinition::makeDoor(textureIndex, doorType);
	};

	// Lambda for creating type 0xD voxel data.
	auto makeTypeDVoxelData = [](uint16_t map1Voxel)
	{
		const int textureIndex = (map1Voxel & 0x00FF) - 1;
		const bool isRightDiag = (map1Voxel & 0x0100) == 0;
		return VoxelDefinition::makeDiagonal(textureIndex, isRightDiag);
	};

	// Write the voxel IDs into the voxel grid.
	for (SNInt x = 0; x < gridWidth; x++)
	{
		for (WEInt z = 0; z < gridDepth; z++)
		{
			const uint16_t map1Voxel = getMap1Voxel(x, z);

			if ((map1Voxel & 0x8000) == 0)
			{
				// A voxel of some kind.
				const bool voxelIsEmpty = map1Voxel == 0;

				if (!voxelIsEmpty)
				{
					const uint8_t mostSigByte = (map1Voxel & 0x7F00) >> 8;
					const uint8_t leastSigByte = map1Voxel & 0x007F;
					const bool voxelIsSolid = mostSigByte == leastSigByte;

					if (voxelIsSolid)
					{
						// Regular solid wall.
						const int dataIndex = getWallDataIndex(map1Voxel, mostSigByte);
						this->setVoxel(x, 1, z, dataIndex);
					}
					else
					{
						// Raised platform.
						const int dataIndex = getRaisedDataIndex(map1Voxel, mostSigByte, x, z);
						this->setVoxel(x, 1, z, dataIndex);
					}
				}
			}
			else
			{
				// A special voxel, or an object of some kind.
				const uint8_t mostSigNibble = (map1Voxel & 0xF000) >> 12;

				if (mostSigNibble == 0x8)
				{
					// The lower byte determines the index of a FLAT for an object.
					const ArenaTypes::FlatIndex flatIndex = map1Voxel & 0x00FF;
					this->addFlatInstance(flatIndex, NewInt2(x, z));
				}
				else if (mostSigNibble == 0x9)
				{
					// Transparent block with 1-sided texture on all sides, such as wooden 
					// arches in dungeons. These do not have back-faces (especially when 
					// standing in the voxel itself).
					const int dataIndex = getDataIndex(map1Voxel, makeType9VoxelData);
					this->setVoxel(x, 1, z, dataIndex);
				}
				else if (mostSigNibble == 0xA)
				{
					// Transparent block with 2-sided texture on one side (i.e., fence).
					const int textureIndex = (map1Voxel & 0x003F) - 1;

					// It is clamped non-negative due to a case in the center province's city
					// where one temple voxel has all zeroes for its texture index, and it
					// appears solid gray in the original game (presumably a silent bug).
					if (textureIndex >= 0)
					{
						const int dataIndex = getTypeADataIndex(map1Voxel, textureIndex);
						this->setVoxel(x, 1, z, dataIndex);
					}
				}
				else if (mostSigNibble == 0xB)
				{
					// Door voxel.
					const int dataIndex = getDataIndex(map1Voxel, makeTypeBVoxelData);
					this->setVoxel(x, 1, z, dataIndex);
				}
				else if (mostSigNibble == 0xC)
				{
					// Unknown.
					DebugLogWarning("Voxel type 0xC not implemented.");
				}
				else if (mostSigNibble == 0xD)
				{
					// Diagonal wall. Its type is determined by the nineth bit.
					const int dataIndex = getDataIndex(map1Voxel, makeTypeDVoxelData);
					this->setVoxel(x, 1, z, dataIndex);
				}
			}
		}
	}
}

void LevelData::readMAP2(const BufferView2D<const ArenaTypes::VoxelID> &map2, const INFFile &inf)
{
	const SNInt gridWidth = map2.getHeight();
	const WEInt gridDepth = map2.getWidth();

	// Lambda for obtaining a two-byte MAP2 voxel.
	auto getMap2Voxel = [&map2, gridWidth, gridDepth](SNInt x, WEInt z)
	{
		const uint16_t voxel = map2.get(z, x);
		return voxel;
	};

	// Lambda for obtaining the voxel data index for a MAP2 voxel.
	auto getMap2DataIndex = [this, &inf](uint16_t map2Voxel)
	{
		const auto map2Iter = std::find_if(
			this->map2DataMappings.begin(), this->map2DataMappings.end(),
			[map2Voxel](const std::pair<uint16_t, int> &pair)
		{
			return pair.first == map2Voxel;
		});

		if (map2Iter != this->map2DataMappings.end())
		{
			return map2Iter->second;
		}
		else
		{
			const int textureIndex = (map2Voxel & 0x007F) - 1;
			constexpr std::optional<int> menuID; // MAP2 cannot have a *MENU ID.
			const int index = this->voxelGrid.addVoxelDef(VoxelDefinition::makeWall(
				textureIndex, textureIndex, textureIndex, menuID,
				VoxelDefinition::WallData::Type::Solid));
			this->map2DataMappings.push_back(std::make_pair(map2Voxel, index));
			return index;
		}
	};

	// Write the voxel IDs into the voxel grid.
	for (SNInt x = 0; x < gridWidth; x++)
	{
		for (WEInt z = 0; z < gridDepth; z++)
		{
			const uint16_t map2Voxel = getMap2Voxel(x, z);

			if (map2Voxel != 0)
			{
				const int height = ArenaLevelUtils::getMap2VoxelHeight(map2Voxel);
				const int dataIndex = getMap2DataIndex(map2Voxel);

				for (int y = 2; y < (height + 2); y++)
				{
					this->setVoxel(x, y, z, dataIndex);
				}
			}
		}
	}
}

void LevelData::readCeiling(const INFFile &inf)
{
	const INFFile::CeilingData &ceiling = inf.getCeiling();

	// Get the index of the ceiling texture name in the textures array.
	const int ceilingIndex = [&ceiling]()
	{
		// @todo: get ceiling from .INFs without *CEILING (like START.INF). Maybe
		// hardcoding index 1 is enough?
		return ceiling.textureIndex.value_or(1);
	}();

	// Define the ceiling voxel data.
	const int index = this->voxelGrid.addVoxelDef(VoxelDefinition::makeCeiling(ceilingIndex));

	// Set all the ceiling voxels.
	const SNInt gridWidth = this->voxelGrid.getWidth();
	const WEInt gridDepth = this->voxelGrid.getDepth();
	for (SNInt x = 0; x < gridWidth; x++)
	{
		for (WEInt z = 0; z < gridDepth; z++)
		{
			this->setVoxel(x, 2, z, index);
		}
	}
}

void LevelData::readLocks(const BufferView<const ArenaTypes::MIFLock> &locks)
{
	for (int i = 0; i < locks.getCount(); i++)
	{
		const auto &lock = locks.get(i);
		const NewInt2 lockPosition = VoxelUtils::originalVoxelToNewVoxel(OriginalInt2(lock.x, lock.y));
		this->locks.insert(std::make_pair(lockPosition, LevelData::Lock(lockPosition, lock.lockLevel)));
	}
}

void LevelData::getAdjacentVoxelIDs(const Int3 &voxel, uint16_t *outNorthID, uint16_t *outSouthID,
	uint16_t *outEastID, uint16_t *outWestID) const
{
	auto getVoxelIdOrAir = [this](const Int3 &voxel)
	{
		// The voxel is air if outside the grid.
		return this->voxelGrid.coordIsValid(voxel.x, voxel.y, voxel.z) ?
			this->voxelGrid.getVoxel(voxel.x, voxel.y, voxel.z) : 0;
	};

	const Int3 northVoxel(voxel.x - 1, voxel.y, voxel.z);
	const Int3 southVoxel(voxel.x + 1, voxel.y, voxel.z);
	const Int3 eastVoxel(voxel.x, voxel.y, voxel.z - 1);
	const Int3 westVoxel(voxel.x, voxel.y, voxel.z + 1);

	if (outNorthID != nullptr)
	{
		*outNorthID = getVoxelIdOrAir(northVoxel);
	}

	if (outSouthID != nullptr)
	{
		*outSouthID = getVoxelIdOrAir(southVoxel);
	}

	if (outEastID != nullptr)
	{
		*outEastID = getVoxelIdOrAir(eastVoxel);
	}

	if (outWestID != nullptr)
	{
		*outWestID = getVoxelIdOrAir(westVoxel);
	}
}

void LevelData::tryUpdateChasmVoxel(const Int3 &voxel)
{
	// Ignore if outside the grid.
	if (!this->voxelGrid.coordIsValid(voxel.x, voxel.y, voxel.z))
	{
		return;
	}

	const uint16_t voxelID = this->voxelGrid.getVoxel(voxel.x, voxel.y, voxel.z);
	const VoxelDefinition &voxelDef = this->voxelGrid.getVoxelDef(voxelID);

	// Ignore if not a chasm (no faces to update).
	if (voxelDef.dataType != VoxelDataType::Chasm)
	{
		return;
	}

	// Query surrounding voxels to see which faces should be set.
	uint16_t northID, southID, eastID, westID;
	this->getAdjacentVoxelIDs(voxel, &northID, &southID, &eastID, &westID);

	const VoxelDefinition &northDef = this->voxelGrid.getVoxelDef(northID);
	const VoxelDefinition &southDef = this->voxelGrid.getVoxelDef(southID);
	const VoxelDefinition &eastDef = this->voxelGrid.getVoxelDef(eastID);
	const VoxelDefinition &westDef = this->voxelGrid.getVoxelDef(westID);

	// Booleans for each face of the new chasm voxel.
	const bool hasNorthFace = northDef.allowsChasmFace();
	const bool hasSouthFace = southDef.allowsChasmFace();
	const bool hasEastFace = eastDef.allowsChasmFace();
	const bool hasWestFace = westDef.allowsChasmFace();

	// Add/update chasm state.
	const NewInt2 voxelXZ(voxel.x, voxel.z);
	ChasmState newChasmState(voxelXZ, hasNorthFace, hasEastFace, hasSouthFace, hasWestFace);
	const auto chasmStateIter = this->chasmStates.find(newChasmState.getVoxel());
	const bool shouldAddChasmState = hasNorthFace || hasEastFace || hasSouthFace || hasWestFace;
	if (chasmStateIter != this->chasmStates.end())
	{
		if (shouldAddChasmState)
		{
			chasmStateIter->second = std::move(newChasmState);
		}
		else
		{
			this->chasmStates.erase(chasmStateIter);
		}
	}
	else
	{
		if (shouldAddChasmState)
		{
			this->chasmStates.emplace(voxelXZ, std::move(newChasmState));
		}
	}
}

uint16_t LevelData::getChasmIdFromFadedFloorVoxel(const Int3 &voxel)
{
	DebugAssert(this->voxelGrid.coordIsValid(voxel.x, voxel.y, voxel.z));

	// Get voxel IDs of adjacent voxels (potentially air).
	uint16_t northID, southID, eastID, westID;
	this->getAdjacentVoxelIDs(voxel, &northID, &southID, &eastID, &westID);

	const VoxelDefinition &northDef = this->voxelGrid.getVoxelDef(northID);
	const VoxelDefinition &southDef = this->voxelGrid.getVoxelDef(southID);
	const VoxelDefinition &eastDef = this->voxelGrid.getVoxelDef(eastID);
	const VoxelDefinition &westDef = this->voxelGrid.getVoxelDef(westID);

	// Booleans for each face of the new chasm voxel.
	const bool hasNorthFace = northDef.allowsChasmFace();
	const bool hasSouthFace = southDef.allowsChasmFace();
	const bool hasEastFace = eastDef.allowsChasmFace();
	const bool hasWestFace = westDef.allowsChasmFace();

	// Based on how the original game behaves, it seems to be the chasm type closest to the player,
	// even dry chasms, that determines what the destroyed floor becomes. This allows for oddities
	// like creating a dry chasm next to lava, which results in continued oddities like having a
	// big difference in chasm depth between the two (depending on ceiling height).
	const VoxelDefinition::ChasmData::Type newChasmType = []()
	{
		// @todo: include player position. If there are no chasms to pick from, then default to
		// wet chasm.
		// @todo: getNearestChasmType(const Int3 &voxel)
		return VoxelDefinition::ChasmData::Type::Wet;
	}();

	const int newTextureID = [this, newChasmType]()
	{
		std::optional<int> chasmIndex;

		switch (newChasmType)
		{
		case VoxelDefinition::ChasmData::Type::Dry:
			chasmIndex = this->inf.getDryChasmIndex();
			break;
		case VoxelDefinition::ChasmData::Type::Wet:
			chasmIndex = this->inf.getWetChasmIndex();
			break;
		case VoxelDefinition::ChasmData::Type::Lava:
			chasmIndex = this->inf.getLavaChasmIndex();
			break;
		default:
			DebugNotImplementedMsg(std::to_string(static_cast<int>(newChasmType)));
			break;
		}

		// Default to the first texture if one is not found.
		return chasmIndex.has_value() ? *chasmIndex : 0;
	}();

	const VoxelDefinition newDef = VoxelDefinition::makeChasm(newTextureID, newChasmType);

	// Find matching chasm voxel definition, adding if missing.
	const std::optional<uint16_t> optChasmID = this->voxelGrid.findVoxelDef(
		[&newDef](const VoxelDefinition &voxelDef)
	{
		if (voxelDef.dataType == VoxelDataType::Chasm)
		{
			DebugAssert(newDef.dataType == VoxelDataType::Chasm);
			const VoxelDefinition::ChasmData &newChasmData = newDef.chasm;
			const VoxelDefinition::ChasmData &chasmData = voxelDef.chasm;
			return chasmData.matches(newChasmData);
		}
		else
		{
			return false;
		}
	});

	// Add/update chasm state.
	const NewInt2 voxelXZ(voxel.x, voxel.z);
	ChasmState newChasmState(voxelXZ, hasNorthFace, hasEastFace, hasSouthFace, hasWestFace);
	const auto chasmStateIter = this->chasmStates.find(newChasmState.getVoxel());
	const bool shouldAddChasmState = hasNorthFace || hasEastFace || hasSouthFace || hasWestFace;
	if (chasmStateIter != this->chasmStates.end())
	{
		if (shouldAddChasmState)
		{
			chasmStateIter->second = std::move(newChasmState);
		}
		else
		{
			this->chasmStates.erase(chasmStateIter);
		}
	}
	else
	{
		if (shouldAddChasmState)
		{
			this->chasmStates.emplace(voxelXZ, std::move(newChasmState));
		}
	}

	if (optChasmID.has_value())
	{
		return *optChasmID;
	}
	else
	{
		// Need to add a new voxel data to the voxel grid.
		return this->voxelGrid.addVoxelDef(newDef);
	}
}

void LevelData::updateFadingVoxels(double dt)
{
	std::vector<Int3> completedVoxels;

	// Reverse iterate, removing voxels that are done fading out.
	for (int i = static_cast<int>(this->fadingVoxels.size()) - 1; i >= 0; i--)
	{
		FadeState &fadingVoxel = this->fadingVoxels[i];
		const Int3 &voxel = fadingVoxel.getVoxel();
		fadingVoxel.update(dt);

		if (fadingVoxel.isDoneFading())
		{
			completedVoxels.push_back(voxel);

			const bool isFloorVoxel = voxel.y == 0;
			const uint16_t newVoxelID = [this, &voxel, isFloorVoxel]() -> uint16_t
			{
				if (isFloorVoxel)
				{
					// Convert from floor to chasm.
					return this->getChasmIdFromFadedFloorVoxel(voxel);
				}
				else
				{
					// Clear the voxel.
					return 0;
				}
			}();

			// Change the voxel in the grid to its empty representation (either air or chasm) and
			// erase the fading voxel from the list.
			voxelGrid.setVoxel(voxel.x, voxel.y, voxel.z, newVoxelID);
			this->fadingVoxels.erase(this->fadingVoxels.begin() + i);
		}
	}

	// Update adjacent chasm faces (not sure why this has to be done after, but it works).
	for (const Int3 &voxel : completedVoxels)
	{
		const bool isFloorVoxel = voxel.y == 0;

		if (isFloorVoxel)
		{
			const Int3 northVoxel(voxel.x - 1, voxel.y, voxel.z);
			const Int3 southVoxel(voxel.x + 1, voxel.y, voxel.z);
			const Int3 eastVoxel(voxel.x, voxel.y, voxel.z - 1);
			const Int3 westVoxel(voxel.x, voxel.y, voxel.z + 1);
			this->tryUpdateChasmVoxel(northVoxel);
			this->tryUpdateChasmVoxel(southVoxel);
			this->tryUpdateChasmVoxel(eastVoxel);
			this->tryUpdateChasmVoxel(westVoxel);
		}
	}
}

void LevelData::setActive(bool nightLightsAreActive, const WorldData &worldData,
	const ProvinceDefinition &provinceDef, const LocationDefinition &locationDef,
	const EntityDefinitionLibrary &entityDefLibrary, const CharacterClassLibrary &charClassLibrary,
	const BinaryAssetLibrary &binaryAssetLibrary, Random &random, CitizenManager &citizenManager,
	TextureManager &textureManager, TextureInstanceManager &textureInstManager, Renderer &renderer)
{
	// Clear renderer textures, distant sky, and entities.
	renderer.clearTexturesAndEntityRenderIDs();
	renderer.clearDistantSky();
	this->entityManager.clear();

	// Palette for voxels and flats, required in the renderer so it can conditionally transform
	// certain palette indices for transparency.
	COLFile col;
	col.init(PaletteFile::fromName(PaletteName::Default).c_str());
	const Palette &palette = col.getPalette();

	// Loads .INF voxel textures into the renderer.
	auto loadVoxelTextures = [this, &textureManager, &renderer, &palette]()
	{
		const auto &voxelTextures = this->inf.getVoxelTextures();
		const int voxelTextureCount = static_cast<int>(voxelTextures.size());
		for (int i = 0; i < voxelTextureCount; i++)
		{
			DebugAssertIndex(voxelTextures, i);
			const auto &textureData = voxelTextures[i];

			const std::string textureName = String::toUppercase(textureData.filename.data());
			const std::string_view extension = StringView::getExtension(textureName);
			const bool isIMG = extension == "IMG";
			const bool isSET = extension == "SET";
			const bool noExtension = extension.size() == 0;

			if (isIMG)
			{
				IMGFile img;
				if (!img.init(textureName.c_str()))
				{
					DebugCrash("Couldn't init .IMG file \"" + textureName + "\".");
				}

				renderer.setVoxelTexture(i, img.getPixels(), palette);
			}
			else if (isSET)
			{
				SETFile set;
				if (!set.init(textureName.c_str()))
				{
					DebugCrash("Couldn't init .SET file \"" + textureName + "\".");
				}

				// Use the texture data's .SET index to obtain the correct surface.
				DebugAssert(textureData.setIndex.has_value());
				const uint8_t *srcPixels = set.getPixels(*textureData.setIndex);
				renderer.setVoxelTexture(i, srcPixels, palette);
			}
			else if (noExtension)
			{
				// Ignore texture names with no extension. They appear to be lore-related names
				// that were used at one point in Arena's development.
				static_cast<void>(textureData);
			}
			else
			{
				DebugCrash("Unrecognized voxel texture extension \"" + textureName + "\".");
			}
		}
	};

	// Loads screen-space chasm textures into the renderer.
	auto loadChasmTextures = [this, &renderer, &palette]()
	{
		const int chasmWidth = RCIFile::WIDTH;
		const int chasmHeight = RCIFile::HEIGHT;
		Buffer<uint8_t> chasmBuffer(chasmWidth * chasmHeight);

		// Dry chasm (just a single color).
		constexpr uint8_t dryChasmColor = 112; // Matches the original game.
		chasmBuffer.fill(dryChasmColor);
		renderer.addChasmTexture(VoxelDefinition::ChasmData::Type::Dry, chasmBuffer.get(),
			chasmWidth, chasmHeight, palette);

		// Lambda for writing an .RCI animation to the renderer.
		auto writeChasmAnim = [&renderer, &palette, chasmWidth, chasmHeight](
			VoxelDefinition::ChasmData::Type chasmType, const std::string &rciName)
		{
			RCIFile rci;
			if (!rci.init(rciName.c_str()))
			{
				DebugLogError("Couldn't init .RCI \"" + rciName + "\".");
				return;
			}

			for (int i = 0; i < rci.getImageCount(); i++)
			{
				const uint8_t *rciPixels = rci.getPixels(i);
				renderer.addChasmTexture(chasmType, rciPixels, chasmWidth, chasmHeight, palette);
			}
		};

		writeChasmAnim(VoxelDefinition::ChasmData::Type::Wet, "WATERANI.RCI");
		writeChasmAnim(VoxelDefinition::ChasmData::Type::Lava, "LAVAANI.RCI");
	};

	// Initializes entities from the flat defs list and write their textures to the renderer.
	auto loadEntities = [this, nightLightsAreActive, &worldData, &provinceDef, &locationDef,
		&entityDefLibrary, &charClassLibrary, &binaryAssetLibrary, &random, &citizenManager,
		&textureManager, &textureInstManager, &renderer, &palette]()
	{
		// See whether the current ruler (if any) is male. This affects the displayed ruler in palaces.
		const std::optional<bool> rulerIsMale = [&locationDef]() -> std::optional<bool>
		{
			if (locationDef.getType() == LocationDefinition::Type::City)
			{
				const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
				return cityDef.rulerIsMale;
			}
			else
			{
				return std::nullopt;
			}
		}();

		const WorldType worldType = worldData.getWorldType();
		const std::optional<InteriorType> interiorType = [&worldData, worldType]()
			-> std::optional<InteriorType>
		{
			if (worldType == WorldType::Interior)
			{
				const InteriorWorldData &interior = static_cast<const InteriorWorldData&>(worldData);
				const ArenaTypes::MenuType interiorMenuType = interior.getInteriorType();
				return InteriorUtils::menuTypeToInteriorType(interiorMenuType);
			}
			else
			{
				return std::nullopt;
			}
		}();

		const auto &exeData = binaryAssetLibrary.getExeData();
		for (const auto &flatDef : this->flatsLists)
		{
			const ArenaTypes::FlatIndex flatIndex = flatDef.getFlatIndex();
			const INFFile::FlatData &flatData = this->inf.getFlat(flatIndex);
			const EntityType entityType = ArenaAnimUtils::getEntityTypeFromFlat(flatIndex, this->inf);
			const std::optional<ArenaTypes::ItemIndex> &optItemIndex = flatData.itemIndex;

			bool isFinalBoss;
			const bool isCreature = optItemIndex.has_value() &&
				ArenaAnimUtils::isCreatureIndex(*optItemIndex, &isFinalBoss);
			const bool isHumanEnemy = optItemIndex.has_value() &&
				ArenaAnimUtils::isHumanEnemyIndex(*optItemIndex);

			// Must be at least one instance of the entity for the loop to try and
			// instantiate it and write textures to the renderer.
			DebugAssert(flatDef.getPositions().size() > 0);

			// Add entity animation data. Static entities have only idle animations (and maybe on/off
			// state for lampposts). Dynamic entities have several animation states and directions.
			//auto &entityAnimData = newEntityDef.getAnimationData();
			EntityAnimationDefinition entityAnimDef;
			EntityAnimationInstance entityAnimInst;
			if (entityType == EntityType::Static)
			{
				if (!ArenaAnimUtils::tryMakeStaticEntityAnims(flatIndex, worldType, interiorType,
					rulerIsMale, this->inf, textureManager, &entityAnimDef, &entityAnimInst))
				{
					DebugLogWarning("Couldn't make static entity anims for flat \"" +
						std::to_string(flatIndex) + "\".");
					continue;
				}

				// The entity can only be instantiated if there is at least an idle animation.
				int idleStateIndex;
				if (!entityAnimDef.tryGetStateIndex(EntityAnimationUtils::STATE_IDLE.c_str(), &idleStateIndex))
				{
					DebugLogWarning("Missing static entity idle anim state for flat \"" +
						std::to_string(flatIndex) + "\".");
					continue;
				}
			}
			else if (entityType == EntityType::Dynamic)
			{
				// Assume that human enemies in level data are male.
				const std::optional<bool> isMale = true;

				if (!ArenaAnimUtils::tryMakeDynamicEntityAnims(flatIndex, isMale, this->inf,
					charClassLibrary, binaryAssetLibrary, textureManager, &entityAnimDef,
					&entityAnimInst))
				{
					DebugLogWarning("Couldn't make dynamic entity anims for flat \"" +
						std::to_string(flatIndex) + "\".");
					continue;
				}

				// Must have at least an idle animation.
				int idleStateIndex;
				if (!entityAnimDef.tryGetStateIndex(EntityAnimationUtils::STATE_IDLE.c_str(), &idleStateIndex))
				{
					DebugLogWarning("Missing dynamic entity idle anim state for flat \"" +
						std::to_string(flatIndex) + "\".");
					continue;
				}
			}
			else
			{
				DebugCrash("Unrecognized entity type \"" +
					std::to_string(static_cast<int>(entityType)) + "\".");
			}

			// @todo: replace isCreature/etc. with some flatIndex -> EntityDefinition::Type function.
			// - Most likely also need location type, etc. because flatIndex is level-dependent.
			EntityDefinition newEntityDef;
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
					continue;
				}

				newEntityDef = entityDefLibrary.getDefinition(entityDefID);
			}
			else if (isHumanEnemy)
			{
				const bool male = (random.next() % 2) == 0;
				const int charClassID = ArenaAnimUtils::getCharacterClassIndexFromItemIndex(*optItemIndex);
				newEntityDef.initEnemyHuman(male, charClassID, std::move(entityAnimDef));
			}
			else // @todo: handle other entity definition types.
			{
				// Doodad.
				const bool streetLight = ArenaAnimUtils::isStreetLightFlatIndex(flatIndex, worldType);
				const double scale = ArenaAnimUtils::getDimensionModifier(flatData);
				const int lightIntensity = flatData.lightIntensity.has_value() ? *flatData.lightIntensity : 0;

				newEntityDef.initDoodad(flatData.yOffset, scale, flatData.collider,
					flatData.transparent, flatData.ceiling, streetLight, flatData.puddle,
					lightIntensity, std::move(entityAnimDef));
			}

			const bool isStreetlight = (newEntityDef.getType() == EntityDefinition::Type::Doodad) &&
				newEntityDef.getDoodad().streetlight;
			const bool isPuddle = (newEntityDef.getType() == EntityDefinition::Type::Doodad) &&
				newEntityDef.getDoodad().puddle;
			const EntityDefID entityDefID = this->entityManager.addEntityDef(
				std::move(newEntityDef), entityDefLibrary);
			const EntityDefinition &entityDefRef = this->entityManager.getEntityDef(
				entityDefID, entityDefLibrary);
			
			// Quick hack to get back the anim def that was moved into the entity def.
			const EntityAnimationDefinition &entityAnimDefRef = entityDefRef.getAnimDef();

			// Generate render ID for this entity type to share between identical instances.
			const EntityRenderID entityRenderID = renderer.makeEntityRenderID();

			// Initialize each instance of the flat def.
			for (const Int2 &position : flatDef.getPositions())
			{
				EntityRef entityRef = this->entityManager.makeEntity(entityType);

				// Using raw entity pointer in this scope for performance due to it currently being
				// impractical to use the ref wrapper when loading the entire wilderness.
				Entity *entityPtr = entityRef.get();

				if (entityType == EntityType::Static)
				{
					StaticEntity *staticEntity = dynamic_cast<StaticEntity*>(entityPtr);
					staticEntity->initDoodad(entityDefID, entityAnimInst);
				}
				else if (entityType == EntityType::Dynamic)
				{
					// All dynamic entities in a level are creatures (never citizens).
					DynamicEntity *dynamicEntity = dynamic_cast<DynamicEntity*>(entityPtr);
					dynamicEntity->initCreature(entityDefID, entityAnimInst,
						CardinalDirection::North, random);
				}
				else
				{
					DebugCrash("Unrecognized entity type \"" +
						std::to_string(static_cast<int>(entityType)) + "\".");
				}

				entityPtr->setRenderID(entityRenderID);

				// Set default animation state.
				int defaultStateIndex;
				if (!isStreetlight)
				{
					// Entities will use idle animation by default.
					if (!entityAnimDefRef.tryGetStateIndex(EntityAnimationUtils::STATE_IDLE.c_str(), &defaultStateIndex))
					{
						DebugLogWarning("Couldn't get idle state index for flat \"" +
							std::to_string(flatIndex) + "\".");
						continue;
					}
				}
				else
				{
					// Need to turn streetlights on or off at initialization.
					const std::string &streetlightStateName = nightLightsAreActive ?
						EntityAnimationUtils::STATE_ACTIVATED : EntityAnimationUtils::STATE_IDLE;

					if (!entityAnimDefRef.tryGetStateIndex(streetlightStateName.c_str(), &defaultStateIndex))
					{
						DebugLogWarning("Couldn't get \"" + streetlightStateName +
							"\" streetlight state index for flat \"" + std::to_string(flatIndex) + "\".");
						continue;
					}
				}

				EntityAnimationInstance &animInst = entityPtr->getAnimInstance();
				animInst.setStateIndex(defaultStateIndex);

				// Note: since the entity pointer is being used directly, update the position last
				// in scope to avoid a dangling pointer problem in case it changes chunks (from 0, 0).
				const NewDouble2 positionXZ(
					static_cast<SNDouble>(position.x) + 0.50,
					static_cast<WEDouble>(position.y) + 0.50);
				entityPtr->setPosition(positionXZ, this->entityManager, this->voxelGrid);
			}

			// Palette for renderer textures.
			const Palette &palette = [&textureManager]() -> const Palette&
			{
				const std::string &paletteName = PaletteFile::fromName(PaletteName::Default);
				PaletteID paletteID;
				if (!textureManager.tryGetPaletteID(paletteName.c_str(), &paletteID))
				{
					DebugCrash("Couldn't get default palette \"" + paletteName + "\".");
				}

				return textureManager.getPaletteHandle(paletteID);
			}();

			// Initialize renderer buffers for the entity animation then populate all textures
			// of the animation.
			renderer.setFlatTextures(entityRenderID, entityAnimDefRef, entityAnimInst, isPuddle,
				palette, textureManager, textureInstManager);
		}

		// Spawn citizens at level start if the conditions are met for the new level.
		const bool isCity = worldType == WorldType::City;
		const bool isWild = worldType == WorldType::Wilderness;
		if (isCity || isWild)
		{
			citizenManager.spawnCitizens(*this, provinceDef.getRaceID(), locationDef,
				entityDefLibrary, binaryAssetLibrary, random, textureManager, textureInstManager,
				renderer);
		}
	};

	loadVoxelTextures();
	loadChasmTextures();
	loadEntities();
}

void LevelData::tick(Game &game, double dt)
{
	this->updateFadingVoxels(dt);

	// Update entities.
	this->entityManager.tick(game, dt);
}
