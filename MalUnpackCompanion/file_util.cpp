#include "file_util.h"
#include "common.h"

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
        DbgPrint(DRIVER_PREFIX __FUNCTION__" [!!!] Exception thrown during the \n");
    }

    if (!NT_SUCCESS(status)) {
        DbgPrint(DRIVER_PREFIX __FUNCTION__" [!!!] Failed to retrieve file ID, status: %X\n", status);
    }
    return status;
}


LONGLONG FileUtil::GetFileIdByPath(PUNICODE_STRING FileName)
{
    if (!FileName || !FileName->Buffer || !FileName->Length) {
        //DbgPrint(DRIVER_PREFIX "[!!!] Invalid name passed\n");
        return (FILE_INVALID_FILE_ID);
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        //DbgPrint(DRIVER_PREFIX "[!!$] Failed to open file by NAME: %S: IRQL is non passive\n", FileName->Buffer);
        return (FILE_INVALID_FILE_ID);
    }
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    LONGLONG FileId = FILE_INVALID_FILE_ID;
    __try
    {
        HANDLE hFile;
        IO_STATUS_BLOCK ioStatusBlock;
        NTSTATUS status = ZwCreateFile(&hFile, SYNCHRONIZE | FILE_READ_ATTRIBUTES, &objAttr, &ioStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
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
