#include "ArenaCityUtils.h"
#include "ArenaWildUtils.h"
#include "ClimateType.h"
#include "ExteriorWorldData.h"
#include "InteriorWorldData.h"
#include "LocationDefinition.h"
#include "LocationType.h"
#include "LocationUtils.h"
#include "VoxelUtils.h"
#include "WeatherType.h"
#include "WorldType.h"
#include "../Assets/MIFUtils.h"

#include "components/debug/Debug.h"

ExteriorWorldData::ExteriorWorldData(ExteriorLevelData &&levelData, bool isCity)
	: levelData(std::move(levelData))
{
	this->isCity = isCity;
}

ExteriorWorldData::~ExteriorWorldData()
{

}

ExteriorWorldData ExteriorWorldData::loadCity(const LocationDefinition &locationDef,
	const ProvinceDefinition &provinceDef, const MIFFile &mif, WeatherType weatherType,
	int currentDay, int starCount, const BinaryAssetLibrary &binaryAssetLibrary,
	const TextAssetLibrary &textAssetLibrary, TextureManager &textureManager)
{
	const MIFFile::Level &level = mif.getLevel(0);
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	const DOSUtils::FilenameBuffer infName =
		ArenaCityUtils::generateInfName(cityDef.climateType, weatherType);

	// Generate level data for the city.
	ExteriorLevelData levelData = ExteriorLevelData::loadCity(
		locationDef, provinceDef, level, weatherType, currentDay, starCount,
		std::string(infName.data()), mif.getDepth(), mif.getWidth(), binaryAssetLibrary,
		textAssetLibrary, textureManager);

	// Generate world data from the level data.
	const bool isCity = true; // False in wilderness.
	ExteriorWorldData worldData(std::move(levelData), isCity);

	// Convert start points from the old coordinate system to the new one.
	for (int i = 0; i < mif.getStartPointCount(); i++)
	{
		const OriginalInt2 &point = mif.getStartPoint(i);
		const Double2 startPointReal = MIFUtils::convertStartPointToReal(point);
		worldData.startPoints.push_back(VoxelUtils::getTransformedVoxel(startPointReal));
	}

	return worldData;
}

ExteriorWorldData ExteriorWorldData::loadWilderness(const LocationDefinition &locationDef,
	const ProvinceDefinition &provinceDef, WeatherType weatherType, int currentDay, int starCount,
	const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager)
{
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	const DOSUtils::FilenameBuffer infName =
		ArenaWildUtils::generateInfName(cityDef.climateType, weatherType);

	// Load wilderness data (no starting points to load).
	ExteriorLevelData levelData = ExteriorLevelData::loadWilderness(
		locationDef, provinceDef, weatherType, currentDay, starCount, std::string(infName.data()),
		binaryAssetLibrary, textureManager);

	const bool isCity = false; // False if wilderness.

	// Generate world data from the wilderness data.
	ExteriorWorldData worldData(std::move(levelData), isCity);
	return worldData;
}

WorldType ExteriorWorldData::getWorldType() const
{
	return this->isCity ? WorldType::City : WorldType::Wilderness;
}

LevelData &ExteriorWorldData::getActiveLevel()
{
	return static_cast<LevelData&>(this->levelData);
}

const LevelData &ExteriorWorldData::getActiveLevel() const
{
	return static_cast<const LevelData&>(this->levelData);
}
