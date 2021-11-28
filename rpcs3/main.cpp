#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "Emu/Cell/Modules/cellOskDialog.h"

#include "Utilities/sema.h"
#ifdef _WIN32
#include <windows.h>
#include "util/dyn_lib.hpp"

// TODO(cjj19970505@live.cn)
// When compiling with WIN32_LEAN_AND_MEAN definition
// NTSTATUS is defined in CMake build but not in VS build
// May be caused by some different header pre-inclusion between CMake and VS configurations.
#if !defined(NTSTATUS)
// Copied from ntdef.h
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#endif
DYNAMIC_IMPORT("ntdll.dll", NtQueryTimerResolution, NTSTATUS(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution));
DYNAMIC_IMPORT("ntdll.dll", NtSetTimerResolution, NTSTATUS(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution));
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <stdlib.h>
#endif

#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#endif

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

#include "Utilities/Config.h"
#include "Utilities/Thread.h"
#include "Utilities/File.h"
#include "Utilities/StrUtil.h"
#include "rpcs3_version.h"
#include "Emu/System.h"
#include "Emu/system_utils.hpp"
#include <thread>
#include <charconv>
#include <string_view>
#include "util/sysinfo.hpp"
#include "Emu/Io/pad_config.h"
#include "Utilities/cheat_info.h"
#include "Emu/system_config.h"
#include "Emu/Io/KeyboardHandler.h"
#include "Emu/Io/MouseHandler.h"
#include "Input/pad_thread.h"
#include "Emu/IdManager.h"
#include "Emu/Io/Null/NullKeyboardHandler.h"
#include "Emu/Io/Null/NullMouseHandler.h"
#include "Emu/Io/KeyboardHandler.h"
#include "Emu/Io/MouseHandler.h"
#include "Emu/Audio/AudioBackend.h"
#include "Emu/Audio/Null/NullAudioBackend.h"
#include "Emu/Audio/Cubeb/CubebBackend.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/RSX/Null/NullGSRender.h"
#include "Emu/RSX/VK/VKGSRender.h"
#ifdef _WIN32
#include "Emu/Audio/XAudio2/XAudio2Backend.h"
#endif
#ifdef HAVE_FAUDIO
#include "Emu/Audio/FAudio/FAudioBackend.h"
#endif
#include <condition_variable>
#include <mutex>
#include <queue>
#include <functional>

LOG_CHANNEL(sys_log, "SYS");

[[noreturn]] extern void report_fatal_error(std::string_view _text)
{
	std::string buf;

	// Check if thread id is in string
	if (_text.find("\nThread id = "sv) == std::string_view::npos)
	{
		// Copy only when needed
		buf = std::string(_text);

		// Always print thread id
		fmt::append(buf, "\nThread id = %s.", std::this_thread::get_id());
	}

	const std::string_view text = buf.empty() ? _text : buf;

#ifdef _WIN32
	if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
		[[maybe_unused]]
		const auto con_out = freopen("conout$", "w", stderr);
#endif
	std::fprintf(stderr, "RPCS3: %s\n", std::string(text).c_str());
	std::abort();
}

struct fatal_error_listener final : logs::listener
{
	~fatal_error_listener() override = default;

	void log(u64 /*stamp*/, const logs::message& msg, const std::string& prefix, const std::string& text) override
	{
		if (msg == logs::level::fatal)
		{
			std::string _msg = "RPCS3: ";

			if (!prefix.empty())
			{
				_msg += prefix;
				_msg += ": ";
			}

			if (msg->name && '\0' != *msg->name)
			{
				_msg += msg->name;
				_msg += ": ";
			}

			_msg += text;
			_msg += '\n';

#ifdef _WIN32
			// If launched from CMD
			if (AttachConsole(ATTACH_PARENT_PROCESS))
				[[maybe_unused]]
				const auto con_out = freopen("CONOUT$", "w", stderr);
#endif
			// Output to error stream as is
			std::fprintf(stderr, "%s", _msg.c_str());

#ifdef _WIN32
			if (IsDebuggerPresent())
			{
				// Output string to attached debugger
				OutputDebugStringA(_msg.c_str());
			}
#endif
			// Pause emulation if fatal error encountered
			Emu.Pause(true);
		}
	}
};

// static constexpr char arg_headless[]     = "headless";
// static constexpr char arg_no_gui[]       = "no-gui";
// static constexpr char arg_high_dpi[]     = "hidpi";
// static constexpr char arg_rounding[]     = "dpi-rounding";
// static constexpr char arg_styles[]       = "styles";
// static constexpr char arg_style[]        = "style";
// static constexpr char arg_stylesheet[]   = "stylesheet";
// static constexpr char arg_config[]       = "config";
// static constexpr char arg_q_debug[]      = "qDebug";
// static constexpr char arg_error[]        = "error";
static constexpr char arg_updating[] = "updating";
// static constexpr char arg_user_id[]      = "user-id";
// static constexpr char arg_installfw[]    = "installfw";
// static constexpr char arg_installpkg[]   = "installpkg";
// static constexpr char arg_commit_db[]    = "get-commit-db";
// static constexpr char arg_timer[]        = "high-res-timer";
// static constexpr char arg_verbose_curl[] = "verbose-curl";

int find_arg(std::string_view arg, int argc, const char* argv[])
{
	for (int i = 0; i < argc; ++i)
	{ // It's not guaranteed that argv 0 is the executable.
		std::string_view current_arg(argv[i]);
		if (!current_arg.starts_with("--"))
		{
			continue;
		}

		if (current_arg.substr(2) == arg)
		{
			return i;
		}
	}
	return -1;
}

// FIXME: WTF is this and why it defined in GUI lib?
cfg_profile g_cfg_profile;

// FIXME: following was defined in GUI lib, but referenced from emu
template <>
void fmt_class_string<cheat_type>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](cheat_type value)
		{
		switch (value)
		{
		case cheat_type::unsigned_8_cheat: return "Unsigned 8 bits";
		case cheat_type::unsigned_16_cheat: return "Unsigned 16 bits";
		case cheat_type::unsigned_32_cheat: return "Unsigned 32 bits";
		case cheat_type::unsigned_64_cheat: return "Unsigned 64 bits";
		case cheat_type::signed_8_cheat: return "Signed 8 bits";
		case cheat_type::signed_16_cheat: return "Signed 16 bits";
		case cheat_type::signed_32_cheat: return "Signed 32 bits";
		case cheat_type::signed_64_cheat: return "Signed 64 bits";
		case cheat_type::max: break;
		}

		return unknown; });
}

// FIXME: remove it
static EmuCallbacks CreateCallbacks()
{
	EmuCallbacks callbacks;

	callbacks.init_kb_handler = []()
	{
		switch (g_cfg.io.keyboard.get())
		{
		case keyboard_handler::null:
		{
			g_fxo->init<KeyboardHandlerBase, NullKeyboardHandler>();
			break;
		}
		case keyboard_handler::basic:
		{
			/*
			basic_keyboard_handler* ret = g_fxo->init<KeyboardHandlerBase, basic_keyboard_handler>();
			ret->moveToThread(get_thread());
			ret->SetTargetWindow(m_game_window);
			*/
			break;
		}
		}
	};

	callbacks.init_mouse_handler = []()
	{
		switch (g_cfg.io.mouse.get())
		{
		default:
		case mouse_handler::null:
		{
			g_fxo->init<MouseHandlerBase, NullMouseHandler>();
			break;
		}
		}
	};

	callbacks.init_pad_handler = [](std::string_view title_id)
	{
		// And why it want game window and thread? It happy with nulls
		g_fxo->init<named_thread<pad_thread>>(nullptr, nullptr, title_id);
	};

	callbacks.get_audio = []() -> std::shared_ptr<AudioBackend>
	{
		std::shared_ptr<AudioBackend> result;
		switch (g_cfg.audio.renderer.get())
		{
		case audio_renderer::null: result = std::make_shared<NullAudioBackend>(); break;
#ifdef _WIN32
		case audio_renderer::xaudio: result = std::make_shared<XAudio2Backend>(); break;
#endif
		case audio_renderer::cubeb: result = std::make_shared<CubebBackend>(); break;
#ifdef HAVE_FAUDIO
		case audio_renderer::faudio: result = std::make_shared<FAudioBackend>(); break;
#endif
		}

		if (result == nullptr || !result->Initialized())
		{
			// Fall back to a null backend if something went wrong
			if (result != nullptr)
			{
				sys_log.error("Audio renderer %s could not be initialized, using a Null renderer instead", g_cfg.audio.renderer.get());
			}
			result = std::make_shared<NullAudioBackend>();
		}
		return result;
	};

	callbacks.resolve_path = [](std::string_view sv)
	{
		// And why just GUI knows how to resolve paths?
		// return QFileInfo(QString::fromUtf8(sv.data(), static_cast<int>(sv.size()))).canonicalFilePath().toStdString();
		return std::string(sv);
	};

	return callbacks;
}

class GLFWFrame : public GSFrameBase
{
	GLFWwindow* mWindow;

public:
	GLFWFrame(const char* name, sizeu size)
	{
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		mWindow = glfwCreateWindow(size.width, size.height, name, nullptr, nullptr);
	}

	std::vector<const char*> required_instance_extensions() const override
	{
		const char** glfwExtensions;
		uint32_t glfwExtensionCount = 0;

		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
	}

	VkSurfaceKHR create_surface(VkInstance instance) override
	{
		VkSurfaceKHR surface;
		glfwCreateWindowSurface(instance, mWindow, nullptr, &surface);
		return surface;
	}

	sizeu client_size() const override
	{
		int width, height;
		glfwGetFramebufferSize(mWindow, &width, &height);

		return sizeu{static_cast<unsigned>(width), static_cast<unsigned>(height)};
	}

	double client_device_pixel_ratio() const override
	{
		int width, height;
		glfwGetWindowSize(mWindow, &width, &height);

		return static_cast<double>(width) / client_size().width;
	}

	GLFWwindow* handle() const
	{
		return mWindow;
	}
};

int main(int argc, const char* argv[])
{
	if (argc != 2)
	{
		std::fprintf(stderr, "USAGE: %s <path to game>\n", argv[0]);
		return 1;
	}

	const std::string lock_name = "RPCS3.buf";
	const std::string lock_path = fs::get_cache_dir() + lock_name;
	static fs::file instance_lock;

	// True if an argument --updating found
	const bool is_updating = find_arg(arg_updating, argc, argv) != -1;

	// Keep trying to lock the file for ~2s normally, and for ~10s in the case of --updating
	for (u32 num = 0; num < (is_updating ? 500u : 100u) && !instance_lock.open(lock_path, fs::rewrite + fs::lock); num++)
	{
		std::this_thread::sleep_for(20ms);
	}

	if (!instance_lock)
	{
		if (fs::g_tls_error == fs::error::acces)
		{
			if (fs::exists(lock_path))
			{
				report_fatal_error("Another instance of RPCS3 is running. Close it or kill its process, if necessary.");
			}

			report_fatal_error("Cannot create " + lock_name + " (access denied)."
#ifdef _WIN32
															  "\nNote that RPCS3 cannot be installed in Program Files or similar directories with limited permissions."
#else
															  "\nPlease, check RPCS3 permissions in '" +
							   fs::get_cache_dir() + "'."
#endif
			);
		}

		report_fatal_error(fmt::format("Cannot create %s (error %s)", lock_path, fs::g_tls_error));
	}

#ifdef _WIN32
	if (!SetProcessWorkingSetSize(GetCurrentProcess(), 0x80000000, 0xC0000000)) // 2-3 GiB
	{
		report_fatal_error("Not enough memory for RPCS3 process.");
	}
#endif

	ensure(thread_ctrl::is_main(), "Not main thread");

	// Initialize TSC freq (in case it isn't)
	static_cast<void>(utils::get_tsc_freq());

	// Initialize thread pool finalizer (on first use)
	static_cast<void>(named_thread("", [](int) {}));

	static std::unique_ptr<logs::listener> log_file;
	{
		// Check free space
		fs::device_stat stats{};
		if (!fs::statfs(fs::get_cache_dir(), stats) || stats.avail_free < 128 * 1024 * 1024)
		{
			report_fatal_error(fmt::format("Not enough free space (%f KB)", stats.avail_free / 1000000.));
		}

		// Limit log size to ~25% of free space
		log_file = logs::make_file_listener(fs::get_cache_dir() + "RPCS3.log", stats.avail_free / 4);
	}

	static std::unique_ptr<logs::listener> fatal_listener = std::make_unique<fatal_error_listener>();
	logs::listener::add(fatal_listener.get());

	{
		// Write RPCS3 version
		logs::stored_message ver{sys_log.always()};
		ver.text = fmt::format("RPCS3-DH v%s | %s", rpcs3::get_version().to_string(), rpcs3::get_branch());

		// Write System information
		logs::stored_message sys{sys_log.always()};
		sys.text = utils::get_system_info();

		// Write OS version
		logs::stored_message os{sys_log.always()};
		os.text = utils::get_OS_version();

		logs::set_init({std::move(ver), std::move(sys), std::move(os)});
	}

	std::string argument_str;
	for (int i = 0; i < argc; i++)
	{
		argument_str += "'" + std::string(argv[i]) + "'";
		if (i != argc - 1)
			argument_str += " ";
	}
	sys_log.notice("argc: %d, argv: %s", argc, argument_str);

	EmuCallbacks callbacks = CreateCallbacks();

	callbacks.try_to_quit = [](bool force_quit, std::function<void()> on_exit) -> bool
	{
		if (force_quit)
		{
			if (on_exit)
			{
				on_exit();
			}

			std::abort();
			return true;
		}

		return false;
	};

	std::condition_variable call_after_cv;
	std::queue<std::function<void()>> call_after_funcs;
	std::mutex call_after_mtx;

	callbacks.call_after = [&](std::function<void()> func)
	{
		std::scoped_lock lock(call_after_mtx);
		call_after_funcs.push(std::move(func));
		call_after_cv.notify_one();
	};

	glfwInit();
	GLFWFrame window("RPCS3-DH", {800, 600});

	callbacks.init_gs_render = [&window]()
	{
		switch (const video_renderer type = g_cfg.video.renderer)
		{
		case video_renderer::null:
			g_fxo->init<rsx::thread, named_thread<NullGSRender>>();
			break;
		case video_renderer::vulkan:
			g_fxo->init<rsx::thread, named_thread<VKGSRender>>(&window);
			break;
			// fmt::throw_exception("Headless mode can only be used with the %s video renderer. Current renderer: %s", video_renderer::null, type);
			//[[fallthrough]];
		default:
			fmt::throw_exception("Invalid video renderer: %s", type);
		}
	};

	callbacks.get_camera_handler = []() -> std::shared_ptr<camera_handler_base>
	{
		switch (g_cfg.io.camera.get())
		{
		case camera_handler::null:
		case camera_handler::fake:
			return std::make_shared<null_camera_handler>();
		case camera_handler::qt:
			fmt::throw_exception("Headless mode can not be used with this camera handler. Current handler: %s", g_cfg.io.camera.get());
		}
		return nullptr;
	};

	callbacks.get_msg_dialog = []() -> std::shared_ptr<MsgDialogBase>
	{
		return nullptr;
	};
	callbacks.get_osk_dialog = []() -> std::shared_ptr<OskDialogBase>
	{
		return nullptr;
	};
	callbacks.get_save_dialog = []() -> std::unique_ptr<SaveDialogBase>
	{
		return nullptr;
	};
	callbacks.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase>
	{
		return nullptr;
	};

	callbacks.on_run = [](bool /*start_playtime*/) {
	};
	callbacks.on_pause = []() {
	};
	callbacks.on_resume = []() {
	};
	callbacks.on_stop = []() {
	};
	callbacks.on_ready = []() {
	};

	callbacks.on_missing_fw = []()
	{
		return false;
	};

	callbacks.handle_taskbar_progress = [](s32, s32) {
	};

	callbacks.get_localized_string = [](localized_string_id, const char*) -> std::string
	{
		return {};
	};
	callbacks.get_localized_u32string = [](localized_string_id, const char*) -> std::u32string
	{
		return {};
	};

	callbacks.play_sound = [](const std::string&) {
	};

	Emu.SetCallbacks(std::move(callbacks));

	Emu.SetUsr("00000001");
	Emu.Init();

	{
		// Log Firmware Version after Emu was initialized
		const std::string firmware_version = utils::get_firmware_version();
		const std::string firmware_string  = firmware_version.empty() ? "Missing Firmware" : ("Firmware version: " + firmware_version);
		sys_log.always()("%s", firmware_string);
	}

	Emu.BootGame(argv[1]);

	std::function<void()> call_after;
	while (!glfwWindowShouldClose(window.handle()))
	{
		glfwPollEvents();

		{
			std::unique_lock lock(call_after_mtx);
			call_after_cv.wait_for(lock, std::chrono::milliseconds(20));

			if (call_after_funcs.empty())
			{
				continue;
			}

			call_after = std::move(call_after_funcs.back());
			call_after_funcs.pop();
		}

		std::exchange(call_after, nullptr)();
	}
	Emu.Stop();
	return 0;
}
