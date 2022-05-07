#include "file_util.h"
#include "common.h"

#include <fltKernel.h>

bool FileUtil::RetrieveImagePath(PIMAGE_INFO ImageInfo, WCHAR FileName[FileUtil::MAX_PATH_LEN])
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return false;
    }
    if (!ImageInfo || !ImageInfo->ExtendedInfoPresent) {
        return false;
    }
    PIMAGE_INFO_EX extendedInfo = NULL;
    extendedInfo = CONTAINING_RECORD(ImageInfo, IMAGE_INFO_EX, ImageInfo);
    if (!extendedInfo || !extendedInfo->FileObject) {
        return false;
    }
    PFLT_FILE_NAME_INFORMATION fileNameInformation = NULL;
    NTSTATUS status = FltGetFileNameInformationUnsafe(extendedInfo->FileObject, NULL, FLT_FILE_NAME_NORMALIZED, &fileNameInformation);
    if (!NT_SUCCESS(status)) {
        return false;
    }
    bool isOk = false;
    const size_t len = fileNameInformation->Name.Length < MAX_PATH_LEN ? fileNameInformation->Name.Length : MAX_PATH_LEN;
    if (len) {
        ::memcpy(FileName, fileNameInformation->Name.Buffer, len);
        isOk = true;
    }
    FltReleaseFileNameInformation(fileNameInformation);
    return isOk;
}

NTSTATUS FileUtil::FetchFileId(HANDLE hFile, LONGLONG &FileId)
{
    FileId = FILE_INVALID_FILE_ID;

    if (!hFile) {
        return STATUS_INVALID_PARAMETER;
    }
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    __try
    {
        IO_STATUS_BLOCK ioStatusBlock;
        FILE_INTERNAL_INFORMATION fileIdInfo;
        status = ZwQueryInformationFile(
            hFile,
            &ioStatusBlock,
            &fileIdInfo,
            sizeof(fileIdInfo),
            FileInternalInformation
        );
        if (NT_SUCCESS(status)) {
            FileId = fileIdInfo.IndexNumber.QuadPart;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = STATUS_UNSUCCESSFUL;
        DbgPrint(DRIVER_PREFIX __FUNCTION__" [!!!] Exception thrown\n");
    }
    return status;
}

NTSTATUS FileUtil::FetchFileSize(HANDLE hFile, LONGLONG& FileSize)
{
    FileSize = INVALID_FILE_SIZE;

    if (!hFile) {
        return STATUS_INVALID_PARAMETER;
    }
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    __try
    {
        IO_STATUS_BLOCK ioStatusBlock = { 0 };
        FILE_STANDARD_INFORMATION fileInfo = { 0 };
        status = ZwQueryInformationFile(
            hFile,
            &ioStatusBlock,
            &fileInfo,
            sizeof(fileInfo),
            FileStandardInformation
        );
        if (NT_SUCCESS(status) && 
            NT_SUCCESS(ioStatusBlock.Status) && 
            !fileInfo.Directory) 
        {
            FileSize = fileInfo.EndOfFile.QuadPart;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
        DbgPrint(DRIVER_PREFIX __FUNCTION__" [!!!] Exception thrown\n");
    }
    return status;
}


LONGLONG FileUtil::GetFileIdByPath(PUNICODE_STRING FileName)
{
    if (!FileName || !FileName->Buffer || !FileName->Length) {
        return (FILE_INVALID_FILE_ID);
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return (FILE_INVALID_FILE_ID);
    }
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    LONGLONG FileId = FILE_INVALID_FILE_ID;
    __try
    {
        HANDLE hFile = NULL;
        IO_STATUS_BLOCK ioStatusBlock;
        NTSTATUS status = ZwCreateFile(&hFile, 
            SYNCHRONIZE | FILE_READ_ATTRIBUTES,
            &objAttr, &ioStatusBlock, 
            NULL, 
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ, FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT,
            NULL,
            0
        );
        if (NT_SUCCESS(status)) {
            FetchFileId(hFile, FileId);
            ZwClose(hFile);
        }
        else {
            DbgPrint(DRIVER_PREFIX "[!!!] Failed to open file by NAME: %S, status: %X\n", FileName->Buffer, status);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        FileId = FILE_INVALID_FILE_ID;
    }
    return FileId;
}

NTSTATUS FileUtil::RequestFileDeletion(PUNICODE_STRING FileName)
{
    if (!FileName || !FileName->Buffer || !FileName->Length) {
        return STATUS_INVALID_PARAMETER;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    __try
    {
        HANDLE hFile = NULL;
        IO_STATUS_BLOCK ioStatusBlock;
        status = ZwCreateFile(&hFile,
            SYNCHRONIZE | DELETE,
            &objAttr, &ioStatusBlock,
            NULL,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_DELETE, FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE,
            NULL,
            0
        );
        if (NT_SUCCESS(status)) {
            FILE_DISPOSITION_INFORMATION disposition = { TRUE };
            status = ZwSetInformationFile(hFile, &ioStatusBlock, &disposition, sizeof(FILE_DISPOSITION_INFORMATION), FileDispositionInformation);
            if (NT_SUCCESS(status)) {
                status = ioStatusBlock.Status;
            }
            ZwClose(hFile);
        }
        else {
            DbgPrint(DRIVER_PREFIX "[!!!] Failed to set the file for deletion, status %X\n", status);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
    }
    return status;
}

