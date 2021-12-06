#include "data_structs.h"


void FastMutex::Init() {
	ExInitializeFastMutex(&_mutex);
}

void FastMutex::Lock() {
	ExAcquireFastMutex(&_mutex);
}

void FastMutex::Unlock() {
	ExReleaseFastMutex(&_mutex);
}

//---

void Event::Init()
{
	KeInitializeEvent(&_event, NotificationEvent, FALSE);
}

NTSTATUS Event::WaitForSignal(PLARGE_INTEGER timeout)
{
	return KeWaitForSingleObject(&_event, Executive, KernelMode, TRUE, timeout);
}

LONG Event::SetEvent()
{
	return KeSetEvent(&_event, 0, FALSE);
}

LONG Event::ResetEvent()
{
	return KeResetEvent(&_event);
}
