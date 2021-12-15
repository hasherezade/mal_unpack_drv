#include "fs_filters.h"
#include "file_util.h"

namespace FltUtil {

	NTSTATUS GetFileId(PCFLT_RELATED_OBJECTS FltObjects, PFLT_CALLBACK_DATA Data, LONGLONG& FileId)
	{
		FileId = FILE_INVALID_FILE_ID;

		if (!Data || !FltObjects) {
			return STATUS_INVALID_PARAMETER;
		}
		
		PFLT_FILE_NAME_INFORMATION pFileNameInfo = NULL;
		NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFileNameInfo);
		if (status != STATUS_SUCCESS) {
			if (STATUS_FLT_INVALID_NAME_REQUEST != status) {
				DbgPrint(DRIVER_PREFIX __FUNCTION__ "[!!!] Failed to get filename information, status: %X\n", status);
			}
			return status;
		}

		PUNICODE_STRING FileName = &pFileNameInfo->Name;
		OBJECT_ATTRIBUTES objAttr;
		InitializeObjectAttributes(&objAttr, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

		HANDLE hFile;
		IO_STATUS_BLOCK ioStatusBlock;
		status = FltCreateFile(FltObjects->Filter,
			FltObjects->Instance,
			&hFile,
			SYNCHRONIZE | FILE_READ_ATTRIBUTES,
			&objAttr,
			&ioStatusBlock,
			NULL,
			FILE_ATTRIBUTE_NORMAL,
			FILE_SHARE_READ,
			FILE_OPEN,
			FILE_SYNCHRONOUS_IO_NONALERT,
			NULL,
			0,
			IO_IGNORE_SHARE_ACCESS_CHECK
		);
		if (NT_SUCCESS(status)) {
			status = FileUtil::FetchFileId(hFile, FileId);
			FltClose(hFile);
		}
		FltReleaseFileNameInformation(pFileNameInfo);

		if (!NT_SUCCESS(status)) {
			if (status != STATUS_OBJECT_NAME_NOT_FOUND && status != STATUS_OBJECT_PATH_NOT_FOUND) {
				DbgPrint(DRIVER_PREFIX __FUNCTION__ "[#] Failed to retrieve fileID of %wZ, status: %X\n", FileName, status);
			}
		}
		return status;
	}

	//WARNING: use it only after the object is verified, otherwise it can cause crash!
	NTSTATUS FltGetFileSize(PCFLT_RELATED_OBJECTS FltObjects, LONGLONG& myFileSize)
	{
		myFileSize = (-1);
		if (!FltObjects) {
			return STATUS_INVALID_PARAMETER;
		}
		LARGE_INTEGER fileSize;
		NTSTATUS status = FsRtlGetFileSize(FltObjects->FileObject, &fileSize);
		if (NT_SUCCESS(status)) {
			myFileSize = fileSize.QuadPart;
		}
		return status;
	}

	NTSTATUS GetFileSize(PCFLT_RELATED_OBJECTS FltObjects, PFLT_CALLBACK_DATA Data, LONGLONG& myFileSize)
	{
		myFileSize = (-1);
		if (!Data || !FltObjects) {
			return STATUS_INVALID_PARAMETER;
		}

		PFLT_FILE_NAME_INFORMATION pFileNameInfo = NULL;
		NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFileNameInfo);
		if (status != STATUS_SUCCESS) {
			if (STATUS_FLT_INVALID_NAME_REQUEST != status) {
				DbgPrint(DRIVER_PREFIX __FUNCTION__ "[!!!] Failed to get filename information, status: %X\n", status);
			}
			return status;
		}

		PUNICODE_STRING FileName = &pFileNameInfo->Name;
		OBJECT_ATTRIBUTES objAttr;
		InitializeObjectAttributes(&objAttr, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

		HANDLE hFile;
		IO_STATUS_BLOCK ioStatusBlock;
		status = FltCreateFile(FltObjects->Filter,
			FltObjects->Instance,
			&hFile,
			SYNCHRONIZE | FILE_READ_ATTRIBUTES,
			&objAttr,
			&ioStatusBlock,
			NULL,
			FILE_ATTRIBUTE_NORMAL,
			FILE_SHARE_READ,
			FILE_OPEN,
			FILE_SYNCHRONOUS_IO_NONALERT,
			NULL,
			0,
			IO_IGNORE_SHARE_ACCESS_CHECK
		);
		if (NT_SUCCESS(status)) {
			status = FileUtil::GetFileSize(hFile, myFileSize);
			FltClose(hFile);
		}
		FltReleaseFileNameInformation(pFileNameInfo);

		if (!NT_SUCCESS(status)) {
			if (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND) {
				myFileSize = 0;
			} else {
				DbgPrint(DRIVER_PREFIX __FUNCTION__ "[#] Failed to retrieve fileSize of %wZ, status: %X\n", FileName, status);
			}
		}
		return status;
	}
}


///

FLT_PREOP_CALLBACK_STATUS MyFilterProtectPreCreate(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext)
{
	UNREFERENCED_PARAMETER(CompletionContext);

	if (Data->RequestorMode == KernelMode) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	auto& params = Data->Iopb->Parameters.Create;

	if (params.Options & FILE_DIRECTORY_FILE) {
		// this is a directory, do not interfere
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
	// check if the process is watched:
	const ULONG sourcePID = HandleToULong(PsGetCurrentProcessId()); //the PID of the process performing the operation
	if (!Data::ContainsProcess(sourcePID)) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK; // not a watched process, do not interfere
	}

	const PUNICODE_STRING fileName = (Data->Iopb->TargetFileObject) ? &Data->Iopb->TargetFileObject->FileName : nullptr;
	const ULONG createDisposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0x000000FF;
	const ACCESS_MASK DesiredAccess = (params.SecurityContext != nullptr) ? params.SecurityContext->DesiredAccess : 0;
	const ULONG all_write = FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA;

	// Check if it is creating a new file:
	LONGLONG FileSize = 0;
	NTSTATUS fileSizeStatus = FltUtil::GetFileSize(FltObjects, Data, FileSize);

	if (FILE_OPEN != createDisposition) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ ": [%lX] Requested file operation, options: %X createDisposition: %X FileSize: %llX FileSizeStatus: %X\n",
			sourcePID,
			params.Options,
			createDisposition,
			FileSize,
			fileSizeStatus
		);
	}

	const ULONG all_create = FILE_CREATE | FILE_SUPERSEDE | FILE_OVERWRITE_IF | FILE_OPEN_IF | FILE_OVERWRITE;
	// Check if it is creating a new file:
	if ((FILE_CREATE == createDisposition)
		|| (FileSize == 0 && (createDisposition & all_create)))
	{
		DbgPrint(DRIVER_PREFIX __FUNCTION__ " [%zX] Creating a new OWNED fileID\n", sourcePID);
		if (fileName) {
			DbgPrint(DRIVER_PREFIX "[%zX] file Name: %wZ \n", fileName);
		}
		// check if adding the file is possible:
		if (!Data::CanAddFile(sourcePID)) {

			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			DbgPrint(DRIVER_PREFIX "[%zX] Could not add to the files watchlist: limit exhausted\n", sourcePID);
			return FLT_PREOP_COMPLETE;
		}
		return FLT_PREOP_SUCCESS_WITH_CALLBACK;
	}

	// Retrieve and check the file ID:
	LONGLONG fileId = FILE_INVALID_FILE_ID;
	NTSTATUS fileIdStatus = FltUtil::GetFileId(FltObjects, Data, fileId);
	//It is not a creation of new file, and cannot verify the file ID, so deny the access...
	if (FILE_INVALID_FILE_ID == fileId) {
		if ((FILE_OPEN != createDisposition) || (DesiredAccess & all_write)) {
			//if could not check the file ID, and the file will be written, deny the access
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;

			DbgPrint(DRIVER_PREFIX "[%d] [!] Could not retrieve ID of the file (status= %X) -> ACCESS_DENIED!\n", sourcePID, fileIdStatus);
			if (fileName) {
				DbgPrint(DRIVER_PREFIX "[%zX] file Name: %wZ \n", fileId, fileName);
			}
			return FLT_PREOP_COMPLETE;
		}
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (!(DesiredAccess & all_write)) {
		// not a write access - skip
		return FLT_PREOP_SUCCESS_NO_CALLBACK; //do not interfere
	}

	if (!Data::IsProcessInFileOwners(sourcePID, fileId)) {
		// this file does not belong to the current process, block the access:
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;

		DbgPrint(DRIVER_PREFIX __FUNCTION__": Attempted writing to NOT-owned file, DesiredAccess: %X createDisposition: %X fileID: %zX -> ACCESS_DENIED\n",
			DesiredAccess,
			createDisposition,
			fileId);
		if (fileName) {
			DbgPrint(DRIVER_PREFIX "[%zX] file Name: %wZ \n", fileId, fileName);
		}
		return FLT_PREOP_COMPLETE;
	}
	else {
		DbgPrint(DRIVER_PREFIX __FUNCTION__": Attempted writing to the OWNED file, DesiredAccess: %X createDisposition: %X fileID: %zX\n",
			DesiredAccess,
			createDisposition,
			fileId);
	}
	return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS MyFilterProtectPostCreate(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags)
{
	UNREFERENCED_PARAMETER(CompletionContext);

	if (Flags & FLTFL_POST_OPERATION_DRAINING) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	if (Data->RequestorMode == KernelMode) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}
	if (!NT_SUCCESS(Data->IoStatus.Status)) {
		// the operation has been rejected at pre-create level
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	auto& params = Data->Iopb->Parameters.Create;

	if (params.Options & FILE_DIRECTORY_FILE) {
		// this is a directory, do not interfere
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	const ULONG sourcePID = HandleToULong(PsGetCurrentProcessId()); //the PID of the process performing the operation
	if (!Data::ContainsProcess(sourcePID)) {
		return FLT_POSTOP_FINISHED_PROCESSING; // not a watched process, do not interfere
	}

	const PUNICODE_STRING fileName = (Data->Iopb->TargetFileObject) ? &Data->Iopb->TargetFileObject->FileName : nullptr;
	const ULONG createDisposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0x000000FF;
	const ACCESS_MASK DesiredAccess = (params.SecurityContext != nullptr) ? params.SecurityContext->DesiredAccess : 0;
	const ULONG all_write = FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA;

	// Retrieve and check the file ID:
	LONGLONG fileId = FILE_INVALID_FILE_ID;
	NTSTATUS fileIdStatus = FltUtil::GetFileId(FltObjects, Data, fileId);
	if (FILE_INVALID_FILE_ID == fileId) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	// Retrieve file size:
	LONGLONG FileSize = 0;
	NTSTATUS fileSizeStatus = FltUtil::GetFileSize(FltObjects, Data, FileSize);
	
	if (FILE_OPEN != createDisposition) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ ": [%lX] Requested file operation, options: %X createDisposition: %X FileSize: %llX FileSizeStatus: %X\n",
			sourcePID,
			params.Options,
			createDisposition,
			FileSize,
			fileSizeStatus
		);
	}

	const ULONG all_create = FILE_CREATE | FILE_SUPERSEDE | FILE_OVERWRITE_IF | FILE_OPEN_IF | FILE_OVERWRITE;
	// Check if it is creating a new file:
	if ((FILE_CREATE == createDisposition) 
		|| (FileSize == 0 && (createDisposition & all_create)))
	{
		DbgPrint(DRIVER_PREFIX __FUNCTION__ " [%zX] Creating a new OWNED fileID: %zX fileIdStatus: %X\n", sourcePID, fileId, fileIdStatus);
		if (fileName) {
			DbgPrint(DRIVER_PREFIX "[%zX] file Name: %wZ \n", fileId, fileName);
		}
		// assign this file to the process that created it, deny access on fail:
		if (Data::AddFile(fileId, sourcePID) == ADD_LIMIT_EXHAUSTED) {
			DbgPrint(DRIVER_PREFIX "[%zX] Could not add to the files watchlist: limit exhausted\n", fileId);
		}
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS MyFilterProtectPreSetInformation(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*)
{
	UNREFERENCED_PARAMETER(FltObjects);

	if (Data->RequestorMode == KernelMode) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	// check if it is a delete operation:
	auto& params = Data->Iopb->Parameters.SetFileInformation;
	if (params.FileInformationClass != FileDispositionInformation && params.FileInformationClass != FileDispositionInformationEx) {
		// not a delete operation
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	FILE_DISPOSITION_INFORMATION* info = (FILE_DISPOSITION_INFORMATION*)params.InfoBuffer;
	if (!info->DeleteFile) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	// check if it is a watched process:
	const ULONG sourcePID = HandleToULong(PsGetCurrentProcessId()); //the PID of the process performing the operation
	if (!Data::ContainsProcess(sourcePID)) { 
		return FLT_PREOP_SUCCESS_NO_CALLBACK; //do not interfere
	}

	//get the File ID:
	LONGLONG fileId;
	NTSTATUS fileIdStatus = FltUtil::GetFileId(FltObjects, Data, fileId);

	const PUNICODE_STRING fileName = (Data->Iopb->TargetFileObject) ? &Data->Iopb->TargetFileObject->FileName : nullptr;
	bool isAllowed = true;

	// if watched process is the ower of this file:
	if (!Data::IsProcessInFileOwners(sourcePID, fileId)) {
		// this file does not belong to the current process, block the access:
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		isAllowed = false;
	}

	// report about the operation:
	if (isAllowed) {
		DbgPrint(DRIVER_PREFIX "[%d] Attempted setting delete disposition for the OWNED file, fileID: %zX status: %X\n",
			sourcePID,
			fileId,
			fileIdStatus);
	}
	else {
		DbgPrint(DRIVER_PREFIX "[%d] Attempted setting delete disposition for the NOT-owned file, fileID: %zX status: %X -> ACCESS_DENIED\n",
			sourcePID,
			fileId,
			fileIdStatus);
	}
	if (fileName) {
		DbgPrint(DRIVER_PREFIX "[%zX] file Name: %wZ \n", fileId, fileName);
	}
	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS
MyFilterUnload(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Flags);
	PAGED_CODE();

	DbgPrint(DRIVER_PREFIX "MyFilterUnload: Entered\n");
	FltUnregisterFilter(g_Settings.gFilterHandle);
	g_Settings.gFilterHandle = NULL;

	return STATUS_SUCCESS;
}


NTSTATUS
MyFilterInstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);
	UNREFERENCED_PARAMETER(VolumeFilesystemType);

	PAGED_CODE();

	DbgPrint(DRIVER_PREFIX "MyFilterInstanceSetup: Entered\n");
	return STATUS_SUCCESS;
}


NTSTATUS
MyFilterInstanceQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	DbgPrint(DRIVER_PREFIX "MyFilterInstanceQueryTeardown: Entered\n");
	return STATUS_SUCCESS;
}

VOID
MyFilterInstanceTeardownStart(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	DbgPrint(DRIVER_PREFIX "MyFilterInstanceTeardownStart: Entered\n");
}


VOID
MyFilterInstanceTeardownComplete(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	DbgPrint(DRIVER_PREFIX "MyFilterInstanceTeardownComplete: Entered\n");
}
