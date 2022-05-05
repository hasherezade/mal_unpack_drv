#pragma once

#include "undoc_api.h"
#include "common.h"

namespace FileUtil {

    bool RetrieveImagePath(PIMAGE_INFO ImageInfo, WCHAR FileName[MAX_PATH_LEN]);

    NTSTATUS FetchFileId(HANDLE hFile, LONGLONG& FileId);

    NTSTATUS FetchFileSize(HANDLE hFile, LONGLONG& FileSize);

    LONGLONG GetFileIdByPath(PUNICODE_STRING FileName);

    NTSTATUS RequestFileDeletion(PUNICODE_STRING FileName);
};

