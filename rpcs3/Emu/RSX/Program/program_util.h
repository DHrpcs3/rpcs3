#pragma once

#include "util/types.hpp"
#include "../gcm_enums.h"

namespace rsx
{
	enum program_limits
	{
		max_vertex_program_instructions = 544
	};

	// Convert u16 to u32
	// FIXME: give better name
	static constexpr u32 duplicate_and_extend(u16 bits)
	{
		u32 x = bits;

		x = (x | (x << 8)) & 0x00FF00FF;
		x = (x | (x << 4)) & 0x0F0F0F0F;
		x = (x | (x << 2)) & 0x33333333;
		x = (x | (x << 1)) & 0x55555555;

		return x | (x << 1);
	}


#pragma pack(push, 1)
	// NOTE: This structure must be packed to match GPU layout (std140).
	struct fragment_program_texture_config
	{
		struct TIU_slot
		{
			float scale[3];
			float subpixel_bias;
			u32 remap;
			u32 control;
			u32 padding[2];
		};

		TIU_slot slots[16];

		TIU_slot& operator[](u32 index) { return slots[index]; }

		void write_to(void* dst, u16 mask)
		{
			masked_transfer(dst, slots, mask);
		}

		void load_from(const void* src, u16 mask)
		{
			masked_transfer(slots, src, mask);
		}

		static void masked_transfer(void* dst, const void* src, u16 mask)
		{	
			// Try to optimize for the very common case (first 4 slots used)
			switch (mask)
			{
			case 0:
				break;
			case 1:
				std::memcpy(dst, src, sizeof(TIU_slot));
				break;
			case 3:
				std::memcpy(dst, src, sizeof(TIU_slot) * 2);
				break;
			case 7:
				std::memcpy(dst, src, sizeof(TIU_slot) * 3);
				break;
			case 15:
				std::memcpy(dst, src, sizeof(TIU_slot) * 4);
				break;

			default:
				// fallback to generic implementation
				masked_transfer_impl(dst, src, mask);
				break;
			};

		}

	private:
		static void masked_transfer_impl(void* dst, const void* src, u16 mask);
	};
#pragma pack(pop)

	struct fragment_program_texture_state
	{
		u32 texture_dimensions = 0;
		u16 redirected_textures = 0;
		u16 shadow_textures = 0;

		void clear(u32 index)
		{
			const u16 clear_mask = ~(static_cast<u16>(1 << index));
			redirected_textures &= clear_mask;
			shadow_textures &= clear_mask;
		}

		void import(const fragment_program_texture_state& other, u16 mask)
		{
			redirected_textures = other.redirected_textures & mask;
			shadow_textures = other.shadow_textures & mask;
			texture_dimensions = other.texture_dimensions & duplicate_and_extend(mask);
		}
		void set_dimension(texture_dimension_extended type, u32 index)
		{
			const auto offset = (index * 2);
			const auto mask = static_cast<u32>(3) << offset;
			texture_dimensions &= ~mask;
			texture_dimensions |= static_cast<u32>(type) << offset;
		}

		bool operator == (const fragment_program_texture_state& other) const
		{
			return texture_dimensions == other.texture_dimensions &&
				redirected_textures == other.redirected_textures &&
				shadow_textures == other.shadow_textures;
		}
	};

	struct vertex_program_texture_state
	{
		u32 texture_dimensions = 0;

		void clear(u32 /* index */)
		{
			// Nothing to do yet
		}

		void import(const vertex_program_texture_state& other, u16 mask)
		{
			texture_dimensions = other.texture_dimensions & duplicate_and_extend(mask);
		}

		void set_dimension(texture_dimension_extended type, u32 index)
		{
			const auto offset = (index * 2);
			const auto mask = static_cast<u32>(3) << offset;
			texture_dimensions &= ~mask;
			texture_dimensions |= static_cast<u32>(type) << offset;
		}

		bool operator==(const vertex_program_texture_state& other) const
		{
			return texture_dimensions == other.texture_dimensions;
		}
	};
}
