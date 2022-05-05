#pragma once

namespace Util {

	inline bool hasSuffix(UNICODE_STRING* targetStr, PWCH searchedSuffix)
	{
		if (!searchedSuffix || !targetStr || !targetStr->Length) {
			return false;
		}
		bool suffixFound = false;
		const wchar_t* found = ::wcsstr(targetStr->Buffer, searchedSuffix);
		while (found != nullptr) {
			if (::wcscmp(found, searchedSuffix) == 0) {
				suffixFound = true;
				break;
			}
		}
		return suffixFound;
	}

};
