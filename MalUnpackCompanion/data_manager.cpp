#include "data_manager.h"
#include "common.h"
#include "process_data_struct.h"
#include "process_util.h"

namespace Data {
	ProcessNodesList g_ProcessNodes;
};

bool Data::AllocGlobals()
{
	g_ProcessNodes.init();
	if (!g_ProcessNodes.initItems()) {
		DbgPrint(DRIVER_PREFIX ": Failed to initialize data items!\n");
		return false;
	}
	return true;
}

void Data::FreeGlobals()
{
	g_ProcessNodes.destroy();
}

bool Data::ContainsFile(LONGLONG fileId)
{
	return (g_ProcessNodes.GetFileOwner(fileId) != 0);
}

ULONG Data::GetFileOwner(LONGLONG fileId)
{
	return g_ProcessNodes.GetFileOwner(fileId);
}

ULONG Data::GetProcessOwner(ULONG pid)
{
	return g_ProcessNodes.GetProcessOwner(pid);
}

bool Data::ContainsProcess(ULONG pid1)
{
	return g_ProcessNodes.ContainsProcess(pid1);
}

bool Data::AreSameFamily(ULONG pid1, ULONG pid2)
{
	return g_ProcessNodes.AreSameFamily(pid1, pid2);
}


bool Data::IsProcessInFileOwners(ULONG pid, LONGLONG fileId)
{
	return g_ProcessNodes.IsProcessInFileOwners(pid, fileId);
}

bool Data::CanAddFile(ULONG parentPid)
{
	return g_ProcessNodes.CanAddFile(parentPid);
}

t_add_status Data::AddFile(LONGLONG fileId, ULONG parentPid)
{
	return g_ProcessNodes.AddFile(fileId, parentPid);
}

t_add_status Data::AddProcess(ULONG pid, ULONG parentPid)
{
	t_add_status status = g_ProcessNodes.AddProcess(pid, parentPid);
	if (status == ADD_LIMIT_EXHAUSTED) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Cannot add the process: %d, terminating...\n", pid);
		ProcessUtil::TerminateProcess(pid);
	}
	return status;
}

t_add_status Data::AddProcessNode(ULONG pid, LONGLONG imgFileId, t_noresp respawnProtect)
{
	t_add_status status = g_ProcessNodes.AddProcessNode(pid, imgFileId, respawnProtect);
	if (status == ADD_LIMIT_EXHAUSTED) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Cannot add the process: %d, terminating...\n", pid);
		ProcessUtil::TerminateProcess(pid);
	}
	return status;
}

int Data::CountProcessTrees()
{
	return g_ProcessNodes.CountNodes();
}

bool Data::DeleteProcess(ULONG pid)
{
	bool isOk = g_ProcessNodes.DeleteProcess(pid);
	DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Watched nodes: %d\n", g_ProcessNodes.CountNodes());
	return isOk;
}

bool Data::DeleteFile(LONGLONG fileId)
{
	bool isOk = g_ProcessNodes.DeleteFile(fileId);
	DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Watched nodes: %d\n", g_ProcessNodes.CountNodes());
	return isOk;
}

size_t Data::CopyProcessList(ULONG parentPid, void* data, size_t outBufSize)
{
	return g_ProcessNodes.CopyProcessList(parentPid, data, outBufSize);
}

size_t Data::CopyFilesList(ULONG parentPid, void* data, size_t outBufSize)
{
	return g_ProcessNodes.CopyFilesList(parentPid, data, outBufSize);
}

NTSTATUS Data::WaitForProcessDeletion(ULONG pid, PLARGE_INTEGER checkInterval)
{
	return g_ProcessNodes.WaitForProcessDeletion(pid, checkInterval);
}
