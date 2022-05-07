#pragma once

#include "undoc_api.h"

#define INVALID_FILE_SIZE (-1)

namespace FileUtil {

    const USHORT MAX_PATH_LEN = 1024;

    bool RetrieveImagePath(PIMAGE_INFO ImageInfo, WCHAR FileName[MAX_PATH_LEN]);

    NTSTATUS FetchFileId(HANDLE hFile, LONGLONG& FileId);

    NTSTATUS FetchFileSize(HANDLE hFile, LONGLONG& FileSize);

    LONGLONG GetFileIdByPath(PUNICODE_STRING FileName);

    NTSTATUS RequestFileDeletion(PUNICODE_STRING FileName);
};

