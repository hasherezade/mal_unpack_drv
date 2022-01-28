#include "process_data_struct.h"

void ProcessNode::_setRootFile(LONGLONG fileId)
{
	this->rootFile = fileId;
}

bool ProcessNode::_containsFile(LONGLONG fileId)
{
	if (this->rootFile != FILE_INVALID_FILE_ID 
		&& this->rootFile == fileId)
	{
		return true;
	}
	if (!filesList) return false;
	return filesList->containsItem(fileId);
}

bool ProcessNode::_isDeadNode()
{
	if (!processList) return true;
	return (processList->containsItem(rootPid)) ? false : true;
}

bool ProcessNode::_containsProcess(ULONG pid)
{
	if (!processList) return false;
	return processList->containsItem(pid);
}

bool ProcessNode::_canAddFile()
{
	if (!filesList) {
		if (!_initItems()) {
			return false;
		}
	}
	return filesList->canAddItem();
}

t_add_status ProcessNode::_addFile(LONGLONG fileId)
{
	if (!filesList) {
		if (!_initItems()) {
			return ADD_UNINITIALIZED;
		}
	}
	return filesList->addItem(fileId);
}

t_add_status ProcessNode::_addProcess(ULONG pid)
{
	if (!processList) {
		if (!_initItems()) {
			return ADD_UNINITIALIZED;
		}
	}
	if (pid != rootPid) {
		if (_isDeadNode()) {
			// the root process terminated, do not allow to add more processes to this list
			return ADD_LIMIT_EXHAUSTED;
		}
	}
	return processList->addItem(pid);
}

int ProcessNode::_countProcesses()
{
	if (!processList) return 0;
	return processList->countItems();
};


bool ProcessNode::_deleteProcess(ULONG pid)
{
	if (!processList) return false;
	return processList->deleteItem(pid);
}

size_t ProcessNode::_copyProcessList(void* data, size_t outBufSize)
{
	if (!processList) return 0;
	return processList->copyItems(data, outBufSize);
}

size_t ProcessNode::_copyFilesList(void* data, size_t outBufSize)
{
	if (!filesList) return 0;
	return filesList->copyItems(data, outBufSize);
}
