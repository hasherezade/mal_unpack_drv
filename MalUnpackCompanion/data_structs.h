#pragma once

#include <ntddk.h>

#define DRIVER_TAG 'nUM!'
#define INVALID_INDEX (-1)
#define MAX_ITEMS 1024

// Mutex locker:

template<typename TLock>
struct AutoLock {
	AutoLock(TLock& lock) : _lock(lock) {
		_lock.Lock();
	}

	~AutoLock() {
		_lock.Unlock();
	}

private:
	TLock& _lock;
};

// Mutex:

class FastMutex {
public:
	void Init();

	void Lock();
	void Unlock();

private:
	FAST_MUTEX _mutex;
};

//Event 

class Event {
public:
	void Init();

	NTSTATUS WaitForEventSet(PLARGE_INTEGER timeout);
	LONG SetEvent();
	LONG ResetEvent();

private:
	KEVENT _event;
};

///
template<typename T>
T* AllocBuffer(size_t itemsCount = 1, bool clear = true)
{
	if (itemsCount == 0) return nullptr;

	const size_t size = itemsCount * sizeof(T);
	T* buf = (T*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (clear) {
		::memset(buf, 0, size);
	}
	return buf;
}

template<typename T>
void FreeBuffer(T* Items, size_t MaxItemCount = 1, bool clear = false)
{
	if (Items == nullptr) return;

	if (MaxItemCount && clear) {
		const size_t size = MaxItemCount * sizeof(T);
		::memset(Items, 0, size);
	}
	ExFreePool(Items);
}

///
typedef enum {
	ADD_OK = 0,
	ADD_UNINITIALIZED,
	ADD_ALREADY_EXIST,
	ADD_LIMIT_EXHAUSTED,
	ADD_INVALID_ITEM,
	ADD_NO_PARENT,
	ADD_FORBIDDEN,
	ADD_STATES_COUNT
} t_add_status;


template<typename T>
struct ItemsList
{
public:
	void init()
	{
		Mutex.Init();
		ItemCount = 0;
		MaxItemCount = 0;
		Items = NULL;
	}

	bool initItems(int maxNum = MAX_ITEMS)
	{
		return _initItems(maxNum);
	}

	bool destroy()
	{
		AutoLock<FastMutex> lock(Mutex);
		return _destroyItems();
	}

	size_t copyItems(void* outBuf, size_t outBufSize)
	{
		if (!outBuf || outBufSize < sizeof(T)) {
			return 0;
		}

		AutoLock<FastMutex> lock(Mutex);
		if (!Items || !ItemCount) {
			return 0;
		}
		size_t maxItemsToCopy = outBufSize / sizeof(T);
		size_t itemsToCopy = (maxItemsToCopy > (size_t)ItemCount) ? ItemCount : maxItemsToCopy;

		const size_t size = itemsToCopy * sizeof(T);
		::memset(outBuf, 0, outBufSize);
		::memcpy(outBuf, Items, size);
		return itemsToCopy;
	}

	int countItems()
	{
		AutoLock<FastMutex> lock(Mutex);
		return ItemCount;
	}

	bool canAddItem()
	{
		AutoLock<FastMutex> lock(Mutex);
		if (Items == NULL) {
			if (!_initItems()) {
				return false;
			}
		}
		if (ItemCount >= MaxItemCount) {
			return false;
		}
		return true;
	}

	t_add_status addItem(T it)
	{
		AutoLock<FastMutex> lock(Mutex);
		if (Items == NULL) {
			if (!_initItems()) {
				return ADD_UNINITIALIZED;
			}
		}
		if (ItemCount >= MaxItemCount) {
			return ADD_LIMIT_EXHAUSTED;
		}
		if (_getItemIndex(it) != INVALID_INDEX) {
			return ADD_ALREADY_EXIST;
		}
		if (_addItemSorted(it)) {
			return ADD_OK;
		}
		return ADD_LIMIT_EXHAUSTED;
	}

	bool containsItem(T it)
	{
		AutoLock<FastMutex> lock(Mutex);
		int index = _getItemIndex(it);
		if (index != INVALID_INDEX) {
			return true;
		}
		return false;
	}

	bool deleteItem(T it)
	{
		AutoLock<FastMutex> lock(Mutex);
		int index = _getItemIndex(it);
		if (index == INVALID_INDEX) {
			return false;
		}
		if (!_shiftItemsLeft(index)) {
			return false;
		}
		if (ItemCount == 0) {
			_destroyItems();
		}
		return true;
	}

private:
	T* Items;
	int ItemCount;
	int MaxItemCount;
	FastMutex Mutex;

	bool _initItems(int maxNum = MAX_ITEMS)
	{
		if (Items) {
			return true;
		}
		ItemCount = 0;
		Items = AllocBuffer<T>(maxNum + 1);
		if (Items != NULL) {
			MaxItemCount = maxNum;
			return true;
		}
		return false;
	}

	bool _destroyItems()
	{
		if (!Items) {
			return false;
		}
		FreeBuffer<T>(Items, MaxItemCount);
		ItemCount = 0;
		MaxItemCount = 0;
		Items = NULL;
		return true;
	}

	int _getItemIndex(T it)
	{
		if (!Items || ItemCount == 0) {
			return INVALID_INDEX;
		}
		int start = 0;
		int stop = ItemCount;
		while (start < stop) {
			int mIndx = (start + stop) / 2;
			if (Items[mIndx] == it) {
				return mIndx;
			}
			if (Items[mIndx] < it) {
				start = mIndx + 1;
			}
			else if (Items[mIndx] > it) {
				stop = mIndx;
			}
		}
		return INVALID_INDEX;
	}

	int _findFirstGreater(T it, int startIndx = 0)
	{
		if (ItemCount == 0) {
			return INVALID_INDEX;
		}
		if (startIndx == INVALID_INDEX) {
			startIndx = 0;
		}
		for (int i = startIndx; i < ItemCount; i++)
		{
			if (Items[i] > it) {
				return i;
			}
		}
		return ItemCount;
	}

	bool _shiftItemsRight(int startIndx)
	{
		if (startIndx == INVALID_INDEX) {
			return false;
		}
		if (ItemCount >= MaxItemCount) {
			return false;
		}
		for (int i = ItemCount; i > startIndx; i--) {
			Items[i] = Items[i - 1];
		}
		ItemCount++;
		return true;
	}

	bool _shiftItemsLeft(int startIndx)
	{
		if (startIndx == INVALID_INDEX) {
			return false;
		}
		for (int i = startIndx + 1; i < ItemCount; i++) {
			Items[i - 1] = Items[i];
		}
		Items[ItemCount - 1] = 0;
		ItemCount--;
		return true;
	}

	bool _addItemSorted(T it)
	{
		int itIndx = _getItemIndex(it);
		int indx = _findFirstGreater(it, itIndx);
		if (indx == INVALID_INDEX) {
			Items[ItemCount++] = it;
			return true;
		}
		if (!_shiftItemsRight(indx)) {
			return false;
		}
		Items[indx] = it;
		return true;
	}
};

//---

