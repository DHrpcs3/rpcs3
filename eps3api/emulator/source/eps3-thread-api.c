#include "functions.h"
#include "eps3/enums.h"
#include "eps3/thread-api.h"

Eps3ErrorCode _eps3ThreadGetInfo(Eps3Thread thread, Eps3ThreadInfo *info)
{
	if (!info)
	{
		return eps3ErrorBadArgument;
	}

	return eps3ThreadGetInfoImpl(thread, info);
}

Eps3ErrorCode _eps3ThreadGetRegister(Eps3Thread thread, int type, int index, void **destRegister)
{
	if (type < 0 || index < 0)
	{
		return eps3ErrorInvalidValue;
	}

	if (!destRegister)
	{
		return eps3ErrorBadArgument;
	}

	return eps3ThreadGetRegisterImpl(thread, type, index, destRegister);
}