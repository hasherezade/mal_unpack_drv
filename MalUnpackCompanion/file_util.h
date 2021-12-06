#pragma once

#include "undoc_api.h"

namespace FileUtil {

    NTSTATUS FetchFileId(HANDLE hFile, LONGLONG& FileId);

    LONGLONG GetFileIdByPath(PUNICODE_STRING FileName);
};

