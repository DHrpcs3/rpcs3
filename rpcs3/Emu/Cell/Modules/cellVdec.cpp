#include "stdafx.h"
#include "Emu/System.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

#include "cellPamf.h"
#include "cellVdec.h"

#include <mutex>
#include <thread>
#include <queue>

std::mutex g_mutex_avcodec_open2;

logs::channel cellVdec("cellVdec", logs::level::notice);

vm::gvar<s32> _cell_vdec_prx_ver; // ???

enum class vdec_cmd : u32
{
	none = 0,

	start_seq,
	end_seq,
	decode,
	set_frc,
	close,
};

struct vdec_frame
{
	struct frame_dtor
	{
		void operator()(AVFrame* data) const
		{
			av_frame_unref(data);
			av_frame_free(&data);
		}
	};

	std::unique_ptr<AVFrame, frame_dtor> avf;
	u64 dts;
	u64 pts;
	u64 userdata;
	u32 frc;

	AVFrame* operator ->() const
	{
		return avf.get();
	}
};

struct vdec_thread : ppu_thread
{
	AVCodec* codec{};
	AVCodecContext* ctx{};

	const s32 type;
	const u32 profile;
	const u32 mem_addr;
	const u32 mem_size;
	const vm::ptr<CellVdecCbMsg> cb_func;
	const u32 cb_arg;
	u32 mem_bias{};

	u32 frc_set{}; // Frame Rate Override
	u64 last_pts{};
	u64 last_dts{};

	std::queue<vdec_frame> out;
	std::queue<u64> user_data; // TODO

	vdec_thread(s32 type, u32 profile, u32 addr, u32 size, vm::ptr<CellVdecCbMsg> func, u32 arg)
		: ppu_thread("HLE Video Decoder")
		, type(type)
		, profile(profile)
		, mem_addr(addr)
		, mem_size(size)
		, cb_func(func)
		, cb_arg(arg)
	{
		avcodec_register_all();

		switch (type)
		{
		case CELL_VDEC_CODEC_TYPE_MPEG2:
		{
			codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
			break;
		}
		case CELL_VDEC_CODEC_TYPE_AVC:
		{
			codec = avcodec_find_decoder(AV_CODEC_ID_H264);
			break;
		}
		case CELL_VDEC_CODEC_TYPE_DIVX:
		{
			codec = avcodec_find_decoder(AV_CODEC_ID_MPEG4);
			break;
		}
		default:
		{
			throw fmt::exception("Unknown video decoder type (0x%x)" HERE, type);
		}
		}

		if (!codec)
		{
			throw fmt::exception("avcodec_find_decoder() failed (type=0x%x)" HERE, type);
		}

		ctx = avcodec_alloc_context3(codec);

		if (!ctx)
		{
			throw fmt::exception("avcodec_alloc_context3() failed (type=0x%x)" HERE, type);
		}

		AVDictionary* opts{};
		av_dict_set(&opts, "refcounted_frames", "1", 0);

		std::lock_guard<std::mutex> lock(g_mutex_avcodec_open2);

		int err = avcodec_open2(ctx, codec, &opts);
		if (err || opts)
		{
			avcodec_free_context(&ctx);
			throw fmt::exception("avcodec_open2() failed (err=0x%x, opts=%d)" HERE, err, opts ? 1 : 0);
		}
	}

	virtual ~vdec_thread() override
	{
		avcodec_close(ctx);
		avcodec_free_context(&ctx);
	}

	virtual std::string dump() const override
	{
		// TODO
		return ppu_thread::dump();
	}

	virtual void cpu_task() override
	{
		while (ppu_cmd cmd = cmd_wait())
		{
			switch (vdec_cmd vcmd = cmd.arg1<vdec_cmd>())
			{
			case vdec_cmd::start_seq:
			{
				cmd_pop();
				avcodec_flush_buffers(ctx);

				frc_set = 0; // TODO: ???
				last_pts = 0;
				last_dts = 0;
				cellVdec.trace("Start sequence...");
				break;
			}

			case vdec_cmd::decode:
			case vdec_cmd::end_seq:
			{
				AVPacket packet{};
				packet.pos = -1;

				u32 au_type{};
				u32 au_addr{};
				u32 au_size{};
				u64 au_pts{};
				u64 au_dts{};
				u64 au_usrd{};
				u64 au_spec{};

				if (vcmd == vdec_cmd::decode)
				{
					const u32 pos = cmd_queue.peek();
					au_type = cmd.arg2<u32>();  // TODO
					au_addr = cmd_queue[pos + 1].load().arg1<u32>();
					au_size = cmd_queue[pos + 1].load().arg2<u32>();
					au_pts = cmd_queue[pos + 2].load().as<u64>();
					au_dts = cmd_queue[pos + 3].load().as<u64>();
					au_usrd = cmd_queue[pos + 4].load().as<u64>(); // TODO
					au_spec = cmd_queue[pos + 5].load().as<u64>(); // TODO
					cmd_pop(5);

					packet.data = vm::_ptr<u8>(au_addr);
					packet.size = au_size;
					packet.pts = au_pts != -1 ? au_pts : AV_NOPTS_VALUE;
					packet.dts = au_dts != -1 ? au_dts : AV_NOPTS_VALUE;
					cellVdec.trace("AU decoding: size=0x%x, pts=0x%llx, dts=0x%llx, userdata=0x%llx", au_size, au_pts, au_dts, au_usrd);
				}
				else
				{
					cmd_pop();

					packet.pts = AV_NOPTS_VALUE;
					packet.dts = AV_NOPTS_VALUE;
					cellVdec.trace("End sequence...");
				}

				while (true)
				{
					vdec_frame frame;
					frame.avf.reset(av_frame_alloc());

					if (!frame.avf)
					{
						throw fmt::exception("av_frame_alloc() failed" HERE);
					}

					int got_picture = 0;

					int decode = avcodec_decode_video2(ctx, frame.avf.get(), &got_picture, &packet);

					if (decode < 0)
					{
						throw fmt::exception("vdecDecodeAu: AU decoding error(0x%x)" HERE, decode);
					}

					if (got_picture == 0)
					{
						break;
					}

					if (decode != packet.size)
					{
						cellVdec.error("vdecDecodeAu: incorrect AU size (0x%x, decoded 0x%x)", packet.size, decode);
					}

					if (got_picture)
					{
						if (frame->interlaced_frame)
						{
							throw fmt::exception("Interlaced frames not supported (0x%x)", frame->interlaced_frame);
						}

						if (frame->repeat_pict)
						{
							throw fmt::exception("Repeated frames not supported (0x%x)", frame->repeat_pict);
						}

						if (frc_set)
						{
							u64 amend = 0;

							switch (frc_set)
							{
							case CELL_VDEC_FRC_24000DIV1001: amend = 1001 * 90000 / 24000; break;
							case CELL_VDEC_FRC_24: amend = 90000 / 24; break;
							case CELL_VDEC_FRC_25: amend = 90000 / 25; break;
							case CELL_VDEC_FRC_30000DIV1001: amend = 1001 * 90000 / 30000; break;
							case CELL_VDEC_FRC_30: amend = 90000 / 30; break;
							case CELL_VDEC_FRC_50: amend = 90000 / 50; break;
							case CELL_VDEC_FRC_60000DIV1001: amend = 1001 * 90000 / 60000; break;
							case CELL_VDEC_FRC_60: amend = 90000 / 60; break;
							default:
							{
								throw EXCEPTION("Invalid frame rate code set (0x%x)", frc_set);
							}
							}

							last_pts += amend;
							last_dts += amend;
							frame.frc = frc_set;
						}
						else
						{
							const u64 amend = ctx->time_base.num * 90000 * ctx->ticks_per_frame / ctx->time_base.den;
							last_pts += amend;
							last_dts += amend;

							if (ctx->time_base.num == 1)
							{
								switch ((u64)ctx->time_base.den + (u64)(ctx->ticks_per_frame - 1) * 0x100000000ull)
								{
								case 24: case 0x100000000ull + 48: frame.frc = CELL_VDEC_FRC_24; break;
								case 25: case 0x100000000ull + 50: frame.frc = CELL_VDEC_FRC_25; break;
								case 30: case 0x100000000ull + 60: frame.frc = CELL_VDEC_FRC_30; break;
								case 50: case 0x100000000ull + 100: frame.frc = CELL_VDEC_FRC_50; break;
								case 60: case 0x100000000ull + 120: frame.frc = CELL_VDEC_FRC_60; break;
								default:
								{
									throw EXCEPTION("Unsupported time_base.den (%d/1, tpf=%d)", ctx->time_base.den, ctx->ticks_per_frame);
								}
								}
							}
							else if (ctx->time_base.num == 1001)
							{
								if (ctx->time_base.den / ctx->ticks_per_frame == 24000)
								{
									frame.frc = CELL_VDEC_FRC_24000DIV1001;
								}
								else if (ctx->time_base.den / ctx->ticks_per_frame == 30000)
								{
									frame.frc = CELL_VDEC_FRC_30000DIV1001;
								}
								else if (ctx->time_base.den / ctx->ticks_per_frame == 60000)
								{
									frame.frc = CELL_VDEC_FRC_60000DIV1001;
								}
								else
								{
									throw EXCEPTION("Unsupported time_base.den (%d/1001, tpf=%d)", ctx->time_base.den, ctx->ticks_per_frame);
								}
							}
							else
							{
								throw EXCEPTION("Unsupported time_base.num (%d)", ctx->time_base.num);
							}
						}

						frame.pts = last_pts = frame->pkt_pts != AV_NOPTS_VALUE ? frame->pkt_pts : last_pts;
						frame.dts = last_dts = frame->pkt_dts != AV_NOPTS_VALUE ? frame->pkt_dts : last_dts;
						frame.userdata = au_usrd;

						cellVdec.trace("got picture (pts=0x%llx, dts=0x%llx)", frame.pts, frame.dts);

						thread_lock{*this}, out.push(std::move(frame));

						cb_func(*this, id, CELL_VDEC_MSG_TYPE_PICOUT, CELL_OK, cb_arg);
					}

					if (vcmd == vdec_cmd::decode)
					{
						break;
					}
				}

				cb_func(*this, id, vcmd == vdec_cmd::decode ? CELL_VDEC_MSG_TYPE_AUDONE : CELL_VDEC_MSG_TYPE_SEQDONE, CELL_OK, cb_arg);
				break;
			}

			case vdec_cmd::set_frc:
			{
				cmd_pop();
				frc_set = cmd.arg2<u32>();
				break;
			}

			case vdec_cmd::close:
			{
				cmd_pop();
				state += cpu_state::exit;
				return;
			}

			default:
			{
				throw fmt::exception("Unknown command (0x%x)" HERE, vcmd);
			}
			}
		}
	}
};

u32 vdecQueryAttr(s32 type, u32 profile, u32 spec_addr /* may be 0 */, vm::ptr<CellVdecAttr> attr)
{
	switch (type) // TODO: check profile levels
	{
	case CELL_VDEC_CODEC_TYPE_AVC: cellVdec.warning("cellVdecQueryAttr: AVC (profile=%d)", profile); break;
	case CELL_VDEC_CODEC_TYPE_MPEG2: cellVdec.warning("cellVdecQueryAttr: MPEG2 (profile=%d)", profile); break;
	case CELL_VDEC_CODEC_TYPE_DIVX: cellVdec.warning("cellVdecQueryAttr: DivX (profile=%d)", profile); break;
	default: return CELL_VDEC_ERROR_ARG;
	}

	// TODO: check values
	attr->decoderVerLower = 0x280000; // from dmux
	attr->decoderVerUpper = 0x260000;
	attr->memSize = 4 * 1024 * 1024; // 4 MB
	attr->cmdDepth = 16;
	return CELL_OK;
}

s32 cellVdecQueryAttr(vm::cptr<CellVdecType> type, vm::ptr<CellVdecAttr> attr)
{
	cellVdec.warning("cellVdecQueryAttr(type=*0x%x, attr=*0x%x)", type, attr);

	return vdecQueryAttr(type->codecType, type->profileLevel, 0, attr);
}

s32 cellVdecQueryAttrEx(vm::cptr<CellVdecTypeEx> type, vm::ptr<CellVdecAttr> attr)
{
	cellVdec.warning("cellVdecQueryAttrEx(type=*0x%x, attr=*0x%x)", type, attr);

	return vdecQueryAttr(type->codecType, type->profileLevel, type->codecSpecificInfo_addr, attr);
}

s32 cellVdecOpen(vm::cptr<CellVdecType> type, vm::cptr<CellVdecResource> res, vm::cptr<CellVdecCb> cb, vm::ptr<u32> handle)
{
	cellVdec.warning("cellVdecOpen(type=*0x%x, res=*0x%x, cb=*0x%x, handle=*0x%x)", type, res, cb, handle);

	// Create decoder thread
	auto&& vdec = std::make_shared<vdec_thread>(type->codecType, type->profileLevel, res->memAddr, res->memSize, cb->cbFunc, cb->cbArg);

	// Hack: store thread id (normally it should be pointer)
	*handle = idm::import_existing<ppu_thread>(vdec);

	vdec->run();

	return CELL_OK;
}

s32 cellVdecOpenEx(vm::cptr<CellVdecTypeEx> type, vm::cptr<CellVdecResourceEx> res, vm::cptr<CellVdecCb> cb, vm::ptr<u32> handle)
{
	cellVdec.warning("cellVdecOpenEx(type=*0x%x, res=*0x%x, cb=*0x%x, handle=*0x%x)", type, res, cb, handle);

	// Create decoder thread
	auto&& vdec = std::make_shared<vdec_thread>(type->codecType, type->profileLevel, res->memAddr, res->memSize, cb->cbFunc, cb->cbArg);

	// Hack: store thread id (normally it should be pointer)
	*handle = idm::import_existing<ppu_thread>(vdec);

	vdec->run();

	return CELL_OK;
}

s32 cellVdecClose(u32 handle)
{
	cellVdec.warning("cellVdecClose(handle=0x%x)", handle);

	const auto vdec = std::dynamic_pointer_cast<vdec_thread>(idm::get<ppu_thread>(handle)); // TODO: avoid RTTI

	if (!vdec)
	{
		return CELL_VDEC_ERROR_ARG;
	}

	vdec->cmd_push({vdec_cmd::close, 0});
	vdec->lock_notify();
	vdec->join();
	idm::remove<ppu_thread>(handle);
	return CELL_OK;
}

s32 cellVdecStartSeq(u32 handle)
{
	cellVdec.trace("cellVdecStartSeq(handle=0x%x)", handle);

	const auto vdec = std::dynamic_pointer_cast<vdec_thread>(idm::get<ppu_thread>(handle)); // TODO: avoid RTTI

	if (!vdec)
	{
		return CELL_VDEC_ERROR_ARG;
	}

	vdec->cmd_push({vdec_cmd::start_seq, 0});
	vdec->lock_notify();
	return CELL_OK;
}

s32 cellVdecEndSeq(u32 handle)
{
	cellVdec.warning("cellVdecEndSeq(handle=0x%x)", handle);

	const auto vdec = std::dynamic_pointer_cast<vdec_thread>(idm::get<ppu_thread>(handle)); // TODO: avoid RTTI

	if (!vdec)
	{
		return CELL_VDEC_ERROR_ARG;
	}

	vdec->cmd_push({vdec_cmd::end_seq, 0});
	vdec->lock_notify();
	return CELL_OK;
}

s32 cellVdecDecodeAu(u32 handle, CellVdecDecodeMode mode, vm::cptr<CellVdecAuInfo> auInfo)
{
	cellVdec.trace("cellVdecDecodeAu(handle=0x%x, mode=%d, auInfo=*0x%x)", handle, mode, auInfo);

	const auto vdec = std::dynamic_pointer_cast<vdec_thread>(idm::get<ppu_thread>(handle)); // TODO: avoid RTTI

	if (mode > CELL_VDEC_DEC_MODE_PB_SKIP || !vdec)
	{
		return CELL_VDEC_ERROR_ARG;
	}

	if (mode != CELL_VDEC_DEC_MODE_NORMAL)
	{
		throw EXCEPTION("Unsupported decoding mode (%d)", mode);
	}

	// TODO: check info
	vdec->cmd_list
	({
		{ vdec_cmd::decode, mode },
		{ auInfo->startAddr, auInfo->size },
		u64{auInfo->pts.upper} << 32 | auInfo->pts.lower,
		u64{auInfo->dts.upper} << 32 | auInfo->dts.lower,
		auInfo->userData,
		auInfo->codecSpecificData,
	});

	vdec->lock_notify();
	return CELL_OK;
}

s32 cellVdecGetPicture(u32 handle, vm::cptr<CellVdecPicFormat> format, vm::ptr<u8> outBuff)
{
	cellVdec.trace("cellVdecGetPicture(handle=0x%x, format=*0x%x, outBuff=*0x%x)", handle, format, outBuff);

	const auto vdec = std::dynamic_pointer_cast<vdec_thread>(idm::get<ppu_thread>(handle)); // TODO: avoid RTTI

	if (!format || !vdec)
	{
		return CELL_VDEC_ERROR_ARG;
	}

	vdec_frame frame;
	{
		thread_lock lock(*vdec);

		if (vdec->out.empty())
		{
			return CELL_VDEC_ERROR_EMPTY;
		}

		frame = std::move(vdec->out.front());

		vdec->out.pop();
	}

	vdec->notify();
	
	if (outBuff)
	{
		const int w = frame->width;
		const int h = frame->height;

		AVPixelFormat out_f = AV_PIX_FMT_YUV420P;

		std::unique_ptr<u8[]> alpha_plane;

		switch (const u32 type = format->formatType)
		{
		case CELL_VDEC_PICFMT_ARGB32_ILV: out_f = AV_PIX_FMT_ARGB; alpha_plane.reset(new u8[w * h]); break;
		case CELL_VDEC_PICFMT_RGBA32_ILV: out_f = AV_PIX_FMT_RGBA; alpha_plane.reset(new u8[w * h]); break;
		case CELL_VDEC_PICFMT_UYVY422_ILV: out_f = AV_PIX_FMT_UYVY422; break;
		case CELL_VDEC_PICFMT_YUV420_PLANAR: out_f = AV_PIX_FMT_YUV420P; break;

		default:
		{
			throw fmt::exception("Unknown formatType (%d)" HERE, type);
		}
		}

		if (format->colorMatrixType != CELL_VDEC_COLOR_MATRIX_TYPE_BT709)
		{
			throw fmt::exception("Unknown colorMatrixType (%d)" HERE, format->colorMatrixType);
		}

		if (alpha_plane)
		{
			std::memset(alpha_plane.get(), format->alpha, w * h);
		}

		AVPixelFormat in_f = AV_PIX_FMT_YUV420P;

		switch (frame->format)
		{
		case AV_PIX_FMT_YUV420P: in_f = alpha_plane ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P; break;

		default:
		{
			throw fmt::exception("Unknown format (%d)" HERE, frame->format);
		}
		}

		std::unique_ptr<SwsContext, void(*)(SwsContext*)> sws(sws_getContext(w, h, in_f, w, h, out_f, SWS_POINT, NULL, NULL, NULL), sws_freeContext);

		u8* in_data[4] = { frame->data[0], frame->data[1], frame->data[2], alpha_plane.get() };
		int in_line[4] = { frame->linesize[0], frame->linesize[1], frame->linesize[2], w * 1 };
		u8* out_data[4] = { outBuff.get_ptr() };
		int out_line[4] = { w * 4 };

		if (!alpha_plane)
		{
			out_data[1] = out_data[0] + w * h;
			out_data[2] = out_data[0] + w * h * 5 / 4;
			out_line[0] = w;
			out_line[1] = w / 2;
			out_line[2] = w / 2;
		}

		sws_scale(sws.get(), in_data, in_line, 0, h, out_data, out_line);

		//const u32 buf_size = align(av_image_get_buffer_size(vdec->ctx->pix_fmt, vdec->ctx->width, vdec->ctx->height, 1), 128);

		//// TODO: zero padding bytes

		//int err = av_image_copy_to_buffer(outBuff.get_ptr(), buf_size, frame->data, frame->linesize, vdec->ctx->pix_fmt, frame->width, frame->height, 1);
		//if (err < 0)
		//{
		//	cellVdec.Fatal("cellVdecGetPicture: av_image_copy_to_buffer failed (err=0x%x)", err);
		//}
	}

	return CELL_OK;
}

s32 cellVdecGetPictureExt(u32 handle, vm::cptr<CellVdecPicFormat2> format2, vm::ptr<u8> outBuff, u32 arg4)
{
	cellVdec.warning("cellVdecGetPictureExt(handle=0x%x, format2=*0x%x, outBuff=*0x%x, arg4=*0x%x)", handle, format2, outBuff, arg4);

	if (arg4 || format2->unk0 || format2->unk1)
	{
		throw EXCEPTION("Unknown arguments (arg4=*0x%x, unk0=0x%x, unk1=0x%x)", arg4, format2->unk0, format2->unk1);
	}

	vm::var<CellVdecPicFormat> format;
	format->formatType = format2->formatType;
	format->colorMatrixType = format2->colorMatrixType;
	format->alpha = format2->alpha;

	return cellVdecGetPicture(handle, format, outBuff);
}

s32 cellVdecGetPicItem(u32 handle, vm::pptr<CellVdecPicItem> picItem)
{
	cellVdec.trace("cellVdecGetPicItem(handle=0x%x, picItem=**0x%x)", handle, picItem);

	const auto vdec = std::dynamic_pointer_cast<vdec_thread>(idm::get<ppu_thread>(handle)); // TODO: avoid RTTI

	if (!vdec)
	{
		return CELL_VDEC_ERROR_ARG;
	}

	AVFrame* frame{};
	u64 pts;
	u64 dts;
	u64 usrd;
	u32 frc;
	{
		thread_lock lock(*vdec);

		if (vdec->out.empty())
		{
			return CELL_VDEC_ERROR_EMPTY;
		}

		frame = vdec->out.front().avf.get();
		pts = vdec->out.front().pts;
		dts = vdec->out.front().dts;
		usrd = vdec->out.front().userdata;
		frc = vdec->out.front().frc;
	}

	const vm::ptr<CellVdecPicItem> info = vm::cast(vdec->mem_addr + vdec->mem_bias);

	vdec->mem_bias += 512;
	if (vdec->mem_bias + 512 > vdec->mem_size)
	{
		vdec->mem_bias = 0;
	}

	info->codecType = vdec->type;
	info->startAddr = 0x00000123; // invalid value (no address for picture)
	info->size = align(av_image_get_buffer_size(vdec->ctx->pix_fmt, vdec->ctx->width, vdec->ctx->height, 1), 128);
	info->auNum = 1;
	info->auPts[0].lower = (u32)(pts);
	info->auPts[0].upper = (u32)(pts >> 32);
	info->auPts[1].lower = (u32)CODEC_TS_INVALID;
	info->auPts[1].upper = (u32)CODEC_TS_INVALID;
	info->auDts[0].lower = (u32)(dts);
	info->auDts[0].upper = (u32)(dts >> 32);
	info->auDts[1].lower = (u32)CODEC_TS_INVALID;
	info->auDts[1].upper = (u32)CODEC_TS_INVALID;
	info->auUserData[0] = usrd;
	info->auUserData[1] = 0;
	info->status = CELL_OK;
	info->attr = CELL_VDEC_PICITEM_ATTR_NORMAL;
	info->picInfo_addr = info.addr() + SIZE_32(CellVdecPicItem);

	if (vdec->type == CELL_VDEC_CODEC_TYPE_AVC)
	{
		const vm::ptr<CellVdecAvcInfo> avc = vm::cast(info.addr() + SIZE_32(CellVdecPicItem));

		avc->horizontalSize = frame->width;
		avc->verticalSize = frame->height;

		switch (frame->pict_type)
		{
		case AV_PICTURE_TYPE_I: avc->pictureType[0] = CELL_VDEC_AVC_PCT_I; break;
		case AV_PICTURE_TYPE_P: avc->pictureType[0] = CELL_VDEC_AVC_PCT_P; break;
		case AV_PICTURE_TYPE_B: avc->pictureType[0] = CELL_VDEC_AVC_PCT_B; break;
		default: cellVdec.error("cellVdecGetPicItem(AVC): unknown pict_type value (0x%x)", frame->pict_type);
		}

		avc->pictureType[1] = CELL_VDEC_AVC_PCT_UNKNOWN; // ???
		avc->idrPictureFlag = false; // ???
		avc->aspect_ratio_idc = CELL_VDEC_AVC_ARI_SAR_UNSPECIFIED; // ???
		avc->sar_height = 0;
		avc->sar_width = 0;
		avc->pic_struct = CELL_VDEC_AVC_PSTR_FRAME; // ???
		avc->picOrderCount[0] = 0; // ???
		avc->picOrderCount[1] = 0;
		avc->vui_parameters_present_flag = true; // ???
		avc->frame_mbs_only_flag = true; // ??? progressive
		avc->video_signal_type_present_flag = true; // ???
		avc->video_format = CELL_VDEC_AVC_VF_COMPONENT; // ???
		avc->video_full_range_flag = false; // ???
		avc->colour_description_present_flag = true;
		avc->colour_primaries = CELL_VDEC_AVC_CP_ITU_R_BT_709_5; // ???
		avc->transfer_characteristics = CELL_VDEC_AVC_TC_ITU_R_BT_709_5;
		avc->matrix_coefficients = CELL_VDEC_AVC_MXC_ITU_R_BT_709_5; // important
		avc->timing_info_present_flag = true;
		
		switch (frc)
		{
		case CELL_VDEC_FRC_24000DIV1001: avc->frameRateCode = CELL_VDEC_AVC_FRC_24000DIV1001; break;
		case CELL_VDEC_FRC_24: avc->frameRateCode = CELL_VDEC_AVC_FRC_24; break;
		case CELL_VDEC_FRC_25: avc->frameRateCode = CELL_VDEC_AVC_FRC_25; break;
		case CELL_VDEC_FRC_30000DIV1001: avc->frameRateCode = CELL_VDEC_AVC_FRC_30000DIV1001; break;
		case CELL_VDEC_FRC_30: avc->frameRateCode = CELL_VDEC_AVC_FRC_30; break;
		case CELL_VDEC_FRC_50: avc->frameRateCode = CELL_VDEC_AVC_FRC_50; break;
		case CELL_VDEC_FRC_60000DIV1001: avc->frameRateCode = CELL_VDEC_AVC_FRC_60000DIV1001; break;
		case CELL_VDEC_FRC_60: avc->frameRateCode = CELL_VDEC_AVC_FRC_60; break;
		default: cellVdec.error("cellVdecGetPicItem(AVC): unknown frc value (0x%x)", frc);
		}

		avc->fixed_frame_rate_flag = true;
		avc->low_delay_hrd_flag = true; // ???
		avc->entropy_coding_mode_flag = true; // ???
		avc->nalUnitPresentFlags = 0; // ???
		avc->ccDataLength[0] = 0;
		avc->ccDataLength[1] = 0;
		avc->reserved[0] = 0;
		avc->reserved[1] = 0;
	}
	else if (vdec->type == CELL_VDEC_CODEC_TYPE_DIVX)
	{
		const vm::ptr<CellVdecDivxInfo> dvx = vm::cast(info.addr() + SIZE_32(CellVdecPicItem));

		switch (frame->pict_type)
		{
		case AV_PICTURE_TYPE_I: dvx->pictureType = CELL_VDEC_DIVX_VCT_I; break;
		case AV_PICTURE_TYPE_P: dvx->pictureType = CELL_VDEC_DIVX_VCT_P; break;
		case AV_PICTURE_TYPE_B: dvx->pictureType = CELL_VDEC_DIVX_VCT_B; break;
		default: cellVdec.error("cellVdecGetPicItem(DivX): unknown pict_type value (0x%x)", frame->pict_type);
		}

		dvx->horizontalSize = frame->width;
		dvx->verticalSize = frame->height;
		dvx->pixelAspectRatio = CELL_VDEC_DIVX_ARI_PAR_1_1; // ???
		dvx->parHeight = 0;
		dvx->parWidth = 0;
		dvx->colourDescription = false; // ???
		dvx->colourPrimaries = CELL_VDEC_DIVX_CP_ITU_R_BT_709; // ???
		dvx->transferCharacteristics = CELL_VDEC_DIVX_TC_ITU_R_BT_709; // ???
		dvx->matrixCoefficients = CELL_VDEC_DIVX_MXC_ITU_R_BT_709; // ???
		dvx->pictureStruct = CELL_VDEC_DIVX_PSTR_FRAME; // ???

		switch (frc)
		{
		case CELL_VDEC_FRC_24000DIV1001: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_24000DIV1001; break;
		case CELL_VDEC_FRC_24: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_24; break;
		case CELL_VDEC_FRC_25: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_25; break;
		case CELL_VDEC_FRC_30000DIV1001: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_30000DIV1001; break;
		case CELL_VDEC_FRC_30: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_30; break;
		case CELL_VDEC_FRC_50: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_50; break;
		case CELL_VDEC_FRC_60000DIV1001: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_60000DIV1001; break;
		case CELL_VDEC_FRC_60: dvx->frameRateCode = CELL_VDEC_DIVX_FRC_60; break;
		default: cellVdec.error("cellVdecGetPicItem(DivX): unknown frc value (0x%x)", frc);
		}
	}
	else if (vdec->type == CELL_VDEC_CODEC_TYPE_MPEG2)
	{
		const vm::ptr<CellVdecMpeg2Info> mp2 = vm::cast(info.addr() + SIZE_32(CellVdecPicItem));

		std::memset(mp2.get_ptr(), 0, sizeof(CellVdecMpeg2Info));
		mp2->horizontal_size = frame->width;
		mp2->vertical_size = frame->height;
		mp2->aspect_ratio_information = CELL_VDEC_MPEG2_ARI_SAR_1_1; // ???
		
		switch (frc)
		{
		case CELL_VDEC_FRC_24000DIV1001: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_24000DIV1001; break;
		case CELL_VDEC_FRC_24: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_24; break;
		case CELL_VDEC_FRC_25: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_25; break;
		case CELL_VDEC_FRC_30000DIV1001: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_30000DIV1001; break;
		case CELL_VDEC_FRC_30: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_30; break;
		case CELL_VDEC_FRC_50: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_50; break;
		case CELL_VDEC_FRC_60000DIV1001: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_60000DIV1001; break;
		case CELL_VDEC_FRC_60: mp2->frame_rate_code = CELL_VDEC_MPEG2_FRC_60; break;
		default: cellVdec.error("cellVdecGetPicItem(MPEG2): unknown frc value (0x%x)", frc);
		}

		mp2->progressive_sequence = true; // ???
		mp2->low_delay = true; // ???
		mp2->video_format = CELL_VDEC_MPEG2_VF_UNSPECIFIED; // ???
		mp2->colour_description = false; // ???

		switch (frame->pict_type)
		{
		case AV_PICTURE_TYPE_I: mp2->picture_coding_type[0] = CELL_VDEC_MPEG2_PCT_I; break;
		case AV_PICTURE_TYPE_P: mp2->picture_coding_type[0] = CELL_VDEC_MPEG2_PCT_P; break;
		case AV_PICTURE_TYPE_B: mp2->picture_coding_type[0] = CELL_VDEC_MPEG2_PCT_B; break;
		default: cellVdec.error("cellVdecGetPicItem(MPEG2): unknown pict_type value (0x%x)", frame->pict_type);
		}

		mp2->picture_coding_type[1] = CELL_VDEC_MPEG2_PCT_FORBIDDEN; // ???
		mp2->picture_structure[0] = CELL_VDEC_MPEG2_PSTR_FRAME;
		mp2->picture_structure[1] = CELL_VDEC_MPEG2_PSTR_FRAME;

		// ...
	}

	*picItem = info;
	return CELL_OK;
}

s32 cellVdecSetFrameRate(u32 handle, CellVdecFrameRate frc)
{
	cellVdec.trace("cellVdecSetFrameRate(handle=0x%x, frc=0x%x)", handle, frc);

	const auto vdec = std::dynamic_pointer_cast<vdec_thread>(idm::get<ppu_thread>(handle)); // TODO: avoid RTTI

	if (!vdec)
	{
		return CELL_VDEC_ERROR_ARG;
	}

	// TODO: check frc value
	vdec->cmd_push({vdec_cmd::set_frc, frc});
	vdec->lock_notify();
	return CELL_OK;
}

DECLARE(ppu_module_manager::cellVdec)("libvdec", []()
{
	REG_VAR(libvdec, _cell_vdec_prx_ver); // 0x085a7ecb

	REG_FUNC(libvdec, cellVdecQueryAttr);
	REG_FUNC(libvdec, cellVdecQueryAttrEx);
	REG_FUNC(libvdec, cellVdecOpen);
	REG_FUNC(libvdec, cellVdecOpenEx);
	//REG_FUNC(libvdec, cellVdecOpenExt); // 0xef4d8ad7
	REG_FUNC(libvdec, cellVdecClose);
	REG_FUNC(libvdec, cellVdecStartSeq);
	//REG_FUNC(libvdec, cellVdecStartSeqExt); // 0xebb8e70a
	REG_FUNC(libvdec, cellVdecEndSeq);
	REG_FUNC(libvdec, cellVdecDecodeAu);
	REG_FUNC(libvdec, cellVdecGetPicture);
	REG_FUNC(libvdec, cellVdecGetPictureExt); // 0xa21aa896
	REG_FUNC(libvdec, cellVdecGetPicItem);
	//REG_FUNC(libvdec, cellVdecGetPicItemExt); // 0x2cbd9806
	REG_FUNC(libvdec, cellVdecSetFrameRate);
	//REG_FUNC(libvdec, cellVdecSetFrameRateExt); // 0xcffc42a5
});