#pragma once

#include "program_util.h"
#include "util/endian.hpp"

#include <string>
#include <vector>

enum fp_register_type
{
	RSX_FP_REGISTER_TYPE_TEMP     = 0,
	RSX_FP_REGISTER_TYPE_INPUT    = 1,
	RSX_FP_REGISTER_TYPE_CONSTANT = 2,
	RSX_FP_REGISTER_TYPE_UNKNOWN  = 3,
};

enum fp_register_precision
{
	RSX_FP_PRECISION_REAL     = 0,
	RSX_FP_PRECISION_HALF     = 1,
	RSX_FP_PRECISION_FIXED12  = 2,
	RSX_FP_PRECISION_FIXED9   = 3,
	RSX_FP_PRECISION_SATURATE = 4,
	RSX_FP_PRECISION_UNKNOWN  = 5 // Unknown what this actually does; seems to do nothing on hwtests but then why would their compiler emit it?
};

enum fp_opcode
{
	RSX_FP_OPCODE_NOP       = 0x00, // No-Operation
	RSX_FP_OPCODE_MOV       = 0x01, // Move
	RSX_FP_OPCODE_MUL       = 0x02, // Multiply
	RSX_FP_OPCODE_ADD       = 0x03, // Add
	RSX_FP_OPCODE_MAD       = 0x04, // Multiply-Add
	RSX_FP_OPCODE_DP3       = 0x05, // 3-component Dot Product
	RSX_FP_OPCODE_DP4       = 0x06, // 4-component Dot Product
	RSX_FP_OPCODE_DST       = 0x07, // Distance
	RSX_FP_OPCODE_MIN       = 0x08, // Minimum
	RSX_FP_OPCODE_MAX       = 0x09, // Maximum
	RSX_FP_OPCODE_SLT       = 0x0A, // Set-If-LessThan
	RSX_FP_OPCODE_SGE       = 0x0B, // Set-If-GreaterEqual
	RSX_FP_OPCODE_SLE       = 0x0C, // Set-If-LessEqual
	RSX_FP_OPCODE_SGT       = 0x0D, // Set-If-GreaterThan
	RSX_FP_OPCODE_SNE       = 0x0E, // Set-If-NotEqual
	RSX_FP_OPCODE_SEQ       = 0x0F, // Set-If-Equal
	RSX_FP_OPCODE_FRC       = 0x10, // Fraction (fract)
	RSX_FP_OPCODE_FLR       = 0x11, // Floor
	RSX_FP_OPCODE_KIL       = 0x12, // Kill fragment
	RSX_FP_OPCODE_PK4       = 0x13, // Pack four signed 8-bit values
	RSX_FP_OPCODE_UP4       = 0x14, // Unpack four signed 8-bit values
	RSX_FP_OPCODE_DDX       = 0x15, // Partial-derivative in x (Screen space derivative w.r.t. x)
	RSX_FP_OPCODE_DDY       = 0x16, // Partial-derivative in y (Screen space derivative w.r.t. y)
	RSX_FP_OPCODE_TEX       = 0x17, // Texture lookup
	RSX_FP_OPCODE_TXP       = 0x18, // Texture sample with projection (Projective texture lookup)
	RSX_FP_OPCODE_TXD       = 0x19, // Texture sample with partial differentiation (Texture lookup with derivatives)
	RSX_FP_OPCODE_RCP       = 0x1A, // Reciprocal
	RSX_FP_OPCODE_RSQ       = 0x1B, // Reciprocal Square Root
	RSX_FP_OPCODE_EX2       = 0x1C, // Exponentiation base 2
	RSX_FP_OPCODE_LG2       = 0x1D, // Log base 2
	RSX_FP_OPCODE_LIT       = 0x1E, // Lighting coefficients
	RSX_FP_OPCODE_LRP       = 0x1F, // Linear Interpolation
	RSX_FP_OPCODE_STR       = 0x20, // Set-If-True
	RSX_FP_OPCODE_SFL       = 0x21, // Set-If-False
	RSX_FP_OPCODE_COS       = 0x22, // Cosine
	RSX_FP_OPCODE_SIN       = 0x23, // Sine
	RSX_FP_OPCODE_PK2       = 0x24, // Pack two 16-bit floats
	RSX_FP_OPCODE_UP2       = 0x25, // Unpack two 16-bit floats
	RSX_FP_OPCODE_POW       = 0x26, // Power
	RSX_FP_OPCODE_PKB       = 0x27, // Pack bytes
	RSX_FP_OPCODE_UPB       = 0x28, // Unpack bytes
	RSX_FP_OPCODE_PK16      = 0x29, // Pack 16 bits
	RSX_FP_OPCODE_UP16      = 0x2A, // Unpack 16
	RSX_FP_OPCODE_BEM       = 0x2B, // Bump-environment map (a.k.a. 2D coordinate transform)
	RSX_FP_OPCODE_PKG       = 0x2C, // Pack with sRGB transformation
	RSX_FP_OPCODE_UPG       = 0x2D, // Unpack gamma
	RSX_FP_OPCODE_DP2A      = 0x2E, // 2-component dot product with scalar addition
	RSX_FP_OPCODE_TXL       = 0x2F, // Texture sample with explicit LOD
	RSX_FP_OPCODE_TXB       = 0x31, // Texture sample with bias
	RSX_FP_OPCODE_TEXBEM    = 0x33,
	RSX_FP_OPCODE_TXPBEM    = 0x34,
	RSX_FP_OPCODE_BEMLUM    = 0x35,
	RSX_FP_OPCODE_REFL      = 0x36, // Reflection vector
	RSX_FP_OPCODE_TIMESWTEX = 0x37,
	RSX_FP_OPCODE_DP2       = 0x38, // 2-component dot product
	RSX_FP_OPCODE_NRM       = 0x39, // Normalize
	RSX_FP_OPCODE_DIV       = 0x3A, // Division
	RSX_FP_OPCODE_DIVSQ     = 0x3B, // Divide by Square Root
	RSX_FP_OPCODE_LIF       = 0x3C, // Final part of LIT
	RSX_FP_OPCODE_FENCT     = 0x3D, // Fence T?
	RSX_FP_OPCODE_FENCB     = 0x3E, // Fence B?
	RSX_FP_OPCODE_BRK       = 0x40, // Break
	RSX_FP_OPCODE_CAL       = 0x41, // Subroutine call
	RSX_FP_OPCODE_IFE       = 0x42, // If
	RSX_FP_OPCODE_LOOP      = 0x43, // Loop
	RSX_FP_OPCODE_REP       = 0x44, // Repeat
	RSX_FP_OPCODE_RET       = 0x45  // Return
};

// TODO: move following functions somewere to utilities code
inline constexpr std::uint32_t getBits(std::uint32_t value, int end,
	int begin)
{
	return (value >> begin) & ((1u << (end - begin + 1)) - 1);
}

inline constexpr std::uint32_t getBit(std::uint32_t value, int bit)
{
	return (value >> bit) & 1;
}

inline constexpr std::uint32_t genMask(std::uint32_t offset,
	std::uint32_t bitCount)
{
	return ((1u << bitCount) - 1u) << offset;
}

inline constexpr std::uint32_t getMaskEnd(std::uint32_t mask)
{
	return 32 - std::countl_zero(mask);
}

inline constexpr std::uint32_t getMaskSize(std::uint32_t mask)
{
	return std::popcount(mask);
}

inline constexpr std::uint32_t fetchMaskedValue(std::uint32_t hex,
	std::uint32_t mask)
{
	return (hex & mask) >> std::countr_zero(mask);
}

inline constexpr std::uint32_t fetchHiLoMaskedValue(std::uint32_t hex,
	std::uint32_t mask_lo, std::uint32_t mask_hi)
{
	return fetchMaskedValue(hex, mask_lo) | (fetchMaskedValue(hex, mask_hi) << std::popcount(mask_lo));
}

namespace rsx
{
	enum class fp_cond
	{
		never = 0,
		lt    = 1 << 0,
		eq    = 1 << 1,
		gt    = 1 << 2,
		le    = lt | eq,
		ge    = gt | eq,
		ne    = lt | gt,

		always = lt | eq | gt,
	};

	struct fp_instruction
	{
		struct dest_t
		{
			static constexpr auto be_end_mask                 = genMask(8, 1);
			static constexpr auto be_dest_reg_mask            = genMask(getMaskEnd(be_end_mask), 6);
			static constexpr auto be_fp16_mask                = genMask(getMaskEnd(be_dest_reg_mask), 1);
			static constexpr auto be_set_cond_mask            = genMask(0, 1);
			static constexpr auto be_mask_x_mask              = genMask(getMaskEnd(be_set_cond_mask), 1);
			static constexpr auto be_mask_y_mask              = genMask(getMaskEnd(be_mask_x_mask), 1);
			static constexpr auto be_mask_z_mask              = genMask(getMaskEnd(be_mask_y_mask), 1);
			static constexpr auto be_mask_w_mask              = genMask(getMaskEnd(be_mask_z_mask), 1);
			static constexpr auto be_src_attr_reg_num_lo_mask = genMask(getMaskEnd(be_mask_w_mask), 3);
			static constexpr auto be_src_attr_reg_num_hi_mask = genMask(24, 1);
			static constexpr auto be_tex_num_mask             = genMask(getMaskEnd(be_src_attr_reg_num_hi_mask), 4);
			static constexpr auto be_exp_tex_mask             = genMask(getMaskEnd(be_tex_num_mask), 1);
			static constexpr auto be_prec_mask                = genMask(getMaskEnd(be_exp_tex_mask), 2);
			static constexpr auto be_opcode_mask              = genMask(16, 6);
			static constexpr auto be_no_dest_mask             = genMask(getMaskEnd(be_opcode_mask), 1);
			static constexpr auto be_saturate_mask            = genMask(getMaskEnd(be_no_dest_mask), 1);

			u32 hex;

			bool end() const
			{
				return fetchMaskedValue(hex, be_end_mask);
			}
			u32 dest_reg() const
			{
				return fetchMaskedValue(hex, be_dest_reg_mask);
			}
			bool fp16() const
			{
				return fetchMaskedValue(hex, be_fp16_mask);
			}
			bool set_cond() const
			{
				return fetchMaskedValue(hex, be_set_cond_mask);
			}
			bool mask_x() const
			{
				return fetchMaskedValue(hex, be_mask_x_mask);
			}
			bool mask_y() const
			{
				return fetchMaskedValue(hex, be_mask_y_mask);
			}
			bool mask_z() const
			{
				return fetchMaskedValue(hex, be_mask_z_mask);
			}
			bool mask_w() const
			{
				return fetchMaskedValue(hex, be_mask_w_mask);
			}
			u32 src_attr_reg_num() const
			{
				return fetchHiLoMaskedValue(hex, be_src_attr_reg_num_lo_mask, be_src_attr_reg_num_hi_mask);
			}
			u32 tex_num() const
			{
				return fetchMaskedValue(hex, be_tex_num_mask);
			}
			bool exp_tex() const
			{
				return fetchMaskedValue(hex, be_exp_tex_mask);
			}
			fp_register_precision prec() const
			{
				return static_cast<fp_register_precision>(fetchMaskedValue(hex, be_prec_mask));
			}
			fp_opcode opcode() const
			{
				return static_cast<fp_opcode>(fetchMaskedValue(hex, be_opcode_mask));
			}
			bool no_dest() const
			{
				return fetchMaskedValue(hex, be_no_dest_mask);
			}
			bool saturate() const
			{
				return fetchMaskedValue(hex, be_saturate_mask);
			}
		};

		struct src_t
		{
			static constexpr auto be_reg_type_mask      = genMask(8, 2);
			static constexpr auto be_tmp_reg_index_mask = genMask(getMaskEnd(be_reg_type_mask), 6);
			static constexpr auto be_fp16_mask          = genMask(0, 1);
			static constexpr auto be_swizzle_x_mask     = genMask(getMaskEnd(be_fp16_mask), 2);
			static constexpr auto be_swizzle_y_mask     = genMask(getMaskEnd(be_swizzle_x_mask), 2);
			static constexpr auto be_swizzle_z_mask     = genMask(getMaskEnd(be_swizzle_y_mask), 2);
			static constexpr auto be_swizzle_w_lo_mask  = genMask(getMaskEnd(be_swizzle_z_mask), 1);
			static constexpr auto be_swizzle_w_hi_mask  = genMask(24, 1);
			static constexpr auto be_neg_mask           = genMask(getMaskEnd(be_swizzle_w_hi_mask), 1);

			u32 hex;

			fp_register_type reg_type() const
			{
				return static_cast<fp_register_type>(fetchMaskedValue(hex, be_reg_type_mask));
			}
			u32 tmp_reg_index() const
			{
				return fetchMaskedValue(hex, be_tmp_reg_index_mask);
			}
			bool fp16() const
			{
				return fetchMaskedValue(hex, be_fp16_mask);
			}
			u32 swizzle_x() const
			{
				return fetchMaskedValue(hex, be_swizzle_x_mask);
			}
			u32 swizzle_y() const
			{
				return fetchMaskedValue(hex, be_swizzle_y_mask);
			}
			u32 swizzle_z() const
			{
				return fetchMaskedValue(hex, be_swizzle_z_mask);
			}
			u32 swizzle_w() const
			{
				return fetchHiLoMaskedValue(hex, be_swizzle_w_lo_mask, be_swizzle_w_hi_mask);
			}
			bool neg() const
			{
				return fetchMaskedValue(hex, be_neg_mask);
			}
		};

		struct src0_t
		{
			static constexpr auto be_reg_type_mask           = genMask(8, 2);
			static constexpr auto be_tmp_reg_index_mask      = genMask(getMaskEnd(be_reg_type_mask), 6);
			static constexpr auto be_fp16_mask               = genMask(0, 1);
			static constexpr auto be_swizzle_x_mask          = genMask(getMaskEnd(be_fp16_mask), 2);
			static constexpr auto be_swizzle_y_mask          = genMask(getMaskEnd(be_swizzle_x_mask), 2);
			static constexpr auto be_swizzle_z_mask          = genMask(getMaskEnd(be_swizzle_y_mask), 2);
			static constexpr auto be_swizzle_w_lo_mask       = genMask(getMaskEnd(be_swizzle_z_mask), 1);
			static constexpr auto be_swizzle_w_hi_mask       = genMask(24, 1);
			static constexpr auto be_neg_mask                = genMask(getMaskEnd(be_swizzle_w_hi_mask), 1);
			static constexpr auto be_exec_cond_mask          = genMask(getMaskEnd(be_neg_mask), 3);
			static constexpr auto be_cond_swizzle_x_mask     = genMask(29, 2);
			static constexpr auto be_cond_swizzle_y_lo_mask  = genMask(getMaskEnd(be_cond_swizzle_x_mask), 1);
			static constexpr auto be_cond_swizzle_y_hi_mask  = genMask(16, 1);
			static constexpr auto be_cond_swizzle_z_mask     = genMask(getMaskEnd(be_cond_swizzle_y_hi_mask), 2);
			static constexpr auto be_cond_swizzle_w_mask     = genMask(getMaskEnd(be_cond_swizzle_z_mask), 2);
			static constexpr auto be_abs_mask                = genMask(getMaskEnd(be_cond_swizzle_z_mask), 1);
			static constexpr auto be_cond_mod_reg_index_mask = genMask(getMaskEnd(be_abs_mask), 1);
			static constexpr auto be_cond_reg_index_mask     = genMask(getMaskEnd(be_cond_mod_reg_index_mask), 1);

			u32 hex;

			fp_register_type reg_type() const
			{
				return static_cast<fp_register_type>(fetchMaskedValue(hex, be_reg_type_mask));
			}
			u32 tmp_reg_index() const
			{
				return fetchMaskedValue(hex, be_tmp_reg_index_mask);
			}
			bool fp16() const
			{
				return fetchMaskedValue(hex, be_fp16_mask);
			}
			u32 swizzle_x() const
			{
				return fetchMaskedValue(hex, be_swizzle_x_mask);
			}
			u32 swizzle_y() const
			{
				return fetchMaskedValue(hex, be_swizzle_y_mask);
			}
			u32 swizzle_z() const
			{
				return fetchMaskedValue(hex, be_swizzle_z_mask);
			}
			u32 swizzle_w() const
			{
				return fetchHiLoMaskedValue(hex, be_swizzle_w_lo_mask, be_swizzle_w_hi_mask);
			}
			bool neg() const
			{
				return fetchMaskedValue(hex, be_neg_mask);
			}
			fp_cond exec_cond() const
			{
				return static_cast<fp_cond>(fetchMaskedValue(hex, be_exec_cond_mask));
			}
			u32 cond_swizzle_x() const
			{
				return fetchMaskedValue(hex, be_cond_swizzle_x_mask);
			}
			u32 cond_swizzle_y() const
			{
				return fetchHiLoMaskedValue(hex, be_cond_swizzle_y_lo_mask, be_cond_swizzle_y_hi_mask);
			}
			u32 cond_swizzle_z() const
			{
				return fetchMaskedValue(hex, be_cond_swizzle_z_mask);
			}
			u32 cond_swizzle_w() const
			{
				return fetchMaskedValue(hex, be_cond_swizzle_w_mask);
			}
			bool abs() const
			{
				return fetchMaskedValue(hex, be_abs_mask);
			}
			bool cond_mod_reg_index() const
			{
				return fetchMaskedValue(hex, be_cond_mod_reg_index_mask);
			}
			bool cond_reg_index() const
			{
				return fetchMaskedValue(hex, be_cond_reg_index_mask);
			}
		};

		struct src1_t
		{
			static constexpr auto be_reg_type_mask         = genMask(8, 2);
			static constexpr auto be_tmp_reg_index_mask    = genMask(getMaskEnd(be_reg_type_mask), 6);
			static constexpr auto be_fp16_mask             = genMask(0, 1);
			static constexpr auto be_swizzle_x_mask        = genMask(getMaskEnd(be_fp16_mask), 2);
			static constexpr auto be_swizzle_y_mask        = genMask(getMaskEnd(be_swizzle_x_mask), 2);
			static constexpr auto be_swizzle_z_mask        = genMask(getMaskEnd(be_swizzle_y_mask), 2);
			static constexpr auto be_swizzle_w_lo_mask     = genMask(getMaskEnd(be_swizzle_z_mask), 1);
			static constexpr auto be_swizzle_w_hi_mask     = genMask(24, 1);
			static constexpr auto be_neg_mask              = genMask(getMaskEnd(be_swizzle_w_hi_mask), 1);
			static constexpr auto be_abs_mask              = genMask(getMaskEnd(be_neg_mask), 1);
			static constexpr auto be_src0_prec_mod_mask    = genMask(getMaskEnd(be_abs_mask), 3);
			static constexpr auto be_src1_prec_mod_lo_mask = genMask(getMaskEnd(be_src0_prec_mod_mask), 2);
			static constexpr auto be_src1_prec_mod_hi_mask = genMask(16, 1);
			static constexpr auto be_src2_prec_mod_mask    = genMask(getMaskEnd(be_src1_prec_mod_hi_mask), 3);
			static constexpr auto be_scale_mask            = genMask(getMaskEnd(be_src2_prec_mod_mask), 3);
			static constexpr auto be_opcode_is_branch_mask = genMask(getMaskEnd(be_scale_mask), 1);

			static constexpr auto be_end_counter_lo_mask  = genMask(10, 6);
			static constexpr auto be_end_counter_hi_mask  = genMask(0, 2);
			static constexpr auto be_init_counter_lo_mask = genMask(getMaskEnd(be_end_counter_hi_mask), 6);
			static constexpr auto be_init_counter_hi_mask = genMask(24, 2);
			static constexpr auto be_increment_lo_mask    = genMask(getMaskEnd(be_init_counter_hi_mask) + 1, 5);
			static constexpr auto be_increment_hi_mask    = genMask(16, 3);

			u32 hex;

			fp_register_type reg_type() const
			{
				return static_cast<fp_register_type>(fetchMaskedValue(hex, be_reg_type_mask));
			}
			u32 tmp_reg_index() const
			{
				return fetchMaskedValue(hex, be_tmp_reg_index_mask);
			}
			bool fp16() const
			{
				return fetchMaskedValue(hex, be_fp16_mask);
			}
			u32 swizzle_x() const
			{
				return fetchMaskedValue(hex, be_swizzle_x_mask);
			}
			u32 swizzle_y() const
			{
				return fetchMaskedValue(hex, be_swizzle_y_mask);
			}
			u32 swizzle_z() const
			{
				return fetchMaskedValue(hex, be_swizzle_z_mask);
			}
			u32 swizzle_w() const
			{
				return fetchHiLoMaskedValue(hex, be_swizzle_w_lo_mask, be_swizzle_w_hi_mask);
			}
			bool neg() const
			{
				return fetchMaskedValue(hex, be_neg_mask);
			}
			bool abs() const
			{
				return fetchMaskedValue(hex, be_abs_mask);
			}
			u32 src0_prec_mod() const
			{
				return fetchMaskedValue(hex, be_src0_prec_mod_mask);
			}
			u32 src1_prec_mod() const
			{
				return fetchHiLoMaskedValue(hex, be_src1_prec_mod_lo_mask, be_src1_prec_mod_hi_mask);
			}
			u32 src2_prec_mod() const
			{
				return fetchMaskedValue(hex, be_src2_prec_mod_mask);
			}
			u32 scale() const
			{
				return fetchMaskedValue(hex, be_scale_mask);
			}
			bool opcode_is_branch() const
			{
				return fetchMaskedValue(hex, be_opcode_is_branch_mask);
			}
			u32 end_counter() const
			{
				return fetchHiLoMaskedValue(hex, be_end_counter_lo_mask, be_end_counter_hi_mask);
			}
			u32 init_counter() const
			{
				return fetchHiLoMaskedValue(hex, be_init_counter_lo_mask, be_init_counter_hi_mask);
			}
			u32 increment() const
			{
				return fetchHiLoMaskedValue(hex, be_increment_lo_mask, be_increment_hi_mask);
			}

			u32 else_offset() const
			{
				return std::bit_cast<be_t<u32>>(hex << 16 | hex >> 16) << 2;
			}
		};

		struct src2_t
		{
			static constexpr auto be_reg_type_mask         = genMask(8, 2);
			static constexpr auto be_tmp_reg_index_mask    = genMask(getMaskEnd(be_reg_type_mask), 6);
			static constexpr auto be_fp16_mask             = genMask(0, 1);
			static constexpr auto be_swizzle_x_mask        = genMask(getMaskEnd(be_fp16_mask), 2);
			static constexpr auto be_swizzle_y_mask        = genMask(getMaskEnd(be_swizzle_x_mask), 2);
			static constexpr auto be_swizzle_z_mask        = genMask(getMaskEnd(be_swizzle_y_mask), 2);
			static constexpr auto be_swizzle_w_lo_mask     = genMask(getMaskEnd(be_swizzle_z_mask), 1);
			static constexpr auto be_swizzle_w_hi_mask     = genMask(24, 1);
			static constexpr auto be_neg_mask              = genMask(getMaskEnd(be_swizzle_w_hi_mask), 1);
			static constexpr auto be_abs_mask              = genMask(getMaskEnd(be_neg_mask), 1);
			static constexpr auto be_addr_reg_lo_mask      = genMask(getMaskEnd(be_abs_mask), 5);
			static constexpr auto be_addr_reg_hi_mask      = genMask(16, 6);
			static constexpr auto be_use_index_reg_mask    = genMask(getMaskEnd(be_addr_reg_hi_mask), 1);
			static constexpr auto be_perspective_corr_mask = genMask(getMaskEnd(be_use_index_reg_mask), 1);

			u32 hex;

			fp_register_type reg_type() const
			{
				return static_cast<fp_register_type>(fetchMaskedValue(hex, be_reg_type_mask));
			}
			u32 tmp_reg_index() const
			{
				return fetchMaskedValue(hex, be_tmp_reg_index_mask);
			}
			bool fp16() const
			{
				return fetchMaskedValue(hex, be_fp16_mask);
			}
			u32 swizzle_x() const
			{
				return fetchMaskedValue(hex, be_swizzle_x_mask);
			}
			u32 swizzle_y() const
			{
				return fetchMaskedValue(hex, be_swizzle_y_mask);
			}
			u32 swizzle_z() const
			{
				return fetchMaskedValue(hex, be_swizzle_z_mask);
			}
			u32 swizzle_w() const
			{
				return fetchHiLoMaskedValue(hex, be_swizzle_w_lo_mask, be_swizzle_w_hi_mask);
			}
			bool neg() const
			{
				return fetchMaskedValue(hex, be_neg_mask);
			}
			bool abs() const
			{
				return fetchMaskedValue(hex, be_abs_mask);
			}

			u32 addr_reg() const
			{
				return fetchHiLoMaskedValue(hex, be_addr_reg_lo_mask, be_addr_reg_hi_mask);
			}
			u32 use_index_reg() const
			{
				return fetchMaskedValue(hex, be_use_index_reg_mask);
			}
			u32 perspective_corr() const
			{
				return fetchMaskedValue(hex, be_perspective_corr_mask);
			}

			u32 end_offset() const
			{
				return std::bit_cast<be_t<u32>>(hex << 16 | hex >> 16) << 2;
			}
		};

		dest_t dest;
		src0_t src0;
		src1_t src1;
		src2_t src2;

		src_t src(std::size_t index)
		{
			switch (index)
			{
			case 0: return src_t{src0.hex};
			case 1: return src_t{src1.hex};
			case 2: return src_t{src2.hex};
			}

			std::abort();
		}

		bool src_abs(std::size_t index)
		{
			switch (index)
			{
			case 0: return src0.abs();
			case 1: return src1.abs();
			case 2: return src2.abs();
			}

			std::abort();
		}

		u32 src_precision_modifier(std::size_t index)
		{
			switch (index)
			{
			case 0: return src1.src0_prec_mod();
			case 1: return src1.src1_prec_mod();
			case 2: return src1.src2_prec_mod();
			}

			std::abort();
		}
	};
} // namespace rsx

extern std::string_view rsx_fp_input_attr_regs[15];
extern std::string_view rsx_fp_op_names[70];

struct RSXFragmentProgram
{
	struct data_storage_helper
	{
		void* data_ptr = nullptr;
		std::vector<char> local_storage;

		data_storage_helper() = default;

		data_storage_helper(void* ptr)
		{
			data_ptr = ptr;
			local_storage.clear();
		}

		data_storage_helper(const data_storage_helper& other)
		{
			this->operator=(other);
		}

		data_storage_helper(data_storage_helper&& other)
			: data_ptr(other.data_ptr), local_storage(std::move(other.local_storage))
		{
			other.data_ptr = nullptr;
		}

		data_storage_helper& operator=(const data_storage_helper& other)
		{
			if (this == &other)
				return *this;

			if (other.data_ptr == other.local_storage.data())
			{
				local_storage = other.local_storage;
				data_ptr      = local_storage.data();
			}
			else
			{
				data_ptr = other.data_ptr;
				local_storage.clear();
			}

			return *this;
		}

		data_storage_helper& operator=(data_storage_helper&& other)
		{
			if (this == &other)
				return *this;

			data_ptr       = other.data_ptr;
			local_storage  = std::move(other.local_storage);
			other.data_ptr = nullptr;

			return *this;
		}

		void deep_copy(u32 max_length)
		{
			if (local_storage.empty() && data_ptr)
			{
				local_storage.resize(max_length);
				std::memcpy(local_storage.data(), data_ptr, max_length);
				data_ptr = local_storage.data();
			}
		}
	};

	bool valid                = false;
	bool two_sided_lighting   = false;
	u32 offset                = 0;
	u32 ucode_length          = 0;
	u32 total_length          = 0;
	u32 ctrl                  = 0;
	u32 texcoord_control_mask = 0;

	mutable data_storage_helper data;
	rsx::fragment_program_texture_state texture_state;
	rsx::fragment_program_texture_config texture_params;

	rsx::texture_dimension_extended get_texture_dimension(u8 id) const
	{
		return rsx::texture_dimension_extended{static_cast<u8>((texture_state.texture_dimensions >> (id * 2)) & 0x3)};
	}

	bool texcoord_is_2d(u8 index) const
	{
		return !!(texcoord_control_mask & (1u << index));
	}

	bool texcoord_is_point_coord(u8 index) const
	{
		index += 16;
		return !!(texcoord_control_mask & (1u << index));
	}

	static RSXFragmentProgram clone(const RSXFragmentProgram& prog)
	{
		auto result = prog;
		result.clone_data();
		return result;
	}

	void* get_data() const
	{
		return data.data_ptr;
	}

	void clone_data() const
	{
		ensure(ucode_length);
		data.deep_copy(ucode_length);
	}
};

namespace rsx
{
	struct fragment_program_info
	{
		static constexpr auto inst_max_count        = 1024;
		static constexpr auto inst_size             = sizeof(u32) * 4;
		static constexpr auto constants_block_size  = sizeof(u64) * inst_size;
		static constexpr auto constants_block_count = inst_max_count / constants_block_size;

		struct function_info
		{
			static constexpr auto max_blocks_count = 16;
			u64 hash;
			u32 offset;
			u32 size;
			u32 texture_refs;
			u32 blocks_count;
			u32 blocks[max_blocks_count];
			u64 constants[constants_block_count];

			bool is_constant(u32 offset) const
			{
				auto block_index  = offset / constants_block_size;
				auto bit_in_block = (offset % constants_block_size) / inst_size;
				return block_index >= constants_block_count ? false : (constants[block_index] & bit_in_block) != 0;
			}

			void mark_constant(u32 offset)
			{
				auto block_index  = offset / constants_block_size;
				auto bit_in_block = (offset % constants_block_size) / inst_size;
				ensure(block_index < constants_block_count);
				constants[block_index] |= bit_in_block;
			}

			void add_block(u32 offset);
		};

		u32 function_count = 0;
		function_info functions[16]{};
	};

	void analyze_fragment_program(fragment_program_info* dst, const u32* ucode);
	void convert_fragment_program_to_spirv(std::vector<u32>& spirv, fragment_program_info* info, const u32* ucode);
} // namespace rsx
