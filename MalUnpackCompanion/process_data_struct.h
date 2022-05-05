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
	LONGLONG imgFile;
	ItemsList<ULONG> *processList;
	ItemsList<LONGLONG> *filesList;
	t_noresp respawnProtect;

	void _init(ULONG _pid, t_noresp _respawnProtect, LONGLONG _imgFile)
	{
		processList = NULL;
		filesList = NULL;
		rootPid = _pid;
		imgFile = _imgFile;
		respawnProtect = _respawnProtect;
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
		imgFile = FILE_INVALID_FILE_ID;
		respawnProtect = t_noresp::NORESP_NO_RESTRICTION;
	}

	bool _copy(const ProcessNode& node)
	{
		::memcpy(this, &node, sizeof(ProcessNode));
		return true;
	}

	// check if the root process terminated
	bool _isDeadNode();

	bool _isEmptyNode();

	bool _containsFile(LONGLONG fileId);

	bool _containsProcess(ULONG pid);

	bool _canAddFile();

	t_add_status _addFile(LONGLONG fileId);

	t_add_status _addProcess(ULONG pid);

	int _countProcesses();

	int _countFiles();

	bool _deleteProcess(ULONG pid);

	bool _deleteFile(LONGLONG);

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
		if (0 == parentPid) {
			return ADD_NO_PARENT;
		}
		AutoLock<FastMutex> lock(Mutex);
		return _addToExistingTree(pid, parentPid);
	}

	t_add_status AddProcessNode(ULONG pid, LONGLONG imgFile, t_noresp respawnProtect)
	{
		if (0 == pid) {
			return ADD_INVALID_ITEM;
		}
		AutoLock<FastMutex> lock(Mutex);
		if (_ContainsProcess(pid)) {
			return ADD_FORBIDDEN;
		}
		return _createNewProcessNode(pid, imgFile, respawnProtect);
	}

	bool CanAddFile(ULONG parentPid)
	{
		if (0 == parentPid) {
			return false;
		}

		AutoLock<FastMutex> lock(Mutex);
		if (_CanAddFile(parentPid) == ADD_OK) {
			return true;
		}
		return false;
	}

	inline bool _DestroyNodeIfEmpty(int i)
	{
		ProcessNode& n = Items[i];
		if (!n._isEmptyNode()) {
			return false;
		}
		n._destroy();
		//rewrite the last element on the place of the current:
		if (ItemCount > 1) {
			Items[i]._copy(Items[ItemCount - 1]);
		}
		ItemCount--;
		return true;
	}

	t_add_status AddFile(LONGLONG fileId, ULONG parentPid)
	{
		if (0 == parentPid || FILE_INVALID_FILE_ID == fileId) {
			return ADD_INVALID_ITEM;
		}

		AutoLock<FastMutex> lock(Mutex);

		t_add_status canAddStatus = _CanAddFile(parentPid);
		if (canAddStatus == ADD_NO_PARENT) {
			return ADD_INVALID_ITEM;
		}

		if (canAddStatus != ADD_OK) {
			return canAddStatus;
		}
		// if this file belongs to a dead node, delete the association first:
		const t_delete_status delStatus = _deletePreviousFileAssociation(fileId, parentPid);
		if (delStatus == DELETE_FORBIDDEN) {
			return ADD_FORBIDDEN;
		}

		// add the file to the process:
		return _addFile(fileId, parentPid);
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
		if (0 == pid) return false;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(pid)) {
				if (n._deleteProcess(pid)) {
					if (n._isDeadNode()) {
						deletionEvent.SetEvent();
					}
					_DestroyNodeIfEmpty(i);
					return true;
				}
			}
		}
		return false;
	}

	bool DeleteFile(LONGLONG fileId)
	{
		if (FILE_INVALID_FILE_ID == fileId) return false;

		AutoLock<FastMutex> lock(Mutex);

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsFile(fileId)) {
				if (n._deleteFile(fileId)) {
					_DestroyNodeIfEmpty(i);
					return true;
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
		return _ContainsProcess(pid1);
	}

	NTSTATUS WaitForProcessDeletion(ULONG pid, PLARGE_INTEGER checkInterval)
	{
		if (0 == pid) return STATUS_INVALID_PARAMETER;

		LONGLONG waitTime = (checkInterval) ? checkInterval->QuadPart : 0;
		bool isRoot = false;

		ULONG ownerPID = 0;
		while ((ownerPID = GetProcessOwner(pid)) == pid) {
			// if the given PID is a root, don't let it terminate without permission
			isRoot = true;

			DbgPrint(DRIVER_PREFIX "[%d] " __FUNCTION__ ": process requested terminate, waitTime: %zx (owner: %d, remaining children: %d)\n", pid, waitTime, pid, CountProcesses(pid));
			deletionEvent.ResetEvent();
			deletionEvent.WaitForEventSet(checkInterval);
		}
		if (isRoot) {
			DbgPrint(DRIVER_PREFIX "[%d] " __FUNCTION__ ": root process termination permitted!\n", pid, waitTime, pid);
		}

		if (ownerPID != 0) {
			// the process is still on the list, so delete it
			// this may happen in case of a child process that is terminating on its own
			DbgPrint(DRIVER_PREFIX "[%d] " __FUNCTION__ ": child process termination permitted!\n", pid, waitTime, pid);
			DeleteProcess(pid);
		}
		return STATUS_SUCCESS;
	}

private:

	ProcessNode* Items;
	int ItemCount;
	int MaxItemCount;
	FastMutex Mutex;
	Event deletionEvent;


	bool _ContainsProcess(ULONG pid1)
	{
		if (0 == pid1) return false;

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(pid1)) {
				return true;
			}
		}
		return false;
	}

	t_add_status _CanAddFile(ULONG parentPid)
	{
		if (0 == parentPid) {
			return ADD_INVALID_ITEM;
		}
		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(parentPid)) {
				if (n._canAddFile()) {
					return ADD_OK;
				}
				return ADD_LIMIT_EXHAUSTED;
			}
		}
		return ADD_NO_PARENT;
	}

	t_add_status _addFile(LONGLONG fileId, ULONG parentPid)
	{
		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			if (n._containsProcess(parentPid)) {
				return n._addFile(fileId);
			}
		}
		return ADD_INVALID_ITEM;
	}

	typedef enum {
		DELETE_OK = 0,
		DELETE_NOT_FOUND,
		DELETE_INVALID_ITEM,
		DELETE_FORBIDDEN,
		DELETE_EXCLUDED,
		DELETE_STATES_COUNT
	} t_delete_status;

	t_delete_status _deletePreviousFileAssociation(LONGLONG fileId, ULONG excludedPid)
	{
		if (FILE_INVALID_FILE_ID == fileId) {
			return DELETE_INVALID_ITEM;
		}

		for (int i = 0; i < ItemCount; i++)
		{
			ProcessNode& n = Items[i];
			// this file belongs to a dead node, delete the association first:
			if (n._containsFile(fileId)) {
				if (n._isDeadNode() && n._countProcesses() == 0) {
					n._deleteFile(fileId);
					_DestroyNodeIfEmpty(i);
					return DELETE_OK;
				}
				if (excludedPid && n._containsProcess(excludedPid)) {
					return DELETE_EXCLUDED; // file found in the excluded process, so deleting is not required
				}
				//this process tree is not dead
				return DELETE_FORBIDDEN;
			}
		}
		return DELETE_NOT_FOUND;
	}


	t_add_status _createNewProcessNode(ULONG pid, LONGLONG imgFile, t_noresp respawnProtect)
	{
		//create a new node for the process:
		ProcessNode* newItem = _getNewItemPtr();
		if (!newItem) {
			return ADD_LIMIT_EXHAUSTED;
		}

		newItem->_init(pid, respawnProtect, imgFile);

		//add root process to the list:
		const t_add_status status = newItem->_addProcess(pid);
		if (status == ADD_OK) {
			return ADD_OK;
		}
		newItem->_destroy();
		ItemCount--;
		return ADD_LIMIT_EXHAUSTED;
	}

	t_add_status _addToExistingTree(ULONG pid, ULONG parentPid)
	{
		if (0 == pid) {
			return ADD_INVALID_ITEM;
		}
		if (0 == parentPid) {
			return ADD_NO_PARENT;
		}

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
		
		// this no parent tree found for such process
		return ADD_NO_PARENT;
	}

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

