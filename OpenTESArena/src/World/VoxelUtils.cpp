#include "ChunkUtils.h"
#include "VoxelFacing2D.h"
#include "VoxelFacing3D.h"
#include "VoxelUtils.h"

#include "components/debug/Debug.h"

NewInt2 VoxelUtils::originalVoxelToNewVoxel(const OriginalInt2 &voxel)
{
	return NewInt2(voxel.y, voxel.x);
}

OriginalInt2 VoxelUtils::newVoxelToOriginalVoxel(const NewInt2 &voxel)
{
	return VoxelUtils::originalVoxelToNewVoxel(voxel);
}

Double2 VoxelUtils::getTransformedVoxel(const Double2 &voxel)
{
	return Double2(voxel.y, voxel.x);
}

NewInt2 VoxelUtils::chunkVoxelToNewVoxel(const ChunkInt2 &chunk, const VoxelInt2 &voxel)
{
	return (chunk * ChunkUtils::CHUNK_DIM) + voxel;
}

ChunkCoord VoxelUtils::newVoxelToChunkVoxel(const NewInt2 &voxel)
{
	// @todo: need to handle voxel outside grid.
	// @todo: probably want (int)Floor() instead of modulo.

	ChunkCoord chunkCoord;
	chunkCoord.chunk = ChunkInt2(voxel.x / ChunkUtils::CHUNK_DIM, voxel.y / ChunkUtils::CHUNK_DIM);
	chunkCoord.voxel = VoxelInt2(voxel.x % ChunkUtils::CHUNK_DIM, voxel.y % ChunkUtils::CHUNK_DIM);
	return chunkCoord;
}

ChunkCoord VoxelUtils::levelVoxelToChunkVoxel(const LevelInt2 &voxel)
{
	// @todo: make sure it handles negative coordinates.
	return VoxelUtils::newVoxelToChunkVoxel(voxel);
}

ChunkInt2 VoxelUtils::newVoxelToChunk(const NewInt2 &voxel)
{
	const ChunkCoord chunkCoord = VoxelUtils::newVoxelToChunkVoxel(voxel);
	return chunkCoord.chunk;
}

VoxelInt2 VoxelUtils::wrapVoxelCoord(const VoxelInt2 &voxel)
{
	// @todo: handle negative numbers
	return VoxelInt2(voxel.x % ChunkUtils::CHUNK_DIM, voxel.y % ChunkUtils::CHUNK_DIM);
}

Double3 VoxelUtils::getNormal(VoxelFacing2D facing)
{
	if (facing == VoxelFacing2D::PositiveX)
	{
		return Double3::UnitX;
	}
	else if (facing == VoxelFacing2D::NegativeX)
	{
		return -Double3::UnitX;
	}
	else if (facing == VoxelFacing2D::PositiveZ)
	{
		return Double3::UnitZ;
	}
	else if (facing == VoxelFacing2D::NegativeZ)
	{
		return -Double3::UnitZ;
	}
	else
	{
		DebugUnhandledReturnMsg(Double3, std::to_string(static_cast<int>(facing)));
	}
}

VoxelFacing3D VoxelUtils::convertFaceTo3D(VoxelFacing2D facing)
{
	if (facing == VoxelFacing2D::PositiveX)
	{
		return VoxelFacing3D::PositiveX;
	}
	else if (facing == VoxelFacing2D::NegativeX)
	{
		return VoxelFacing3D::NegativeX;
	}
	else if (facing == VoxelFacing2D::PositiveZ)
	{
		return VoxelFacing3D::PositiveZ;
	}
	else if (facing == VoxelFacing2D::NegativeZ)
	{
		return VoxelFacing3D::NegativeZ;
	}
	else
	{
		DebugUnhandledReturnMsg(VoxelFacing3D, std::to_string(static_cast<int>(facing)));
	}
}

std::optional<VoxelFacing2D> VoxelUtils::tryConvertFaceTo2D(VoxelFacing3D facing)
{
	if (facing == VoxelFacing3D::PositiveX)
	{
		return VoxelFacing2D::PositiveX;
	}
	else if (facing == VoxelFacing3D::NegativeX)
	{
		return VoxelFacing2D::NegativeX;
	}
	else if (facing == VoxelFacing3D::PositiveZ)
	{
		return VoxelFacing2D::PositiveZ;
	}
	else if (facing == VoxelFacing3D::NegativeZ)
	{
		return VoxelFacing2D::NegativeZ;
	}
	else if ((facing == VoxelFacing3D::PositiveY) || (facing == VoxelFacing3D::NegativeY))
	{
		return std::nullopt;
	}
	else
	{
		DebugUnhandledReturnMsg(VoxelFacing2D, std::to_string(static_cast<int>(facing)));
	}
}
