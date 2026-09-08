#pragma once
#include <cstdlib> // For malloc and free
#include <iostream>

namespace RDA {

    template<typename T>
    struct obj_list; // Forward declaration



    // Header of the data chunk allocated by the object
    template<typename T>
    struct obj_element{
        T* forward = nullptr;
        T* backward = nullptr;
        obj_list<T>* owner = nullptr;
        int32_t refCount = 0;
    };

    template<typename T>
    struct OBJ_FULL_STRUCT {
        obj_element<T> el;
        T obj;
        OBJ_FULL_STRUCT() = default;
    };

    // A pool block: either a free-list link, or storage for a live object.
    //
    // The ctor/dtor are user-provided and empty on purpose. If T has a non-trivial
    // constructor or destructor (which every GPU resource does), the compiler would
    // otherwise delete this union's, making the pool unusable for exactly the types
    // it exists to hold. Lifetime here is manual by design: blocks are malloc'd,
    // objects are placement-new'd, and destroyed explicitly via ~T().
    template<typename T>
    union OBJ_POOL_UNION {
        OBJ_POOL_UNION* next;
        OBJ_FULL_STRUCT<T> str;
        OBJ_POOL_UNION() {}
        ~OBJ_POOL_UNION() {}
        OBJ_POOL_UNION(OBJ_POOL_UNION* other) : next(other) {};
    };

    // The struct that is passed(the shared_ptr)
    template<typename T>
    struct obj_ref {
        obj_ref() = default;
        obj_ref(T* obj) : object(obj) {
            if (object) {
                obj_element<T>* el = reinterpret_cast<obj_element<T>*>(obj) - 1;
                el->owner->incRefCount(obj);
            }
        }
        obj_ref(const obj_ref& other) : object(other.object) {
            if (object) {
                obj_element<T>* el = reinterpret_cast<obj_element<T>*>(other.object) - 1;
                el->owner->incRefCount(other.object);
            }
        };
        ~obj_ref() {
            if (object && obj_list<T>::canDestroyObject()) {
                obj_element<T>* el = reinterpret_cast<obj_element<T>*>(object) - 1;
                el->owner->decRefCount(object);
                object = nullptr;
            }
        }

        obj_ref& operator=(const obj_ref& other) {
            if (this != &other) {
                if (object) {
                    obj_element<T>* el = reinterpret_cast<obj_element<T>*>(object) - 1;
                    el->owner->decRefCount(object);
                }
                object = other.object;
                if (object) {
                    obj_element<T>* el = reinterpret_cast<obj_element<T>*>(object) - 1;
                    el->owner->incRefCount(object);
                }
            }
            return *this;
        }

		T* operator->() const { return object; }
        T& operator*()  const { return *object; }

        T* GetRawPointer() const { return object; }
        bool IsValid() const { return object != nullptr; };

        friend bool operator>(const obj_ref<T>& a, const obj_ref<T> b) { return a.object > b.object; };
        friend bool operator<(const obj_ref<T>& a, const obj_ref<T> b) { return a.object < b.object; };
        friend bool operator!=(const obj_ref<T>& a, const obj_ref<T> b) { return a.object != b.object; };
        friend bool operator>=(const obj_ref<T>& a, const obj_ref<T> b) { return a.object >= b.object; };
        friend bool operator<=(const obj_ref<T>& a, const obj_ref<T> b) { return a.object <= b.object; };

    private:
        friend struct obj_list<T>; // Grant access to obj_list
        T* object = nullptr;
    };
    // A combination of a linked list and shared pointers
    //
    // This struct let you handle objects allocated on heap using the object reference
    // An use case would be the mesh handling, where you create one object that handles a buffer on the GPU and then use
    // the mesh reference for accesing that block of memory
    // It also have a garbage collector, so the memory its automaticaly deallocated when there are no other reference to it
    // Note: Minimum size of an object is 64 bytes. This is done because of how the allocation works
    template<typename T>
    struct obj_list {
        // The main way of iterating through the entire list
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = obj_ref<T>;
            using pointer = obj_ref<T>*;  // or also value_type*
            using reference = obj_ref<T>&;  // or also value_type&

            Iterator(value_type ref) : m_ref(ref) {}

            reference operator*() { return m_ref; }
            pointer operator->() { return &m_ref; }

            // Prefix increment
            Iterator& operator++() {
                if (m_ref.IsValid()) {
                    obj_element<T>* el = reinterpret_cast<obj_element<T>*>(m_ref.GetRawPointer()) - 1;
                    m_ref = el->forward;
                }
                return *this;
            }

            // Postfix increment
            Iterator operator++(int) {
                Iterator tmp = *this; ++(*this); return tmp;
            }

            friend bool operator== (const Iterator& a, const Iterator& b) { return a.m_ref == b.m_ref; };
            friend bool operator!= (const Iterator& a, const Iterator& b) { return a.m_ref != b.m_ref; };

        private:
            value_type m_ref;
        };
        // Used for dealocating objects before a specific deinitialization(something like in an API library)
        static bool canDestroyObject() {
            return maintainAlive;
        }
        obj_list() = default;
        obj_list(size_t pSize) : _capacity(pSize), _poolSize(pSize) {
            // A size of 0 would underflow the loop bound below; treat it as "no pool"
            // and fall back to per-object allocation.
            if (pSize == 0) {
                _poolSize = 0;
                _capacity = 0;
                return;
            }
            _pool = reinterpret_cast<OBJ_POOL_UNION<T>*>(malloc(pSize * sizeof(OBJ_POOL_UNION<T>)));
            if (!_pool) throw std::bad_alloc();
            for (size_t i = 0; i < _poolSize - 1; i++){
                _pool[i].next = &_pool[i + 1];
            }
            _pool[_poolSize - 1].next = nullptr;
            _freeBlock = _pool;
        }
        ~obj_list() {
            if (maintainAlive){
                erase_all();
            }
            if (_pool){
                free(_pool);
            }
        }
        // The addObject function only allocates the datachunk for the object
        // It doesnt call the constructor for it, nor set the "default" values
        obj_ref<T> addObject() {
            obj_element<T>* alP = nullptr;
            if (_capacity){
                 alP = insert();
            }
            else{
                // Allocatating the memory block
                // Allocate a block of memory for the object + ref counter + linked list pointers
                std::cout << "[OBJ_REF]->Allocation Made\n";
                alP = static_cast<obj_element<T>*>(malloc(sizeof(obj_element<T>) + sizeof(T)));
            }
            if (!alP) throw std::bad_alloc();
            // Setting the default header
            *alP = obj_element<T>();
            alP->owner = this;
            // If there is already elements in the list
            if (_list_start) {
                obj_element<T>* nextEl = reinterpret_cast<obj_element<T>*>(_list_start) - 1;
                nextEl->backward = reinterpret_cast<T*>(alP + 1);
                alP->forward = reinterpret_cast<T*>(nextEl + 1);
            }
            // Setting the list start to the new object
            _list_start = reinterpret_cast<T*>(alP + 1);
            _list_size++;
            return obj_ref<T>(_list_start);
        }
        // Allocates memory for the object + the size specified
        obj_ref<T> addObject(size_t p_size) {
            std::cout << "[OBJ_REF]->Allocation Made\n";
            // Allocatating the memory block
            // Allocate a block of memory for the object header + the object + the extra trailing bytes
            obj_element<T>* alP = static_cast<obj_element<T>*>(malloc(sizeof(obj_element<T>) + sizeof(T) + p_size));
            if (!alP) throw std::bad_alloc();
            // Setting the default header
            *alP = obj_element<T>();
            alP->owner = this;
            // If there is already elements in the list
            if (_list_start) {
                obj_element<T>* nextEl = reinterpret_cast<obj_element<T>*>(_list_start) - 1;
                nextEl->backward = reinterpret_cast<T*>(alP + 1);
                alP->forward = reinterpret_cast<T*>(nextEl + 1);
            }
            // Setting the list start to the new object
            _list_start = reinterpret_cast<T*>(alP + 1);
            _list_size++;
            return obj_ref<T>(_list_start);
        }
        // Will empty the entire list, without verifing if the list still have references
        // This is for ending the program lifetime
        // Beacause this list is made for objects that needs to deconstruct before an library its unitialized
        // it have a global variable that says whether the constructors should delete the object or not
        void erase_all() {
            maintainAlive = false;
            T* i = _list_start;
            while (i) {
                obj_element<T>* j = reinterpret_cast<obj_element<T>*>(i) - 1;
                i->~T();
                i = j->forward;

                if (!ptr_in_range(reinterpret_cast<char*>(j), _poolSize * sizeof(OBJ_POOL_UNION<T>), reinterpret_cast<char*>(_pool))) {
                    free(j);
                }
            }
            // Leave the list empty rather than pointing at destroyed objects, so a
            // second erase_all() (or the destructor) is a no-op instead of a crash.
            _list_start = nullptr;
            _list_size = 0;
        }

        obj_ref<T> getListStart() {
            return _list_start;
        }
        size_t getListSize() {
            return _list_size;
        }
        Iterator begin() { return Iterator(obj_ref<T>(_list_start)); }
        Iterator end() { return Iterator(nullptr); }
        Iterator makeIterator(obj_ref<T>& ref) { return Iterator(ref); }

    private:
        friend obj_ref<T>;
        friend obj_element<T>;
        static bool maintainAlive;

        // List related
        T* _list_start = nullptr;
        size_t _list_size = 0;

        // Pool Related

        OBJ_POOL_UNION<T>* _pool = nullptr;
        OBJ_POOL_UNION<T>* _freeBlock = nullptr;
        size_t _capacity = 0;
        size_t _poolSize = 0;

        // The ref counter lives in the header, not immediately before the object:
        // obj_element is padded out to a multiple of its alignment, so the bytes
        // right before the payload are padding. Always go through the header.
        void incRefCount(T* obj) {
            obj_element<T>* header = reinterpret_cast<obj_element<T>*>(obj) - 1;
            header->refCount++;
        }

        void decRefCount(T* obj) {
            if (obj) {
                obj_element<T>* ref = reinterpret_cast<obj_element<T>*>(obj) - 1;
                ref->refCount--;
                // Deletion Function
                if (ref->refCount < 1) {
                    std::cout << "Obj deleted\n";
                    // Get object header
                    obj_element<T>* objHeader = reinterpret_cast<obj_element<T>*>(obj) - 1;

                    if (objHeader->backward) {
                        obj_element<T>* el = reinterpret_cast<obj_element<T>*>(objHeader->backward) - 1;
                        el->forward = objHeader->forward;
                    }
                    else {
                        // If there is no backward, that also means this element is first in the list
                        _list_start = objHeader->forward;
                    }
                    if (objHeader->forward) {
                        obj_element<T>* el = reinterpret_cast<obj_element<T>*>(objHeader->forward) - 1;
                        el->backward = objHeader->backward;
                    }
                    obj->~T();
                    _list_size--;
                    if (ptr_in_range(reinterpret_cast<char*>(objHeader), _poolSize * sizeof(OBJ_POOL_UNION<T>), reinterpret_cast<char*>(_pool))) {
                        pop(objHeader);
                    }
                    else{
                        std::cout << "[OBJ_REF]->Deallocation Made\n";
                        free(objHeader);
                    }
                }
            }
        }

        obj_element<T>* insert() {
            OBJ_POOL_UNION<T>* current_block = _freeBlock;
            _freeBlock = _freeBlock->next;
            _capacity--;
            return reinterpret_cast<obj_element<T>*>(current_block);
        }

        void pop(obj_element<T>* objHeader) {
            OBJ_POOL_UNION<T>* helper = reinterpret_cast<OBJ_POOL_UNION<T>*>(objHeader);
            helper->next = _freeBlock;
            _freeBlock = helper;
            _capacity++;
        }

        bool ptr_in_range(char* ptr, size_t size, char* arr) {
            return ptr >= arr && ptr < arr + size;
        }

    };

    template<typename T>
    bool obj_list<T>::maintainAlive = true;
}