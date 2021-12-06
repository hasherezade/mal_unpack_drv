#include "filters.h"

#include "common.h"
#include "process_util.h"

#define PROCESS_VM_OPERATION (0x0008)
#define PROCESS_VM_WRITE (0x0020)
#define PROCESS_CREATE_THREAD (0x0002)


OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION Info)
{
	UNREFERENCED_PARAMETER(RegistrationContext);
	if (Info->KernelHandle) {
		return OB_PREOP_SUCCESS; //do not interfere in kernel mode operations
	}
	const ULONG sourcePID = HandleToULong(PsGetCurrentProcessId()); //the PID of the process performing the operation
	if (!Data::ContainsProcess(sourcePID)) {
		return OB_PREOP_SUCCESS; //do not interfere
	}

	PEPROCESS targetProcess = (PEPROCESS)Info->Object;
	const ULONG targetPid = HandleToULong(PsGetProcessId(targetProcess));

	const bool isMyProcess = Data::AreSameFamily(targetPid, sourcePID) 
		|| Data::AreSameFamily(ProcessUtil::GetProcessParentPID(targetProcess), sourcePID) ;

	if (isMyProcess) {
		DbgPrint(DRIVER_PREFIX "[%d] Allowing opening handle to a child: [%d]\n", sourcePID, targetPid);
		return OB_PREOP_SUCCESS;
	}
	///
	bool isDenied = false;
	// writing operations:
	{
		if ((Info->Parameters->CreateHandleInformation.DesiredAccess & PROCESS_VM_WRITE)
			|| (Info->Parameters->CreateHandleInformation.DesiredAccess & PROCESS_VM_OPERATION))
		{
			DbgPrint(DRIVER_PREFIX "[%d] trying to open process for writing: [%d]\n", sourcePID, targetPid);
			// disallow the operations:
			if (!isMyProcess) {
				isDenied = true;
				Info->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_VM_OPERATION;
				Info->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_VM_WRITE;
			}
		}

		if ((Info->Parameters->DuplicateHandleInformation.DesiredAccess & PROCESS_VM_WRITE)
			|| (Info->Parameters->DuplicateHandleInformation.DesiredAccess & PROCESS_VM_OPERATION))
		{
			DbgPrint(DRIVER_PREFIX "[%d] trying to duplicate handle of the process for writing: [%d]\n", sourcePID, targetPid);
			// disallow the operations:
			if (!isMyProcess) {
				isDenied = true;
				Info->Parameters->DuplicateHandleInformation.DesiredAccess &= ~PROCESS_VM_OPERATION;
				Info->Parameters->DuplicateHandleInformation.DesiredAccess &= ~PROCESS_VM_WRITE;
			}
		}
	}

	// create remote thread:
	{
		if (Info->Parameters->CreateHandleInformation.DesiredAccess & PROCESS_CREATE_THREAD)
		{
			DbgPrint(DRIVER_PREFIX "[%d] trying to open process for creating a Thread: [%d]\n", sourcePID, targetPid);
			// disallow the operations:
			if (!isMyProcess) {
				isDenied = true;
				Info->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_CREATE_THREAD;
			}
		}

		if (Info->Parameters->DuplicateHandleInformation.DesiredAccess & PROCESS_CREATE_THREAD)
		{
			DbgPrint(DRIVER_PREFIX "[%d] trying to duplicate handle of the process for creating a Thread: [%d]\n", sourcePID, targetPid);
			// disallow the operations:
			if (!isMyProcess) {
				isDenied = true;
				Info->Parameters->DuplicateHandleInformation.DesiredAccess &= ~PROCESS_CREATE_THREAD;
			}
		}
	}
	if (isDenied) {
		DbgPrint(DRIVER_PREFIX "[%d] [!] The target PID: [%d] is not watched, ACCESS DENIED\n", sourcePID, targetPid);
		ProcessUtil::ShowProcessPath(targetProcess);
	}
	return OB_PREOP_SUCCESS;
}


NTSTATUS OnRegistryNotify(PVOID context, PVOID regNotifyClass, PVOID arg2)
{
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(arg2);
	const ULONG sourcePID = HandleToULong(PsGetCurrentProcessId()); //the PID of the process performing the operation
	if (!Data::ContainsProcess(sourcePID)) {
		return STATUS_SUCCESS; //do not interfere
	}
	const REG_NOTIFY_CLASS regNotify = (REG_NOTIFY_CLASS)(ULONG_PTR)regNotifyClass;
	switch (regNotify) {
		case RegNtPreCreateKey:
		case RegNtSetValueKey:
		case RegNtPreDeleteKey:
		case RegNtPreRenameKey:
			break;
		default:
			return STATUS_SUCCESS; //do not interfere
	}
	DbgPrint(DRIVER_PREFIX "[%d] Process is trying to access registry key, notify type: [%d]\n", sourcePID, regNotify);
	return STATUS_ACCESS_DENIED; //block the access
}
