#pragma once
#include "data_structs.h"
#include "common.h"

#ifndef FILE_INVALID_FILE_ID
	#define FILE_INVALID_FILE_ID               ((LONGLONG)-1LL) 
#endif


struct ProcessNode
{
	friend struct ProcessNodesList;

protected:
	ULONG rootPid;
	ItemsList<ULONG> *processList;
	ItemsList<LONGLONG> *filesList;

	void _init(ULONG _pid)
	{
		processList = NULL;
		filesList = NULL;
		rootPid = _pid;
	}

	bool _initItems()
	{
		if (!processList) {
			processList = AllocBuffer<ItemsList<ULONG> >();
			if (!processList) {
				_destroy();
				DbgPrint(DRIVER_PREFIX "Failed to initialize processList!\n");
				return false;
			}
		}
		if (!filesList) {
			filesList = AllocBuffer<ItemsList<LONGLONG> >();
			if (!filesList) {
				_destroy();
				DbgPrint(DRIVER_PREFIX "Failed to initialize filesList!\n");
				return false;
			}
		}
		processList->init();
		if (!processList->initItems()) {
			DbgPrint(DRIVER_PREFIX "Failed to initialize processList items!\n");
			_destroy();
			return false;
		}
		filesList->init();
		if (!filesList->initItems()) {
			DbgPrint(DRIVER_PREFIX "Failed to initialize filesList items!\n");
			_destroy();
			return false;
		}
		DbgPrint(DRIVER_PREFIX "ProcessNode: initialized lists!\n");
		return true;
	}

	void _destroy()
	{
		if (processList) {
			processList->destroy();
			FreeBuffer(processList);
			processList = NULL;
		}
		if (filesList) {
			filesList->destroy();
			FreeBuffer(filesList);
			filesList = NULL;
		}
		rootPid = 0;
	}

	bool _copy(const ProcessNode& node)
	{
		rootPid = node.rootPid;
		processList = node.processList;
		filesList = node.filesList;
		return true;
	}

	// check it the root process terminated
	bool _isDeadNode();

	bool _containsFile(LONGLONG fileId);

	bool _containsProcess(ULONG pid);

	t_add_status _addFile(LONGLONG fileId);

	t_add_status _addProcess(ULONG pid);

	int _countProcesses();

	bool _deleteProcess(ULONG pid);

	size_t _copyProcessList(void* data, size_t outBufSize);

	size_t _copyFilesList(void* data, size_t outBufSize);

};

//---

struct ProcessNodesList
{
public:
	void init()
	{
		Items = 0;
		MaxItemCount = 0;
		ItemCount = 0;
		Mutex.Init();
		deletionEvent.Init();
	}

	bool initItems(int maxNum = MAX_ITEMS)
	{
		AutoLock<FastMutex> lock(Mutex);
		if (Items) {
			return true;
		}
		ItemCount = 0;
		Items = AllocBuffer<ProcessNode>(maxNum + 1);
		if (Items != NULL) {
			MaxItemCount = maxNum;
			return true;
		}
		return false;
	}

	bool destroy()
	{
		AutoLock<FastMutex> lock(Mutex);
		if (Items) {
			_destroyItems();
			FreeBuffer<ProcessNode>(Items, MaxItemCount);
			ItemCount = 0;
			MaxItemCount = 0;
			Items = NULL;
			return true;
		}
		return false;
	}

	t_add_status AddProcess(ULONG pid, ULONG parentPid)
	{
		if (0 == pid) {
			return ADD_INVALID_ITEM;
		}
		//Allow for the parentPid == 0: it means a new root node
		
		AutoLock<FastMutex> lock(Mutex);
		if (parentPid != 0) {
			for (int i = 0; i < ItemCount; i++)
			{
				ProcessNode& n = Items[i];
				if (n.rootPid == parentPid) {
					return n._addProcess(pid);
				}
			}

			for (int i = 0; i < ItemCount; i++)
			{
				ProcessNode& n = Items[i];
				if (n._containsProcess(parentPid)) {
					return n._addProcess(pid);
				}
			}
		}

		//create a new node for the process:
		ProcessNode* newItem = _getNewItemPtr();
		if (!newItem) {
			DbgPrint(DRIVER_PREFIX __FUNCTION__ "Could not get a new node, failed to add pid: %d!\n", pid);
			return ADD_LIMIT_EXHAUSTED;
		}
		DbgPrint(DRIVER_PREFIX "Adding new root node: %d!\n", pid);
		newItem->_init(pid);
		if (newItem->_addProcess(pid) == ADD_OK) {
			return ADD_OK;
		}
		DbgPrint(DRIVER_PREFIX "Failed to add the node: %d!\n", pid);
		newItem->_destroy();
		ItemCount--;
		return ADD_LIMIT_EXHAUSTED;
	}


	t_add_status AddFile(LONGLONG fileId, ULONG parentPid)
	{
		if (0 == parentPid || FILE_INVALID_FILE_ID == fileId) {
			return ADD_INVALID_ITEM;
		}

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(parentPid)) {
				return n._addFile(fileId);
			}
		}
		return ADD_INVALID_ITEM;
	}

	t_add_status AddProcessToFileOwner(ULONG PID, LONGLONG fileId)
	{
		if (0 == PID || FILE_INVALID_FILE_ID == fileId) {
			return ADD_INVALID_ITEM;
		}

		AutoLock<FastMutex> lock(Mutex);

		t_add_status status = ADD_INVALID_ITEM;
		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsFile(fileId)) {
				status = n._addProcess(PID);
				break;
			}
		}
		return status;
	}

	bool IsProcessInFileOwners(ULONG PID, LONGLONG fileId)
	{
		if (0 == PID || FILE_INVALID_FILE_ID == fileId) {
			return false;
		}

		AutoLock<FastMutex> lock(Mutex);
		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsFile(fileId)) {
				if (n._containsProcess(PID)) {
					return true;
				}
				return false;
			}
		}
		return false;
	}

	bool DeleteProcess(ULONG pid)
	{
		if (0 == pid) return 0;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(pid)) {
				if (n._deleteProcess(pid)) {
					if (n._countProcesses() == 0) {
						n._destroy();
						//rewrite the last element on the place of the current:
						if (ItemCount > 1) {
							Items[i]._copy(Items[ItemCount - 1]);
						}
						ItemCount--;
						deletionEvent.SetEvent();
					}
				}
			}
		}
		return false;
	}

	size_t CopyProcessList(ULONG parentPid, void* data, size_t outBufSize)
	{
		if (0 == parentPid) return 0;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n.rootPid == parentPid) {
				return n._copyProcessList(data, outBufSize);
			}
		}
		return 0;
	}

	size_t CopyFilesList(ULONG parentPid, void* data, size_t outBufSize)
	{
		if (0 == parentPid) return 0;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n.rootPid == parentPid) {
				return n._copyFilesList(data, outBufSize);
			}
		}
		return 0;
	}

	int CountProcesses(ULONG parentPid)
	{
		if (0 == parentPid) return 0;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n.rootPid == parentPid) {
				return n._countProcesses();
			}
		}
		return 0;
	}

	int CountNodes()
	{
		AutoLock<FastMutex> lock(Mutex);
		return ItemCount;
	}

	ULONG GetFileOwner(LONGLONG fileId)
	{
		if (FILE_INVALID_FILE_ID == fileId) return 0;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsFile(fileId)) {
				return n.rootPid;
			}
		}
		return 0;
	}

	ULONG GetProcessOwner(ULONG pid)
	{
		if (0 == pid) return 0;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(pid)) {
				return n.rootPid;
			}
		}
		return 0;
	}

	bool AreSameFamily(ULONG pid1, ULONG pid2)
	{
		if (pid1 == 0 || pid2 == 0) {
			return false;
		}

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(pid1)) {
				if (n._containsProcess(pid2)) {
					return true;
				}
				else {
					return false;
				}
			}
		}
		return false;
	}

	bool ContainsProcess(ULONG pid1)
	{
		if (0 == pid1) return false;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(pid1)) {
				return true;
			}
		}
		return false;
	}

	NTSTATUS WaitForProcessDeletion(ULONG pid, PLARGE_INTEGER checkInterval)
	{
		if (0 == pid) return STATUS_SUCCESS;

		ULONG ownerPID = 0;
		LONGLONG waitTime = (checkInterval) ? checkInterval->QuadPart : 0;
		bool isMine = false;
		while ((ownerPID = GetProcessOwner(pid)) != 0) {
			isMine = true;

			DbgPrint(DRIVER_PREFIX "[%d] " __FUNCTION__ ": process requested terminate, waitTime: %zx (owner: %d, remaining children: %d)\n", pid, waitTime, ownerPID, CountProcesses(ownerPID));
			deletionEvent.ResetEvent();
			deletionEvent.WaitForEventSet(checkInterval);
		}
		if (isMine) {
			DbgPrint(DRIVER_PREFIX "[%d] " __FUNCTION__ ": process termination permitted!\n", pid, waitTime, ownerPID);
		}
		return STATUS_SUCCESS;
	}

private:

	ProcessNode* Items;
	int ItemCount;
	int MaxItemCount;
	FastMutex Mutex;
	Event deletionEvent;

	bool _destroyItems()
	{
		if (!Items) return false;

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			n._destroy();
		}
		deletionEvent.SetEvent();
		return true;
	}

	ProcessNode* _getNewItemPtr()
	{
		if (ItemCount >= MaxItemCount) {
			return nullptr;
		}
		ProcessNode* item = &Items[ItemCount];
		ItemCount++;
		return item;
	}

};

