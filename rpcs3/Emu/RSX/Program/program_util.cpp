#include "stdafx.h"
#include "program_util.h"

namespace rsx
{
	void fragment_program_texture_config::masked_transfer_impl(void* dst, const void* src, u16 mask)
	{
		const auto start = std::countr_zero(mask);
		const auto end = 16 - std::countl_zero(mask);
		const auto mem_offset = (start * sizeof(TIU_slot));
		const auto mem_size = (end - start) * sizeof(TIU_slot);
		std::memcpy(static_cast<u8*>(dst) + mem_offset, reinterpret_cast<const u8*>(src) + mem_offset, mem_size);
	}
}
