#pragma once

#include "undoc_api.h"

namespace FileUtil {

    NTSTATUS FetchFileId(HANDLE hFile, LONGLONG& FileId);

    NTSTATUS GetFileSize(HANDLE hFile, LONGLONG& FileSize);

    LONGLONG GetFileIdByPath(PUNICODE_STRING FileName);
};

