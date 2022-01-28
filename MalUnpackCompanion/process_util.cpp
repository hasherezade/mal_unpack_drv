#include "process_util.h"
#include "data_structs.h"
#include "common.h"

namespace ProcessUtil {

	NTSTATUS RetrieveProcessHandle(const PEPROCESS Process, bool& isCurrentProcess, HANDLE& hProcess)
	{
		NTSTATUS status = STATUS_SUCCESS;
		isCurrentProcess = PsGetCurrentProcess() == Process;
		hProcess = NULL;
		if (isCurrentProcess) {
			hProcess = NtCurrentProcess();
		}
		else {
			status = ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, nullptr, 0, nullptr, KernelMode, &hProcess);
		}
		return status;
	}

};

bool ProcessUtil::CheckProcessPath(const PEPROCESS Process, PWCH supportedName)
{
	bool isCurrentProcess = false;
	HANDLE hProcess = NULL;
	NTSTATUS status = ProcessUtil::RetrieveProcessHandle(Process, isCurrentProcess, hProcess);
	if (!NT_SUCCESS(status)) {
		return false;
	}

	bool allowAccess = false;
	ULONG size = 300;
	UNICODE_STRING* processName = (UNICODE_STRING*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);

	if (processName) {
		RtlZeroMemory(processName, size); // ensure string will be NULL-terminated
		status = ZwQueryInformationProcess(hProcess, ProcessImageFileName, processName, size - sizeof(WCHAR), nullptr);
		if (NT_SUCCESS(status)) {
			const wchar_t* found = ::wcsstr(processName->Buffer, supportedName);
			if (found != nullptr) {
				if (::wcscmp(found, supportedName) == 0) {
					allowAccess = true;
				}
			}
		}
		ExFreePool(processName);
	}
	if (!isCurrentProcess) {
		ZwClose(hProcess);
	}
	if (!allowAccess) {
		DbgPrint(DRIVER_PREFIX "Access to the driver denied to the process: %wZ!\n", processName);
	}
	return allowAccess;
}

bool ProcessUtil::ShowProcessPath(const PEPROCESS Process)
{
	bool isCurrentProcess = false;
	HANDLE hProcess = NULL;
	NTSTATUS status = ProcessUtil::RetrieveProcessHandle(Process, isCurrentProcess, hProcess);
	if (!NT_SUCCESS(status)) {
		return false;
	}

	bool found = false;
	ULONG size = 300;
	UNICODE_STRING* processName = (UNICODE_STRING*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);

	if (processName) {
		RtlZeroMemory(processName, size);	// ensure string will be NULL-terminated
		status = ZwQueryInformationProcess(hProcess, ProcessImageFileName, processName, size - sizeof(WCHAR), nullptr);
		if (NT_SUCCESS(status)) {
			DbgPrint(DRIVER_PREFIX "Process name: %wZ!\n", processName);
			found = true;
		}
		ExFreePool(processName);
	}
	if (!isCurrentProcess) {
		ZwClose(hProcess);
	}
	return found;
}

NTSTATUS ProcessUtil::TerminateProcess(ULONG PID)
{
	NTSTATUS status = STATUS_SUCCESS;
	DbgPrint(DRIVER_PREFIX "Terminating process: %d\n", PID);

	OBJECT_ATTRIBUTES ObjectAttributes;
	InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	CLIENT_ID ClientId;
	ClientId.UniqueProcess = (HANDLE)PID;
	ClientId.UniqueThread = NULL;

	HANDLE hProcess;
	status = ZwOpenProcess(&hProcess, PROCESS_ALL_ACCESS, &ObjectAttributes, &ClientId);
	if (NT_SUCCESS(status))
	{
		status = ZwTerminateProcess(hProcess, 0);
		//the terminate operation is already pending
		if (status == STATUS_PROCESS_IS_TERMINATING) {
			status = STATUS_SUCCESS;
			DbgPrint(DRIVER_PREFIX "The terminate operation is already pending\n", status);
		}
		if (!NT_SUCCESS(status)) {
			DbgPrint(DRIVER_PREFIX "ZwTerminateProcess failed with status : %08X\n", status);
		}
		ZwClose(hProcess);
	}
	else {
		DbgPrint(DRIVER_PREFIX "ZwOpenProcess failed with status : %08X\n", status);
	}

	return status;
}

ULONG ProcessUtil::GetProcessParentPID(const PEPROCESS Process)
{
	bool isCurrentProcess = false;
	HANDLE hProcess = NULL;
	NTSTATUS status = ProcessUtil::RetrieveProcessHandle(Process, isCurrentProcess, hProcess);
	if (!NT_SUCCESS(status)) {
		return false;
	}

	ULONG_PTR parentPID = 0;
	__try
	{
		PROCESS_BASIC_INFORMATION pBasicInfo = { 0 };

		ULONG retrunLen = 0;
		const ULONG size = sizeof(PROCESS_BASIC_INFORMATION);
		status = ZwQueryInformationProcess(hProcess, ProcessBasicInformation, &pBasicInfo, size, &retrunLen);
		if (NT_SUCCESS(status) && (retrunLen == size)) {
			parentPID = pBasicInfo.InheritedFromUniqueProcessId;
			//DbgPrint(DRIVER_PREFIX "Process ParentID retrieved : [%p]\n", parentPID);
		}
		if (!isCurrentProcess) {
			ZwClose(hProcess);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		DbgPrint(DRIVER_PREFIX __FUNCTION__"failed with an exception");
	}
	return ULONG(parentPID);
}
