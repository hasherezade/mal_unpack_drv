#pragma once

#include <ntifs.h>
#include <ntddk.h>
#include <fltKernel.h>

#include "version.h"

typedef struct _active_settings {

	bool hasProcessNotify;
	bool hasThreadNotify;
	bool hasImageNotify;
	PVOID RegHandle;
	LARGE_INTEGER RegCookie;
	PFLT_FILTER gFilterHandle;

	void init()
	{
		hasProcessNotify = false;
		hasThreadNotify = false;
		hasImageNotify = false;
		RegHandle = NULL;
		RegCookie.QuadPart = 0;
		gFilterHandle = NULL;
	}
} active_settings;
