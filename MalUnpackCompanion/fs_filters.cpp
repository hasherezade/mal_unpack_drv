#include "fs_filters.h"
#include "file_util.h"

namespace FltUtil {

	NTSTATUS IsNonDefaultFileStream(PCFLT_RELATED_OBJECTS FltObjects, PFLT_CALLBACK_DATA Data, BOOLEAN& isNonDefault)
	{
		if (!Data || !FltObjects) {
			return STATUS_INVALID_PARAMETER;
		}
		PFLT_FILE_NAME_INFORMATION pFileNameInfo = NULL;
		NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFileNameInfo);
		if (!NT_SUCCESS(status)) {
			return status;
		}
		if (pFileNameInfo && pFileNameInfo->Stream.Length) {
			isNonDefault = TRUE;
		}
		FltReleaseFileNameInformation(pFileNameInfo);
		return status;
	}

	NTSTATUS GetFileId(PCFLT_RELATED_OBJECTS FltObjects, PFLT_CALLBACK_DATA Data, LONGLONG& FileId, char *caller)
	{
		UNREFERENCED_PARAMETER(caller);

		FileId = FILE_INVALID_FILE_ID;

		if (!Data || !FltObjects) {
			return STATUS_INVALID_PARAMETER;
		}
		
		PFLT_FILE_NAME_INFORMATION pFileNameInfo = NULL;
		NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFileNameInfo);
		if (!NT_SUCCESS(status)) {
			if (status != STATUS_FLT_INVALID_NAME_REQUEST && 
				status != STATUS_OBJECT_NAME_INVALID &&
				status != STATUS_OBJECT_PATH_NOT_FOUND)
			{
				KdPrint((DRIVER_PREFIX "[!!!][%s] Failed to get filename information, status: %X\n", caller, status));
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
		return status;
	}

	//WARNING: use it only after the object is verified, otherwise it can cause crash!
	NTSTATUS FltGetFileSize(PCFLT_RELATED_OBJECTS FltObjects, LONGLONG& myFileSize)
	{
		myFileSize = INVALID_FILE_SIZE;
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
		myFileSize = INVALID_FILE_SIZE;
		if (!Data || !FltObjects) {
			return STATUS_INVALID_PARAMETER;
		}

		PFLT_FILE_NAME_INFORMATION pFileNameInfo = NULL;
		NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFileNameInfo);
		if (!NT_SUCCESS(status)) {
			if (status != STATUS_FLT_INVALID_NAME_REQUEST &&
				status != STATUS_OBJECT_NAME_INVALID &&
				status != STATUS_OBJECT_PATH_NOT_FOUND)
			{
				KdPrint((DRIVER_PREFIX __FUNCTION__ "[!!!] Failed to get filename information, status: %X\n", status));
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
			status = FileUtil::FetchFileSize(hFile, myFileSize);
			FltClose(hFile);
		}
		FltReleaseFileNameInformation(pFileNameInfo);
		return status;
	}

	bool _IsAnyCreateOverwriteDisp(ULONG createDisposition)
	{
		switch (createDisposition) {
		case FILE_CREATE:
		case FILE_SUPERSEDE:
		case FILE_OVERWRITE_IF:
		case FILE_OPEN_IF:
		case FILE_OVERWRITE:
			return true;
		}
		return false;
	}

	bool IsCreateOrOverwriteEmpty(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects)
	{
		if (!Data || !FltObjects) return false;

		const auto& params = Data->Iopb->Parameters.Create;

		const ULONG createDisposition = (params.Options >> 24) & 0x000000FF;
		const bool isAnyCreate = _IsAnyCreateOverwriteDisp(createDisposition);

		// Retrieve file size:
		LONGLONG FileSize = INVALID_FILE_SIZE;
		const NTSTATUS fileSizeStatus = FltUtil::GetFileSize(FltObjects, Data, FileSize);

		if (fileSizeStatus == STATUS_OBJECT_NAME_NOT_FOUND || 
			fileSizeStatus == STATUS_OBJECT_PATH_NOT_FOUND)
		{
			FileSize = 0; //name not found, it is a new file
		}
		// Check if it is creating a new file or replacing empty:
		if ((FILE_CREATE == createDisposition)
			|| ((FileSize == 0) && isAnyCreate))
		{
			const ACCESS_MASK DesiredAccess = (params.SecurityContext != nullptr) ? params.SecurityContext->DesiredAccess : 0;
			KdPrint((DRIVER_PREFIX __FUNCTION__ ": Requested creating new file, createDisposition %X, DesiredAccess %X, isAnyCreate: %X, fileSize: %llX\n",
				createDisposition,
				DesiredAccess,
				isAnyCreate,
				FileSize
			));
			return true;
		}
		return false;
	}
}


bool _SetFileContext(PCFLT_RELATED_OBJECTS FltObjects, LONGLONG fileId, char* caller)
{
	FileContext* ctx = nullptr; //STATUS_FLT_CONTEXT_ALLOCATION_NOT_FOUND
	NTSTATUS ctx_status = FltAllocateContext(FltObjects->Filter, FLT_FILE_CONTEXT, sizeof(FileContext), PagedPool, (PFLT_CONTEXT*)&ctx);
	if (!NT_SUCCESS(ctx_status)) {
		DbgPrint(DRIVER_PREFIX "[CTX][ERR][%s][% llX] Creating the context failed : % x\n", caller, fileId, ctx_status);
		return false;
	}
	bool isSet = false;
	ctx->fileId = fileId;
	ctx_status = FltSetFileContext(FltObjects->Instance, FltObjects->FileObject, FLT_SET_CONTEXT_KEEP_IF_EXISTS, ctx, nullptr);
	if (NT_SUCCESS(ctx_status)) {
		KdPrint((DRIVER_PREFIX "[CTX][OK][%s][%llX] Attached the context to the file\n", caller, fileId));
		isSet = true;
	}
	else {
		if (ctx_status != STATUS_NOT_SUPPORTED && ctx_status != STATUS_FLT_CONTEXT_ALREADY_DEFINED) {
			DbgPrint(DRIVER_PREFIX "[CTX][ERR][%s][% llX] Attaching the context failed : % x\n", caller, fileId, ctx_status);
		}
	}
	FltReleaseContext(ctx); ctx = nullptr;
	return isSet;
}


LONGLONG _GetFileIdFromContext(PFLT_INSTANCE CONST Instance, PFILE_OBJECT CONST FileObject, char* caller)
{
	UNREFERENCED_PARAMETER(caller);
	LONGLONG fileId = FILE_INVALID_FILE_ID;
	FileContext* ctx = nullptr;
	NTSTATUS ctx_status = FltGetFileContext(Instance, FileObject, (PFLT_CONTEXT*)&ctx);
	if (NT_SUCCESS(ctx_status)) {
		if (ctx) {
			fileId = ctx->fileId;
			KdPrint((DRIVER_PREFIX "[CTX][OK][%s] Retrieved fileID: %llX\n", caller, fileId));
		}
		FltReleaseContext(ctx); ctx = nullptr;
	}
	else {
		KdPrint((DRIVER_PREFIX "[CTX][ERR][%s] Couldn't get file context, status: %x\n", caller, ctx_status));
	}
	return fileId;
}

///


FLT_PREOP_CALLBACK_STATUS MyFilterProtectPreCreate(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext)
{
	UNREFERENCED_PARAMETER(CompletionContext);

	if (Data->RequestorMode == KernelMode) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	auto& params = Data->Iopb->Parameters.Create;
	// chceck if the caller insist if it must be a directory:
	if (params.Options & FILE_DIRECTORY_FILE) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK; // do not interfere
	}
	// check if the process is watched:
	const ULONG sourcePID = HandleToULong(PsGetCurrentProcessId()); //the PID of the process performing the operation
	if (!Data::ContainsProcess(sourcePID)) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK; // not a watched process, do not interfere
	}

	// Check if it is creating a new file:
	if (FltUtil::IsCreateOrOverwriteEmpty(Data, FltObjects)) {
		BOOLEAN isAltStream = FALSE;
		if (NT_SUCCESS(FltUtil::IsNonDefaultFileStream(FltObjects, Data, isAltStream)) && isAltStream) {
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			DbgPrint(DRIVER_PREFIX "[%d] WARNING: Creating Alternative Data Streams is forbidden\n", sourcePID);
			return FLT_PREOP_COMPLETE;
		}
		// check if adding the file is possible:
		if (!Data::CanAddFile(sourcePID)) {
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			KdPrint((DRIVER_PREFIX " [%d] Could not add to the files watchlist: limit exhausted\n", sourcePID));
			return FLT_PREOP_COMPLETE;
		}
		return FLT_PREOP_SYNCHRONIZE; // sync with post-op
	}

	const ULONG createDisposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0x000000FF;
	const ACCESS_MASK DesiredAccess = (params.SecurityContext != nullptr) ? params.SecurityContext->DesiredAccess : 0;
	const ULONG all_write = FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA;

	// Retrieve and check the file ID:
	LONGLONG fileId = FILE_INVALID_FILE_ID;
	NTSTATUS fileIdStatus = FltUtil::GetFileId(FltObjects, Data, fileId, __FUNCTION__);
	//It is NOT a creation of new file, and cannot verify the file ID, so deny the access...
	if (FILE_INVALID_FILE_ID == fileId) {
		if ((FILE_OPEN != createDisposition) || (DesiredAccess & all_write)) {

			if (fileIdStatus == STATUS_OBJECT_NAME_NOT_FOUND) {
				Data->IoStatus.Status = fileIdStatus;
				return FLT_PREOP_COMPLETE;
			}
			//if could not check the file ID, and the file will be written, deny the access
			DbgPrint(DRIVER_PREFIX " [%d][!][%s] Could not retrieve ID of the file (status= %X), createDisposition: %X, DesiredAccess: %X-> ACCESS_DENIED!\n",
				sourcePID, 
				__FUNCTION__,
				fileIdStatus, createDisposition, DesiredAccess);
			
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			return FLT_PREOP_COMPLETE;
		}
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (DesiredAccess & all_write) {
		if (!Data::IsProcessInFileOwners(sourcePID, fileId)) {
			// this file does not belong to the current process, block the access:
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			return FLT_PREOP_COMPLETE;
		}

		DbgPrint(DRIVER_PREFIX __FUNCTION__": Attempted writing to the OWNED file, DesiredAccess: %X createDisposition: %X fileID: %zX\n",
			DesiredAccess,
			createDisposition,
			fileId);
	}

	return FLT_PREOP_SUCCESS_NO_CALLBACK; // no need to execute post-callback
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

	// Retrieve and check the file ID:
	LONGLONG fileId = FILE_INVALID_FILE_ID;
	NTSTATUS fileIdStatus = FltUtil::GetFileId(FltObjects, Data, fileId, __FUNCTION__);
	if (FILE_INVALID_FILE_ID == fileId) {
		// this should never happend: case handled pre-create
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	if (Data->IoStatus.Information == FILE_CREATED ||
		Data->IoStatus.Information == FILE_OVERWRITTEN ||
		Data->IoStatus.Information == FILE_SUPERSEDED)
	{
		DbgPrint(DRIVER_PREFIX "[%d][%s] Creating a new OWNED fileID: %zX fileIdStatus: %X\n", sourcePID, __FUNCTION__, fileId, fileIdStatus);
		const PUNICODE_STRING fileName = (Data->Iopb->TargetFileObject) ? &Data->Iopb->TargetFileObject->FileName : nullptr;
		if (fileName) {
			DbgPrint(DRIVER_PREFIX "[%llX] file Name: %wZ\n", fileId, fileName);
		}
		// assign this file to the process that created it:
		const t_add_status add_status =  Data::AddFile(fileId, sourcePID);
		if (add_status == ADD_OK) {
			_SetFileContext(FltObjects, fileId, __FUNCTION__);
		}
		if (add_status == ADD_LIMIT_EXHAUSTED) {
			DbgPrint(DRIVER_PREFIX "[%llX][%s] Could not add to the files watchlist: limit exhausted\n", fileId, __FUNCTION__);
		}
		if (add_status == ADD_FORBIDDEN) {
			DbgPrint(DRIVER_PREFIX "[%llX][%s] Could not add to the file to watchlist: already associated with other process\n", fileId, __FUNCTION__);
		}
		// cancel the open operation if the file was not added to the list
		if (add_status != ADD_OK && add_status != ADD_ALREADY_EXIST) {
			FltCancelFileOpen(FltObjects->Instance, FltObjects->FileObject);
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			Data->IoStatus.Information = 0;
			return FLT_POSTOP_FINISHED_PROCESSING;
		}
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
	NTSTATUS fileIdStatus = 0;
	LONGLONG fileId = _GetFileIdFromContext(FltObjects->Instance, FltObjects->FileObject,__FUNCTION__);
	if (fileId == FILE_INVALID_FILE_ID) {
		fileIdStatus = FltUtil::GetFileId(FltObjects, Data, fileId, __FUNCTION__);
	}

	const PUNICODE_STRING fileName = (Data->Iopb->TargetFileObject) ? &Data->Iopb->TargetFileObject->FileName : nullptr;

	// check if the watched process is the ower of this file:
	if (Data::IsProcessInFileOwners(sourcePID, fileId)) {
		// report about the operation:
		DbgPrint(DRIVER_PREFIX "[%d] Attempted setting delete disposition for the OWNED file, fileID: %llX status: %X\n",
			sourcePID,
			fileId,
			fileIdStatus);

		if (fileName) {
			DbgPrint(DRIVER_PREFIX "[%zX] file Name: %wZ \n", fileId, fileName);
		}
		return FLT_PREOP_SUCCESS_NO_CALLBACK; //do not interfere
	}

	DbgPrint(DRIVER_PREFIX "[%d] Attempted setting delete disposition for the NOT-owned file, fileID: %llX status: %X -> ACCESS_DENIED\n",
		sourcePID,
		fileId,
		fileIdStatus);
	if (fileName) {
		DbgPrint(DRIVER_PREFIX "[%zX] file Name: %wZ \n", fileId, fileName);
	}

	// this file does not belong to the current process, block the access:
	Data->IoStatus.Status = STATUS_ACCESS_DENIED;
	return FLT_PREOP_COMPLETE; //finish processing
}


FLT_PREOP_CALLBACK_STATUS MyPreCleanup(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*)
{
	PAGED_CODE();

	ULONG fileOwner = 0;
	LONGLONG fileId = FILE_INVALID_FILE_ID;
	NTSTATUS fileIdStatus = FltUtil::GetFileId(FltObjects, Data, fileId, __FUNCTION__);
	if (NT_SUCCESS(fileIdStatus)) {
		fileOwner = Data::GetFileOwner(fileId);
		if (fileOwner && fileId != FILE_INVALID_FILE_ID) {
			_SetFileContext(FltObjects, fileId, __FUNCTION__);
		}
	}
	return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS MyPostCleanup(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	if (Flags & FLTFL_POST_OPERATION_DRAINING) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	if (!NT_SUCCESS(Data->IoStatus.Status)) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	FILE_STANDARD_INFORMATION fileInfo;
	NTSTATUS status = FltQueryInformationFile(Data->Iopb->TargetInstance,
		Data->Iopb->TargetFileObject,
		&fileInfo,
		sizeof(fileInfo),
		FileStandardInformation,
		NULL);

	if (STATUS_FILE_DELETED != status) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	LONGLONG fileId = _GetFileIdFromContext(FltObjects->Instance, FltObjects->FileObject, __FUNCTION__);
	if (fileId != FILE_INVALID_FILE_ID) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__" >>> The watched file was deleted from the disk: %llx\n", fileId);
		if (Data::DeleteFile(fileId)) {
			DbgPrint(DRIVER_PREFIX __FUNCTION__" >>> DELETED from the watch list: %llx\n", fileId);
		}
	}
	return FLT_POSTOP_FINISHED_PROCESSING;
}


NTSTATUS
MyFilterUnload(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Flags);
	PAGED_CODE();

	KdPrint((DRIVER_PREFIX "MyFilterUnload: Entered\n"));
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

	KdPrint((DRIVER_PREFIX "MyFilterInstanceSetup: Entered\n"));
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

	KdPrint((DRIVER_PREFIX "MyFilterInstanceQueryTeardown: Entered\n"));
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

	KdPrint((DRIVER_PREFIX "MyFilterInstanceTeardownStart: Entered\n"));
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

	KdPrint((DRIVER_PREFIX "MyFilterInstanceTeardownComplete: Entered\n"));
}
