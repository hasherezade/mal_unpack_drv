#pragma once

#include <fltKernel.h>
#include "main.h"

#include "common.h"
#include "data_manager.h"

extern active_settings g_Settings;


struct FileContext
{
	LONGLONG fileId;
};

CONST FLT_CONTEXT_REGISTRATION ContextRegistration[] = {
	{ FLT_FILE_CONTEXT, 0, nullptr, sizeof(FileContext), DRIVER_TAG, nullptr, nullptr, nullptr },
	{ FLT_CONTEXT_END }
};

////
FLT_PREOP_CALLBACK_STATUS MyFilterProtectPreCreate(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);
FLT_POSTOP_CALLBACK_STATUS MyFilterProtectPostCreate(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags);

FLT_PREOP_CALLBACK_STATUS MyFilterProtectPreSetInformation(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*);

FLT_PREOP_CALLBACK_STATUS MyPreCleanup(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*);
FLT_POSTOP_CALLBACK_STATUS MyPostCleanup(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags);
///

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
	{ IRP_MJ_CREATE, 0, MyFilterProtectPreCreate, MyFilterProtectPostCreate },
	{ IRP_MJ_SET_INFORMATION, 0, MyFilterProtectPreSetInformation, nullptr },
	{ IRP_MJ_CLEANUP, 0, MyPreCleanup, MyPostCleanup},
	{ IRP_MJ_OPERATION_END }
};


NTSTATUS
MyFilterUnload(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

NTSTATUS
MyFilterInstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
);


NTSTATUS
MyFilterInstanceQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

VOID
MyFilterInstanceTeardownStart(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

VOID
MyFilterInstanceTeardownComplete(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
);

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {

	sizeof(FLT_REGISTRATION),
	FLT_REGISTRATION_VERSION,
	0,                       //  Flags

	ContextRegistration,                 //  Context
	Callbacks,               //  Operation callbacks

	MyFilterUnload,                   //  MiniFilterUnload

	MyFilterInstanceSetup,            //  InstanceSetup
	MyFilterInstanceQueryTeardown,    //  InstanceQueryTeardown
	MyFilterInstanceTeardownStart,    //  InstanceTeardownStart
	MyFilterInstanceTeardownComplete, //  InstanceTeardownComplete
};
