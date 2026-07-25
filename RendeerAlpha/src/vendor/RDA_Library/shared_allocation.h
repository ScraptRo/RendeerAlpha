#pragma once
#include <cstdlib> // For malloc and free
#include <new>     // For placement new
#include <iostream>

namespace RDA {
	struct sh_all_header{
		uint16_t _refCount = 0;
	};

	// A minimal intrusive shared pointer.
	// The reference-count header lives immediately before the payload inside a
	// single allocation, so there is one heap block per shared object.
	template<typename T>
	struct sh_all {
		sh_all() = default;

		sh_all(const sh_all& other) : allocation(other.allocation) {
			if (allocation){
				header()->_refCount++;
			}
		};

		explicit sh_all(const T& item) {
			sh_all_header* temp = reinterpret_cast<sh_all_header*>(malloc(sizeof(sh_all_header) + sizeof(T)));
			if (!temp) throw std::bad_alloc();
			temp->_refCount = 1;
			allocation = reinterpret_cast<T*>(temp + 1);
			::new (allocation) T(item); // copy construct the payload in place
		};

		~sh_all() {
			release();
		}

		sh_all& operator=(const sh_all& other) {
			if (this != &other){
				release();
				allocation = other.allocation;
				if (allocation){
					header()->_refCount++;
				}
			}
			return *this;
		}

		// Drops this handle's reference without waiting for destruction.
		void deconnect() {
			release();
		}

		T* operator->() const { return allocation; }
		T& operator*()  const { return *allocation; }
		T* GetRawPointer() const { return allocation; }
		bool IsValid() const { return allocation != nullptr; }

	private:
		sh_all_header* header() const { return reinterpret_cast<sh_all_header*>(allocation) - 1; }

		void release() {
			if (allocation){
				sh_all_header* h = header();
				if (--h->_refCount == 0){
					allocation->~T();
					free(h);
				}
				allocation = nullptr;
			}
		}

		T* allocation = nullptr;
	};
}
