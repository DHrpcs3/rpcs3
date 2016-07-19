#include "rpcs3/pch.h"
#include "rpcs3/utils/dynamic_library.h"

#ifdef _WIN32
	#include <Windows.h>
#else
	#include <dlfcn.h>
#endif

namespace utils
{
	dynamic_library::dynamic_library(const std::string &path)
	{
		load(path);
	}

	dynamic_library::~dynamic_library()
	{
		close();
	}

	bool dynamic_library::load(const std::string &path)
	{
#ifdef _WIN32
		m_handle = LoadLibraryA(path.c_str());
#else
		m_handle = dlopen(path.c_str(), RTLD_LAZY);
#endif
		return loaded();
	}

	void dynamic_library::close()
	{
#ifdef _WIN32
		FreeLibrary((HMODULE)m_handle);
#else
		dlclose(m_handle);
#endif
		m_handle = nullptr;
	}

	void *dynamic_library::get_impl(const std::string &name) const
	{
#ifdef _WIN32
		return (void*)GetProcAddress((HMODULE)m_handle, name.c_str());
#else
		return dlsym(m_handle, (char *)name.c_str());
#endif
	}

	bool dynamic_library::loaded() const
	{
		return !m_handle;
	}

	dynamic_library::operator bool() const
	{
		return loaded();
	}
}