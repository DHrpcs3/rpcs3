#include "RSXFragmentProgram.h"

std::string_view rsx_fp_input_attr_regs[15] =
	{
		"WPOS", "COL0", "COL1", "FOGC", "TEX0",
		"TEX1", "TEX2", "TEX3", "TEX4", "TEX5",
		"TEX6", "TEX7", "TEX8", "TEX9", "SSA"};

std::string_view rsx_fp_op_names[70] =
	{
		"NOP", "MOV", "MUL", "ADD", "MAD", "DP3", "DP4",
		"DST", "MIN", "MAX", "SLT", "SGE", "SLE", "SGT",
		"SNE", "SEQ", "FRC", "FLR", "KIL", "PK4", "UP4",
		"DDX", "DDY", "TEX", "TXP", "TXD", "RCP", "RSQ",
		"EX2", "LG2", "LIT", "LRP", "STR", "SFL", "COS",
		"SIN", "PK2", "UP2", "POW", "PKB", "UPB", "PK16",
		"UP16", "BEM", "PKG", "UPG", "DP2A", "TXL", "NULL",
		"TXB", "NULL", "TEXBEM", "TXPBEM", "BEMLUM", "REFL", "TIMESWTEX",
		"DP2", "NRM", "DIV", "DIVSQ", "LIF", "FENCT", "FENCB",
		"NULL", "BRK", "CAL", "IFE", "LOOP", "REP", "RET"};

void rsx::fragment_program_info::function_info::add_block(u32 offset)
{
	u32 index = 0;

	while (index < blocks_count && offset > blocks[index])
	{
		index++;
	}

	ensure(index < max_blocks_count);

	if (offset == blocks[index])
	{
		return;
	}

	if (index + 1 < blocks_count)
	{
		std::memmove(blocks + index + 1, blocks + index, blocks_count - index - 1);
	}
	else
	{
		blocks_count = index + 1;
	}

	blocks[index] = offset;
}

void rsx::analyze_fragment_program(fragment_program_info* dst, const u32* ucode)
{
	auto* current_function   = dst->functions;
	current_function->offset = 0;
	dst->function_count      = 1;

	while (current_function != dst->functions + dst->function_count)
	{
		u64 hash   = 0xCBF29CE484222325ULL;
		u32 offset = current_function->offset;

		while (true)
		{
			fp_instruction instruction;
			std::memcpy(&instruction, ucode + offset, sizeof(instruction));

			if (instruction.dest.end())
			{
				break;
			}

			{
				std::uint64_t instrHEX[2];
				std::memcpy(&instrHEX, &instruction, sizeof(instrHEX));

				hash ^= instrHEX[0];
				hash += (hash << 1) + (hash << 4) + (hash << 5) + (hash << 7) + (hash << 8) + (hash << 40);
				hash ^= instrHEX[1];
				hash += (hash << 1) + (hash << 4) + (hash << 5) + (hash << 7) + (hash << 8) + (hash << 40);
			}

			offset += 4;

			auto exec_cond = instruction.src0.exec_cond();

			if (instruction.src1.opcode_is_branch())
			{
				if (exec_cond == fp_cond::never)
				{
					continue;
				}

				bool end = false;

				switch (instruction.dest.opcode() | 0x40)
				{
					break;
				case RSX_FP_OPCODE_CAL:
					dst->functions[dst->function_count++].offset = instruction.src2.end_offset();
					break;

				case RSX_FP_OPCODE_IFE:
				{
					auto else_offset = instruction.src1.else_offset();
					auto end_offset  = instruction.src2.end_offset();
					current_function->add_block(else_offset);
					if (else_offset != end_offset)
					{
						current_function->add_block(else_offset);
					}
					break;
				}

				case RSX_FP_OPCODE_REP:
				case RSX_FP_OPCODE_BRK:
				case RSX_FP_OPCODE_LOOP:
					current_function->add_block(instruction.src2.end_offset());
					break;

				case RSX_FP_OPCODE_RET:
					end = exec_cond == fp_cond::always;
					break;

				default:
					break;
				}

				if (end)
				{
					break;
				}
			}
			else
			{
				if (exec_cond != fp_cond::never)
				{
					// TODO
				}

				if (instruction.src0.reg_type() == RSX_FP_REGISTER_TYPE_CONSTANT ||
					instruction.src1.reg_type() == RSX_FP_REGISTER_TYPE_CONSTANT ||
					instruction.src2.reg_type() == RSX_FP_REGISTER_TYPE_CONSTANT)
				{
					current_function->mark_constant(offset);
					offset += 4;
				}
			}
		}

		current_function->size = offset - current_function->offset;
		current_function->hash = hash;
		++current_function;
	}
}

void rsx::convert_fragment_program_to_spirv(std::vector<u32>& spirv, fragment_program_info* info, const u32* ucode)
{
    // TODO: implement
}
