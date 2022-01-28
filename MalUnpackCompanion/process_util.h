#pragma once

#include "undoc_api.h"

namespace ProcessUtil {

    bool CheckProcessPath(const PEPROCESS Process, PWCH supportedName);

    bool ShowProcessPath(const PEPROCESS Process);

    ULONGLONG GetProcessFileId(const PEPROCESS Process);

    ULONG GetProcessParentPID(const PEPROCESS Process);

    NTSTATUS TerminateProcess(ULONG PID);
};

