#pragma once

#include "main.h"
#include "data_structs.h"


namespace Data {
    bool AllocGlobals();

    void FreeGlobals();

    bool ContainsFile(LONGLONG fileId);

    ULONG GetFileOwner(LONGLONG fileId);

    ULONG  GetProcessOwner(ULONG pid);

    bool ContainsProcess(ULONG pid);

    bool AreSameFamily(ULONG pid1, ULONG pid2);

    t_add_status AddFile(LONGLONG fileId, ULONG parentPid);

    bool CanAddFile(ULONG parentPid);

    t_add_status AddProcess(ULONG pid, ULONG parentPid);

    t_add_status AddProcessNode(ULONG pid, LONGLONG imgFileId, t_noresp respawnProtect);

    bool IsProcessInFileOwners(ULONG pid1, LONGLONG fileId);

    int CountProcessTrees();

    bool DeleteProcess(ULONG pid);

    bool DeleteFile(LONGLONG fileId);

    size_t CopyProcessList(ULONG rootPid, void* data, size_t outBufSize);

    size_t CopyFilesList(ULONG rootPid, void* data, size_t outBufSize);

    NTSTATUS WaitForProcessDeletion(ULONG pid, PLARGE_INTEGER checkInterval);
};
