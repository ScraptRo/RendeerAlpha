#pragma once
#include <cstdlib>
#include <iostream>

namespace RDA {
	// This list makes that every struct can be added to a list and then iterate through it
	// You can consider a disadvantage the fact that it's 'weakly typed'. By that i mean the fact that you can
	// add a struct B to a list of struct A's

	// Example:
	//	struct SomeStruct {
	//		RDA::stack_element mNode;
	//		int someData = 0;
	//	};
	//	int main() {
	//		RDA::stack_list anchor;
	//		SomeStruct m1;
	//		SomeStruct m2;
	//		m2 = m1;
	//		m1.someData = 1;
	//		m2.someData = 2;
	//		anchor.addNode(&m1.mNode);
	//		anchor.addNode(&m2.mNode);
	//		for (auto& el : anchor) {
	//			std::cout << el.getBody<SomeStruct, &SomeStruct::mNode>()->someData << '\n';
	//		}
	//	}
	struct stack_element {
		stack_element* next = nullptr;
		stack_element* prev = nullptr;

		stack_element() = default;

		stack_element(stack_element& other) {
			if (other.next) {
				other.next->prev = this;
			}
			next = other.next;
			other.next = this;
			prev = &other;
		}
		~stack_element() {
			if (next) {
				next->prev = prev;
			}
			if (prev) {
				prev->next = next;
			}
			next = nullptr;
			prev = nullptr;
		}

		stack_element& operator=(stack_element& other) {
			// Remove old reference
			if (next) {
				next->prev = prev;
			}
			if (prev) {
				prev->next = next;
			}
			// Insert in the new
			if (other.next) {
				other.next->prev = this;
			}
			next = other.next;
			other.next = this;
			prev = &other;
			return *this;
		}

		// Operator overloads for pointer equality
		bool operator==(const stack_element& other) const {
			return this == &other;
		}
		bool operator!=(const stack_element& other) const {
			return this != &other;
		}

		template<typename T, stack_element T::* Member>
		T* getBody() {
			std::ptrdiff_t offset = reinterpret_cast<std::ptrdiff_t>(&(static_cast<T*>(nullptr)->*Member));
			return reinterpret_cast<T*>(reinterpret_cast<char*>(this) - offset);
		}
	};

	struct stack_list {
		// Insert a node(or list)
		void addNode(stack_element* node) {
			if (!node) return;
			if (dummy.next) {
				dummy.next->prev = node;
			}
			node->next = dummy.next;
			node->prev = &dummy;
			dummy.next = node;
		}

		struct Iterator {
			using iterator_category = std::forward_iterator_tag;
			using difference_type = std::ptrdiff_t;
			using value_type = stack_element;
			using pointer = stack_element*;
			using reference = stack_element&;

			Iterator(pointer ref) : m_ref(ref) {}

			reference operator*() { return *m_ref; }
			pointer operator->() { return m_ref; }

			// Prefix increment
			Iterator& operator++() {
				if (m_ref) {
					m_ref = m_ref->next;
				}
				return *this;
			}

			// Postfix increment
			Iterator operator++(int) {
				Iterator tmp = *this; ++(*this); return tmp;
			}

			friend bool operator== (const Iterator& a, const Iterator& b) { return a.m_ref == b.m_ref; }
			friend bool operator!= (const Iterator& a, const Iterator& b) { return a.m_ref != b.m_ref; }

		private:
			pointer m_ref;
		};
		Iterator begin() { return Iterator(dummy.next); }
		Iterator end() { return Iterator(nullptr); }

	private:
		stack_element dummy;
	};

}