#include "file_util.h"
#include "common.h"

#include <fltKernel.h>

bool FileUtil::RetrieveImagePath(PIMAGE_INFO ImageInfo, WCHAR FileName[MAX_PATH_LEN])
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
    FileSize = (-1);

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
        if (NT_SUCCESS(status) && !fileInfo.Directory) {
            FileSize = fileInfo.EndOfFile.QuadPart;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = STATUS_UNSUCCESSFUL;
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
        HANDLE hFile;
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

NTSTATUS changeFileAttributes(HANDLE hFile)
{
    if (!hFile) {
        return STATUS_UNSUCCESSFUL;
    }
    IO_STATUS_BLOCK ioStatusBlock;
    FILE_BASIC_INFORMATION basicInfo = { 0 };
    NTSTATUS status = ZwQueryInformationFile(hFile, &ioStatusBlock, &basicInfo, sizeof(FILE_BASIC_INFORMATION), FileBasicInformation);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (basicInfo.FileAttributes & FILE_ATTRIBUTE_SYSTEM) {
        DbgPrint(DRIVER_PREFIX "[!!!] This file has FILE_ATTRIBUTE_SYSTEM");
        basicInfo.FileAttributes ^= FILE_ATTRIBUTE_SYSTEM;
    }
    if (basicInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) {
        DbgPrint(DRIVER_PREFIX "[!!!]This file has FILE_ATTRIBUTE_READONLY");
        basicInfo.FileAttributes ^= FILE_ATTRIBUTE_READONLY;
    }
    if (basicInfo.FileAttributes & FILE_ATTRIBUTE_HIDDEN) {
        DbgPrint(DRIVER_PREFIX "[!!!]This file has FILE_ATTRIBUTE_HIDDEN");
        basicInfo.FileAttributes ^= FILE_ATTRIBUTE_HIDDEN;
    }
    status = ZwSetInformationFile(hFile, &ioStatusBlock, &basicInfo, sizeof(FILE_BASIC_INFORMATION), FileBasicInformation);
    if (NT_SUCCESS(status)) {
        status = ioStatusBlock.Status;
    }
    return status;
}

NTSTATUS FileUtil::RequestFileDeletion(LONGLONG FileId)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_UNSUCCESSFUL;
    }
    if (FileId == FILE_INVALID_FILE_ID) {
        return STATUS_INVALID_PARAMETER;
    }
    NTSTATUS openStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    HANDLE hFile = 0;
    UNICODE_STRING ucName = { sizeof(FileId), sizeof(FileId), (PWSTR)FileId };

    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, &ucName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    __try
    {
        IO_STATUS_BLOCK ioStatusBlock;
        openStatus = ZwCreateFile(&hFile,
            SYNCHRONIZE | DELETE,
            &objAttr, &ioStatusBlock,
            NULL,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_DELETE,
            FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE | FILE_OPEN_BY_FILE_ID,
            NULL,
            0
        );
        if (NT_SUCCESS(openStatus)) {
            if (NT_SUCCESS(changeFileAttributes(hFile))) {
                DbgPrint(DRIVER_PREFIX "[+][%llx] File attributes changed\n", FileId);
            }
            FILE_DISPOSITION_INFORMATION disposition = { TRUE };
            status = ZwSetInformationFile(hFile, &ioStatusBlock, &disposition, sizeof(FILE_DISPOSITION_INFORMATION), FileDispositionInformation);
            if (NT_SUCCESS(status)) {
                status = ioStatusBlock.Status;
            }
        }
        else {
            DbgPrint(DRIVER_PREFIX "[!!!][%llx] Failed to set the file for deletion, status %X\n", FileId, status);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = STATUS_UNSUCCESSFUL;
    }
    if (!NT_SUCCESS(openStatus) && hFile) {
        ZwClose(hFile);
    }
    return status;
}

