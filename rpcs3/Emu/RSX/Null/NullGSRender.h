#pragma once
#include "Emu/RSX/RSXThread.h"

class NullGSRender : public rsx::thread
{
public:
	u64 get_cycles() final;

	void on_init_thread() override {}
	void flip(const rsx::display_flip_info_t& ) override {}

private:
	void end() override;
};
