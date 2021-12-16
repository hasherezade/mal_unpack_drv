#pragma once

#include "undoc_api.h"

namespace FileUtil {

    const USHORT MAX_PATH_LEN = 1024;

    typedef struct nameInfo {
        OBJECT_NAME_INFORMATION ObjNameInfo;
        WCHAR FileName[MAX_PATH_LEN];
    } t_nameInfo;

    bool RetrieveImagePath(PIMAGE_INFO ImageInfo, t_nameInfo &NameInfo);

    NTSTATUS FetchFileId(HANDLE hFile, LONGLONG& FileId);

    NTSTATUS GetFileSize(HANDLE hFile, LONGLONG& FileSize);

    LONGLONG GetFileIdByPath(PUNICODE_STRING FileName);
};

