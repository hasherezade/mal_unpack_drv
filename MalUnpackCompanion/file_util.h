#pragma once

#include "undoc_api.h"

namespace FileUtil {

    const USHORT MAX_PATH_LEN = 1024;

    bool RetrieveImagePath(PIMAGE_INFO ImageInfo, WCHAR FileName[MAX_PATH_LEN]);

    NTSTATUS FetchFileId(HANDLE hFile, LONGLONG& FileId);

    NTSTATUS GetFileSize(HANDLE hFile, LONGLONG& FileSize);

    LONGLONG GetFileIdByPath(PUNICODE_STRING FileName);
};

