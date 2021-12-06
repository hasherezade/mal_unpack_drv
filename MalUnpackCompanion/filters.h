#pragma once

#include <ntifs.h>
#include <ntddk.h>

#include "data_manager.h"

OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION Info);

NTSTATUS OnRegistryNotify(PVOID context, PVOID arg1, PVOID arg2);
