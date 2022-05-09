#include <ntifs.h>

#include <fltKernel.h>
#include <dontuse.h>

#include "main.h"
#include "undoc_api.h"

#include "common.h"
#include "data_manager.h"
#include "filters.h"
#include "fs_filters.h"

#include "process_util.h"
#include "file_util.h"
#include "util.h"

#define SLEEP_TIME 1000

#define ALTITUDE_PROCESS_FILTER L"12345.6171"
#define ALTITUDE_REGISTRY_FILTER L"7657.124"

#define SUPPORTED_CLIENT_NAME L"\\mal_unpack.exe"

#define IO_METHOD_FROM_CTL_CODE(cltCode) (cltCode & 0x00000003)

active_settings g_Settings;
//---

bool _AddProcessToParent(ULONG PID, ULONG ParentPID)
{
	if (Data::ContainsProcess(ParentPID)) {
		DbgPrint(DRIVER_PREFIX "[%d] created WATCHED process: [%d]\n", ParentPID, PID);
		t_add_status aStat = Data::AddProcess(PID, ParentPID);
		if (aStat == ADD_OK || aStat == ADD_ALREADY_EXIST) {
			return true;
		}
		if (aStat == ADD_LIMIT_EXHAUSTED) {
			DbgPrint(DRIVER_PREFIX "[%d] Could not add to the watchlist: limit exhausted\n", PID);
		}
	}
	return false;
}

void _OnProcessCreation(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId, _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	const ULONG PID = HandleToULong(ProcessId);

	USHORT commandLineSize = 0;
	if (CreateInfo->CommandLine) {
		commandLineSize = CreateInfo->CommandLine->Length;
	}
	const ULONG ParentPID = HandleToULong(CreateInfo->ParentProcessId);
	bool isAdded = _AddProcessToParent(PID, ParentPID);

	const ULONG creatorPID = HandleToULong(PsGetCurrentProcessId()); //the PID creating the thread
	if (!isAdded && (ParentPID != creatorPID)) {
		isAdded = _AddProcessToParent(PID, creatorPID);
	}
	if (isAdded && commandLineSize) {
		DbgPrint(DRIVER_PREFIX "Added: [%d] -> %S\n", PID, CreateInfo->CommandLine->Buffer);
	}
}

void _OnProcessExit(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId)
{
	UNREFERENCED_PARAMETER(Process);

	const ULONG PID = HandleToULong(ProcessId);
	Data::WaitForProcessDeletion(PID, 0);
}

void OnProcessNotify(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId, _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	if (CreateInfo) {
		//process created:
		_OnProcessCreation(Process, ProcessId, CreateInfo);
	}
	else {
		_OnProcessExit(Process, ProcessId);
	}
}

void OnThreadNotify(HANDLE ProcessId, HANDLE Thread, BOOLEAN Create)
{
	const ULONG creatorPID = HandleToULong(PsGetCurrentProcessId()); //the PID creating the thread
	const ULONG targetPID = HandleToULong(ProcessId);
	const ULONG ThreadId = HandleToULong(Thread);

	if (!Data::ContainsProcess(creatorPID)) {
		return;
	}
	if (Create && (creatorPID != targetPID)) {
		DbgPrint(DRIVER_PREFIX "[%d] THREAD: Creating remote thread! %d -> %d [%x]\n", creatorPID, creatorPID, targetPID, targetPID);
		if (Data::AddProcess(targetPID, creatorPID) == ADD_LIMIT_EXHAUSTED) {
			DbgPrint(DRIVER_PREFIX "[%d] Could not add to the watchlist: limit exhausted\n", targetPID);
		}
	}
}

#define _RETRIEVE_PATH
void OnImageLoadNotify(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo)
{
	if (ProcessId == nullptr) {
		// system process, ignore
		return;
	}
#ifdef _RETRIEVE_PATH
	UNREFERENCED_PARAMETER(FullImageName);
	// retrieve image path manually: backward compatibility with Windows < 10
	WCHAR FileName[FileUtil::MAX_PATH_LEN] = { 0 };
	if (!FileUtil::RetrieveImagePath(ImageInfo, FileName)) {
		return;
	}
	UNICODE_STRING ImageU = { 0 };
	RtlInitUnicodeString(&ImageU, FileName);

	PUNICODE_STRING ImagePath = &ImageU;
#else
	UNREFERENCED_PARAMETER(ImageInfo);
	PUNICODE_STRING ImagePath = FullImageName;
#endif
	if (!ImagePath) {
		return;
	}
	const ULONG PID = HandleToULong(ProcessId);
	//DbgPrint(DRIVER_PREFIX __FUNCTION__" [%d] Retrieved path: %S\n", PID, ImagePath->Buffer);

	LONGLONG fileId = FileUtil::GetFileIdByPath(ImagePath);
	if (fileId == FILE_INVALID_FILE_ID) {
		return;
	}

	const t_add_status aStat = Data::AddProcessToFileOwner(PID, fileId);
	if (aStat == ADD_INVALID_ITEM) {
		return; // this file is not owned by any process
	}
	if (aStat == ADD_LIMIT_EXHAUSTED) {
		DbgPrint(DRIVER_PREFIX "[%d] Could not add to the watchlist: limit exhausted\n");
	}
	if (aStat == ADD_OK) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__" [%d] Added process created from the OWNED file-> %zX\n", PID, fileId);
	}
}

void _UnregisterCallbacks()
{
	if (g_Settings.RegHandle) {
		ObUnRegisterCallbacks(g_Settings.RegHandle);
		g_Settings.RegHandle = NULL;
	}
	if (g_Settings.RegCookie.QuadPart != 0) {
		CmUnRegisterCallback(g_Settings.RegCookie);
		g_Settings.RegCookie.QuadPart = 0;
	}
	if (g_Settings.gFilterHandle) {
		FltUnregisterFilter(g_Settings.gFilterHandle);
		g_Settings.gFilterHandle = NULL;
	}
}

void MyDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	Data::FreeGlobals();

	//unregister the notification
	if (g_Settings.hasImageNotify) {
		PsRemoveLoadImageNotifyRoutine(OnImageLoadNotify);
		g_Settings.hasImageNotify = false;
	}
	if (g_Settings.hasThreadNotify) {
		PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
		g_Settings.hasThreadNotify = false;
	}
	if (g_Settings.hasProcessNotify) {
		PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
		g_Settings.hasProcessNotify = false;
	}

	_UnregisterCallbacks();

	if (g_Settings.hasLink) {
		UNICODE_STRING symLink = RTL_CONSTANT_STRING(MY_DRIVER_LINK);
		// delete symbolic link
		IoDeleteSymbolicLink(&symLink);
		g_Settings.hasLink = false;
	}

	if (g_Settings.hasDevice) {
		// delete device object
		IoDeleteDevice(DriverObject->DeviceObject);
		g_Settings.hasDevice = false;

		DbgPrint(DRIVER_PREFIX "driver unloaded!\n");
	}
}

#define _ONLY_SUPPORTED_CLIENT
NTSTATUS HandleCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

#ifdef _ONLY_SUPPORTED_CLIENT
	NTSTATUS openStatus = STATUS_ACCESS_DENIED;
	const ULONG sourcePID = HandleToULong(PsGetCurrentProcessId()); //the PID of the process performing the operation
	
	PEPROCESS Process;
	NTSTATUS status = PsLookupProcessByProcessId(ULongToHandle(sourcePID), &Process);
	if (NT_SUCCESS(status)) {
		//TODO: make more fancy check:
		if (ProcessUtil::CheckProcessPath(Process, SUPPORTED_CLIENT_NAME)) {
			openStatus = STATUS_SUCCESS;
		}
		ObDereferenceObject(Process);
	}

	if (openStatus != STATUS_SUCCESS) {
		DbgPrint(DRIVER_PREFIX "[%d] ACCESS DENIED: cannot open the driver with this process\n", sourcePID);
	}
#else
	NTSTATUS openStatus = STATUS_SUCCESS;
#endif //  ONLY_SUPPORTED_CLIENT

	Irp->IoStatus.Status = openStatus;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return openStatus;
}

NTSTATUS FetchInputBufferOfMinSize(IN PIRP Irp, OUT void** inpData, IN const size_t inpDataSize, OUT OPTIONAL size_t *actualSize = nullptr)
{
	if (!Irp || inpData == nullptr) {
		return STATUS_UNSUCCESSFUL;
	}
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ULONG method = IO_METHOD_FROM_CTL_CODE(stack->Parameters.DeviceIoControl.IoControlCode);

	size_t InputBufferLength = 0;

	if (method == METHOD_BUFFERED) {
		InputBufferLength = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (InputBufferLength < inpDataSize) {
			return STATUS_BUFFER_TOO_SMALL;
		}
		*inpData = Irp->AssociatedIrp.SystemBuffer;
	}
	else if (method == METHOD_NEITHER) {
		InputBufferLength = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (InputBufferLength < inpDataSize) {
			return STATUS_BUFFER_TOO_SMALL;
		}
		*inpData = stack->Parameters.DeviceIoControl.Type3InputBuffer;
	}
	else {
		return STATUS_NOT_SUPPORTED;
	}
	if (*inpData == nullptr) {
		return STATUS_INVALID_PARAMETER;
	}
	if (actualSize) {
		*actualSize = InputBufferLength;
	}
	return STATUS_SUCCESS;
}

template<typename DATA_BUF>
NTSTATUS FetchInputBuffer(PIRP Irp, DATA_BUF** inpData)
{
	if (!Irp || inpData == nullptr) {
		return STATUS_UNSUCCESSFUL;
	}
	const size_t inpDataSize = sizeof(DATA_BUF);
	return FetchInputBufferOfMinSize(Irp, (void**)inpData, inpDataSize);
}

//---
t_add_status _AddProcessWatch(ProcessDataEx &settings)
{
	const ULONG PID = settings.Pid;
	const LONGLONG FileId = settings.fileId;

	PEPROCESS Process;
	NTSTATUS status = PsLookupProcessByProcessId(ULongToHandle(PID), &Process);
	if (!NT_SUCCESS(status)) {
		DbgPrint(DRIVER_PREFIX ": Such process does not exist: %d\n", PID);
		return t_add_status::ADD_INVALID_ITEM;
	}

	ObDereferenceObject(Process);

	DbgPrint(DRIVER_PREFIX ": Watching process requested %d, noresp=%d\n", PID, settings.noresp);
	t_add_status add_status = Data::AddProcessNode(PID, FileId, settings.noresp);
	if (status == ADD_OK && FileId != FILE_INVALID_FILE_ID) {
		if (Data::AddFile(FileId, PID) == ADD_OK) {
			DbgPrint(DRIVER_PREFIX ": Watching process file %llx\n", FileId);
		}
	}
	return add_status;
}

NTSTATUS FetchProcessData(PIRP Irp, ProcessDataEx &settings)
{
	NTSTATUS status = STATUS_INVALID_PARAMETER;
	// v2:
	{
		ProcessDataEx_v2* inpDataEx2 = nullptr;
		status = FetchInputBuffer(Irp, &inpDataEx2);
		if (NT_SUCCESS(status)) {
			settings.Pid = inpDataEx2->Pid;
			settings.fileId = inpDataEx2->fileId;
			settings.noresp = inpDataEx2->noresp;
			return status;
		}
	}
	// v1:
	{
		ProcessDataEx_v1* inpDataEx = nullptr;
		status = FetchInputBuffer(Irp, &inpDataEx);
		if (NT_SUCCESS(status)) {
			settings.Pid = inpDataEx->Pid;
			settings.fileId = inpDataEx->fileId;
			return status;
		}
	}
	// basic:
	{
		ProcessDataBasic* inpData = nullptr;
		status = FetchInputBuffer(Irp, &inpData);
		if (NT_SUCCESS(status)) {
			settings.Pid = inpData->Pid;
		}
	}
	return status;
}

NTSTATUS AddProcessWatch(PIRP Irp)
{
	ProcessDataEx settings = { 0 };
	settings.fileId = FILE_INVALID_FILE_ID;

	NTSTATUS status = FetchProcessData(Irp, settings);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	const t_add_status ret = _AddProcessWatch(settings);
	switch (ret) {
		case ADD_OK:
		case ADD_ALREADY_EXIST:
			return STATUS_SUCCESS;
		case ADD_INVALID_ITEM:
			return STATUS_INVALID_PARAMETER;
	}
	const int count = Data::CountProcessTrees();
	DbgPrint(DRIVER_PREFIX "[!][%zd] Failed to add process to the list. Watched nodes = %zd, add status = %d\n", settings.Pid, count, ret);
	return STATUS_UNSUCCESSFUL;
}


NTSTATUS RemoveProcessWatch(PIRP Irp)
{
	ProcessDataBasic* inpData = nullptr;

	NTSTATUS status = FetchInputBuffer(Irp, &inpData);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	const ULONG PID = inpData->Pid;
	DbgPrint(DRIVER_PREFIX "Removing process watch: %d\n", PID);
	if (Data::DeleteProcess(PID)) {
		DbgPrint(DRIVER_PREFIX "Removed from the list: %d\n", PID);
	}
	return STATUS_SUCCESS;
}

NTSTATUS _TerminateWatched(ULONG PID)
{
	if (!Data::ContainsProcess(PID)) {
		return STATUS_SUCCESS;
	}
	NTSTATUS status = ProcessUtil::TerminateProcess(PID);
	if (NT_SUCCESS(status)) {
		Data::DeleteProcess(PID);
	}
	return status;
}

NTSTATUS TerminateWatched(PIRP Irp)
{
	ProcessDataBasic* inpData = nullptr;

	NTSTATUS status = FetchInputBuffer(Irp, &inpData);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	return _TerminateWatched(inpData->Pid);
}

#define _TREAT_RENAMED_AS_DELETED
NTSTATUS _DeleteWatchedFile(ULONG PID, PUNICODE_STRING FileName)
{
	LONGLONG fileId = FileUtil::GetFileIdByPath(FileName);
	const ULONG fileOwnerPid = Data::GetFileOwner(fileId);
	if (fileOwnerPid != PID) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ "FileID = %llx, PID = %d, fileOwnerPid = %d - owner mismatch!\n", fileId, PID, fileOwnerPid);
		return STATUS_ACCESS_DENIED;
	}
	NTSTATUS status = FileUtil::RequestFileDeletion(FileName);
	DbgPrint(DRIVER_PREFIX __FUNCTION__ "FileID = %llx, PID = %d, status = %X\n", fileId, PID, status);
#ifdef _TREAT_RENAMED_AS_DELETED
	if (status == STATUS_CANNOT_DELETE) {
		if (Util::hasSuffix(FileName, RENAMED_EXTENSION)) {
			if (Data::DeleteFile(fileId)) {
				status = STATUS_SUCCESS;
			}
		}
	}
#endif
	return status;
}

NTSTATUS DeleteWatchedFile(PIRP Irp)
{
	ProcessFileData* inpData = nullptr;
	const size_t minimalLen = 4; // minimal length we expect valid path to be
	const size_t minimalSize = sizeof(ProcessFileData) + (sizeof(WCHAR) * minimalLen);
	size_t actualSize = 0;
	NTSTATUS status = FetchInputBufferOfMinSize(Irp, (void**)&inpData, minimalSize, &actualSize);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	NT_ASSERT(actualSize > sizeof(ProcessFileData));
	const size_t actualLen = (actualSize - sizeof(ProcessFileData)) / sizeof(WCHAR);
	// ensure it is NULL-terminated:
	inpData->FileName[actualLen] = L'\0';
	DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Passed buffer: %S len: %lld size: %lld\n", inpData->FileName, actualLen, actualSize);

	UNICODE_STRING name;
	RtlInitUnicodeString(&name, inpData->FileName);
	KdPrint((DRIVER_PREFIX __FUNCTION__ ": Passed buffer to unicode: %wZ\n", name));
	return _DeleteWatchedFile(inpData->Pid, &name);
}

NTSTATUS _CopyWatchedList(PIRP Irp, ULONG_PTR& outLen, bool files)
{
	ProcessDataBasic* inpData = nullptr;
	NTSTATUS status = FetchInputBuffer(Irp, &inpData);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	const size_t elementSize = files ? sizeof(LONGLONG) : sizeof(ULONG);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	const size_t outBufSize = stack->Parameters.DeviceIoControl.OutputBufferLength;
	if (outBufSize < elementSize) {
		return STATUS_BUFFER_TOO_SMALL;
	}
	void* outData = Irp->AssociatedIrp.SystemBuffer;
	if (outData == nullptr) {
		return STATUS_INVALID_PARAMETER;
	}
	ULONG parentPid = inpData->Pid;
	size_t items = 0;
	if (files) {
		items = Data::CopyFilesList(parentPid, outData, outBufSize);
	}
	else {
		items = Data::CopyProcessList(parentPid, outData, outBufSize);
	}
	if (items > 0) {
		KdPrint((DRIVER_PREFIX "Copied items to system buffer: %d\n", items));
		size_t copiedSize = items * elementSize;
		outLen = ULONG(copiedSize);
	}
	return STATUS_SUCCESS;
}

NTSTATUS CopyProcessesList(PIRP Irp, ULONG_PTR& outLen)
{
	return _CopyWatchedList(Irp, outLen, false);
}

NTSTATUS CopyFilesList(PIRP Irp, ULONG_PTR& outLen)
{
	return _CopyWatchedList(Irp, outLen, true);
}

NTSTATUS FetchDriverVersion(PIRP Irp, ULONG_PTR &outLen)
{
	const char* versionStr = VER_FILEVERSION_STR;
	const size_t versionSize = strlen(versionStr) + 1;

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	const size_t outBufSize = stack->Parameters.DeviceIoControl.OutputBufferLength;
	if (outBufSize < versionSize) {
		return STATUS_BUFFER_TOO_SMALL;
	}
	void* outBuf = Irp->AssociatedIrp.SystemBuffer;
	if (outBuf == nullptr) {
		return STATUS_INVALID_PARAMETER;
	}
	::memcpy(outBuf, versionStr, versionSize);
	outLen = versionSize;
	return STATUS_SUCCESS;
}

NTSTATUS CountNodes(PIRP Irp, ULONG_PTR& outLen)
{
	auto counter = Data::CountProcessTrees();
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	const size_t outBufSize = stack->Parameters.DeviceIoControl.OutputBufferLength;
	if (outBufSize < sizeof(counter)) {
		return STATUS_BUFFER_TOO_SMALL;
	}
	void* outBuf = Irp->AssociatedIrp.SystemBuffer;
	if (outBuf == nullptr) {
		return STATUS_INVALID_PARAMETER;
	}
	::memcpy(outBuf, &counter, sizeof(counter));
	outLen = sizeof(counter);
	return STATUS_SUCCESS;
}

NTSTATUS HandleDeviceControl(PDEVICE_OBJECT, PIRP Irp)
{
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	
	NTSTATUS status = STATUS_SUCCESS;

	ULONG_PTR outLen = 0;
	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_MUNPACK_COMPANION_VERSION:
		{
			status = FetchDriverVersion(Irp, outLen);
			break;
		}
		case IOCTL_MUNPACK_COMPANION_COUNT_NODES:
		{
			status = CountNodes(Irp, outLen);
			break;
		}
		case IOCTL_MUNPACK_COMPANION_ADD_TO_WATCHED:
		{
			status = AddProcessWatch(Irp);
			break;
		}
#ifdef _ALLOW_DELETE
		case IOCTL_MUNPACK_COMPANION_REMOVE_FROM_WATCHED:
		{
			status = RemoveProcessWatch(Irp);
			break;
		}
#endif
		case IOCTL_MUNPACK_COMPANION_TERMINATE_WATCHED:
		{
			status = TerminateWatched(Irp);
			break;
		}
		case IOCTL_MUNPACK_COMPANION_DELETE_WATCHED_FILE:
		{
			status = DeleteWatchedFile(Irp);
			break;
		}
		case IOCTL_MUNPACK_COMPANION_LIST_PROCESSES:
		{
			status = CopyProcessesList(Irp, outLen);
			break;
		}
		case IOCTL_MUNPACK_COMPANION_LIST_FILES:
		{
			status = CopyFilesList(Irp, outLen);
			break;
		}
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = outLen;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}


NTSTATUS _RegisterOpenProcessCallbacks()
{
	OB_OPERATION_REGISTRATION operations[] = {
		{
			PsProcessType,		// object type
			OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE,
			OnPreOpenProcess, nullptr	// pre, post
		}
	};
	OB_CALLBACK_REGISTRATION reg = {
		OB_FLT_REGISTRATION_VERSION,
		1,				// operation count
		RTL_CONSTANT_STRING(ALTITUDE_PROCESS_FILTER),	// altitude
		nullptr,		// context
		operations
	};

	NTSTATUS status = ObRegisterCallbacks(&reg, &g_Settings.RegHandle);
	if (!NT_SUCCESS(status)) {
		DbgPrint(DRIVER_PREFIX "failed to register callbacks (status=%08X)\n", status);
		g_Settings.RegHandle = NULL;
		return status;
	}
	return status;
}


NTSTATUS _RegisterRegistryCallbacks(_In_ PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING altitude = RTL_CONSTANT_STRING(ALTITUDE_REGISTRY_FILTER);
	NTSTATUS status = CmRegisterCallbackEx(OnRegistryNotify, &altitude, DriverObject, nullptr, &g_Settings.RegCookie, nullptr);
	if (!NT_SUCCESS(status)) {
		DbgPrint(DRIVER_PREFIX "failed to set registry callback (%08X)\n", status);
		g_Settings.RegCookie.QuadPart = 0;
		return status;
	}
	return status;
}

NTSTATUS RegisterCallbacks(_In_ PDRIVER_OBJECT DriverObject)
{
	NTSTATUS status = _RegisterOpenProcessCallbacks();
	if (!NT_SUCCESS(status)) {
		return status;
	}
	// Registry operations filtering:
	status = _RegisterRegistryCallbacks(DriverObject);
	return status;
}

NTSTATUS _InitializeDriver(_In_ PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING devName = RTL_CONSTANT_STRING(MY_DEVICE);

	PDEVICE_OBJECT DeviceObject = nullptr;
	NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (NT_SUCCESS(status)) {
		g_Settings.hasDevice = true;
	} else {
		DbgPrint(DRIVER_PREFIX "Failed to create device (0x%08X)\n", status);
		return status;
	}

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(MY_DRIVER_LINK);
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (NT_SUCCESS(status)) {
		g_Settings.hasLink = true;
	} else {
		DbgPrint(DRIVER_PREFIX "Failed to create symbolic link (0x%08X)\n", status);
		return status;
	}

	status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
	if (NT_SUCCESS(status)) {
		g_Settings.hasProcessNotify = true;
	}
	else {
		DbgPrint(DRIVER_PREFIX "failed to register process callback (0x%08X)\n", status);
		return status;
	}

	status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
	if (NT_SUCCESS(status)) {
		g_Settings.hasThreadNotify = true;
	}
	else {
		DbgPrint(DRIVER_PREFIX "failed to set thread callback (status=%08X)\n", status);
		return status;
	}

	status = PsSetLoadImageNotifyRoutine(OnImageLoadNotify);
	if (NT_SUCCESS(status)) {
		g_Settings.hasImageNotify = true;
	}
	else {
		DbgPrint(DRIVER_PREFIX "failed to set image notify callback (status=%08X)\n", status);
		return status;
	}
	return STATUS_SUCCESS;
}

///

extern "C"
NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) 
{
	UNREFERENCED_PARAMETER(RegistryPath);

	// check version:
	RTL_OSVERSIONINFOW version = { 0 };
	RtlGetVersion(&version);
	DbgPrint(DRIVER_PREFIX "OS Version: %d.%d.%d\n", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
	DbgPrint(DRIVER_PREFIX "Driver Version: %s\n", VER_FILEVERSION_STR);

	if (!RtlIsNtDdiVersionAvailable(NTDDI_WINBLUE)) {
		DbgPrint(DRIVER_PREFIX "Windows < 8.1 is not supported!\n");
		return STATUS_NOT_SUPPORTED;
	}

	// init all global data:
	g_Settings.init();

	if (!Data::AllocGlobals()) {
		DbgPrint(DRIVER_PREFIX "Failed to initialize global data structures\n");
		return STATUS_FATAL_MEMORY_EXHAUSTION;
	}
	else {
		KdPrint((DRIVER_PREFIX "Initialized global data structures!\n"));
	}

	//
	//  Register with FltMgr to tell it our callback routines
	//
	NTSTATUS status = FltRegisterFilter(DriverObject,
		&FilterRegistration,
		&g_Settings.gFilterHandle);

	FLT_ASSERT(NT_SUCCESS(status));

	if (NT_SUCCESS(status)) {
		status = FltStartFiltering(g_Settings.gFilterHandle);
	}
	if (!NT_SUCCESS(status)) {
		MyDriverUnload(DriverObject);
		return status;
	}

	status = RegisterCallbacks(DriverObject);
	if (!NT_SUCCESS(status)) {
		MyDriverUnload(DriverObject);
		return status;
	}

	DriverObject->DriverUnload = MyDriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = HandleCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = HandleCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HandleDeviceControl;

	KdPrint((DRIVER_PREFIX "driver loaded!\n"));

	status = _InitializeDriver(DriverObject);
	if (NT_SUCCESS(status)) {
		DbgPrint(DRIVER_PREFIX "DriverEntry completed successfully\n");
	}
	else {
		MyDriverUnload(DriverObject);
	}
	return status;
}
