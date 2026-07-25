#pragma once

namespace RDA {

	// Just a simple stuct meant for passing info about an array from one function to another
	template<typename T>
	struct array_wrapper {
		
		array_wrapper() = default;
		array_wrapper(array_wrapper& other) : mData(other.mData), size(other.size) {};
		array_wrapper(T* pData, size_t pSize) : mData(pData) ,size(pSize) {}

		void SetData(T* pData, size_t pSize) {
			mData = pData;
			size = pSize;
		}

		size_t GetSize() { return size; }
		T* data() { return mData; }
		bool isValid() { return (mData != nullptr) && (size != 0); }

	private:
		T* mData = nullptr;
		size_t size = 0;
	};

}