#include "data_manager.h"
#include "common.h"
#include "process_data_struct.h"
#include "process_util.h"

namespace Data {
	ProcessNodesList g_ProcessNode;
};

bool Data::AllocGlobals()
{
	g_ProcessNode.init();
	if (!g_ProcessNode.initItems()) {
		DbgPrint(DRIVER_PREFIX ": Failed to initialize data items!\n");
		return false;
	}
	return true;
}

void Data::FreeGlobals()
{
	g_ProcessNode.destroy();
}


bool Data::ContainsFile(LONGLONG fileId)
{
	return (g_ProcessNode.GetFileOwner(fileId) != 0);
}

ULONG Data::GetFileOwner(LONGLONG fileId)
{
	return g_ProcessNode.GetFileOwner(fileId);
}

ULONG Data::GetProcessOwner(ULONG pid)
{
	return g_ProcessNode.GetProcessOwner(pid);
}

bool Data::ContainsProcess(ULONG pid1)
{
	return g_ProcessNode.ContainsProcess(pid1);
}

bool Data::AreSameFamily(ULONG pid1, ULONG pid2)
{
	return g_ProcessNode.AreSameFamily(pid1, pid2);
}


bool Data::IsProcessInFileOwners(ULONG pid, LONGLONG fileId)
{
	return g_ProcessNode.IsProcessInFileOwners(pid, fileId);
}

bool Data::CanAddFile(ULONG parentPid)
{
	return g_ProcessNode.CanAddFile(parentPid);
}

t_add_status Data::AddFile(LONGLONG fileId, ULONG parentPid)
{
	return g_ProcessNode.AddFile(fileId, parentPid);
}

t_add_status Data::AddProcess(ULONG pid, ULONG parentPid)
{
	t_add_status status = g_ProcessNode.AddProcess(pid, parentPid);
	if (status == ADD_LIMIT_EXHAUSTED) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Cannot add the process: %d, terminating...\n", pid);
		ProcessUtil::TerminateProcess(pid);
	}
	return status;
}

t_add_status Data::AddProcessNode(ULONG pid, LONGLONG imgFileId, t_noresp respawnProtect)
{
	t_add_status status = g_ProcessNode.AddProcessNode(pid, imgFileId, respawnProtect);
	if (status == ADD_LIMIT_EXHAUSTED) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Cannot add the process: %d, terminating...\n", pid);
		ProcessUtil::TerminateProcess(pid);
	}
	return status;
}

t_add_status Data::AddProcessToFileOwner(ULONG PID, LONGLONG fileId)
{
	t_add_status status = g_ProcessNode.AddProcessToFileOwner(PID, fileId);
	if (status == ADD_LIMIT_EXHAUSTED) {
		DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Cannot add the process: %d, terminating...\n", PID);
		ProcessUtil::TerminateProcess(PID);
	}
	return status;
}

int Data::CountProcessTrees()
{
	return g_ProcessNode.CountNodes();
}

bool Data::DeleteProcess(ULONG pid)
{
	bool isOk = g_ProcessNode.DeleteProcess(pid);
	DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Watched nodes: %d\n", g_ProcessNode.CountNodes());
	return isOk;
}

bool Data::DeleteFile(LONGLONG fileId)
{
	bool isOk = g_ProcessNode.DeleteFile(fileId);
	DbgPrint(DRIVER_PREFIX __FUNCTION__ ": Watched nodes: %d\n", g_ProcessNode.CountNodes());
	return isOk;
}

size_t Data::CopyProcessList(ULONG parentPid, void* data, size_t outBufSize)
{
	return g_ProcessNode.CopyProcessList(parentPid, data, outBufSize);
}

size_t Data::CopyFilesList(ULONG parentPid, void* data, size_t outBufSize)
{
	return g_ProcessNode.CopyFilesList(parentPid, data, outBufSize);
}

NTSTATUS Data::WaitForProcessDeletion(ULONG pid, PLARGE_INTEGER checkInterval)
{
	return g_ProcessNode.WaitForProcessDeletion(pid, checkInterval);
}
