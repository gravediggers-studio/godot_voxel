#ifndef ZN_SPAN_H
#define ZN_SPAN_H

#include "../errors.h"
#include <algorithm>
#include <array>
#include <cstddef>

namespace zylann {

// View into an array, referencing a pointer and a size.
// STL equivalent would be std::span<T> in C++20
template <typename T>
class [[nodiscard]] Span {
public:
	inline Span() : _ptr(nullptr), _size(0) {}

	inline Span(T *p_ptr, size_t p_begin, size_t p_end) {
		ZN_ASSERT(p_end >= p_begin);
		_ptr = p_ptr + p_begin;
		_size = p_end - p_begin;
	}

	inline Span(T *p_ptr, size_t p_size) : _ptr(p_ptr), _size(p_size) {}

	inline Span(Span<T> &p_other, size_t p_begin, size_t p_end) {
		ZN_ASSERT(p_end >= p_begin);
		ZN_ASSERT(p_begin < p_other.size());
		ZN_ASSERT(p_end <= p_other.size()); // `<=` because p_end is typically `p_begin + size`
		_ptr = p_other._ptr + p_begin;
		_size = p_end - p_begin;
	}

	// Initially added to support `Span<const T> = Span<T>`, or `Span<Base> = Span<Derived>`
	template <typename U>
	inline Span(Span<U> p_other) {
		_ptr = p_other.data();
		_size = p_other.size();
	}

	// Had to add this because somehow calling `template_func_expecting_span_const_t(Span<T>)` does not work...
	// Curiously enough, std::span has the same issue. I wonder what I'm missing.
	inline Span<const T> to_const() const {
		return Span<const T>(*this);
	}

	inline Span<T> sub(size_t from, size_t len) const {
		ZN_ASSERT(from + len <= _size);
		return Span<T>(_ptr + from, len);
	}

	inline Span<T> sub(size_t from) const {
		ZN_ASSERT(from <= _size);
		return Span<T>(_ptr + from, _size - from);
	}

	// Reinterprets the data as a span of a different type. The returned span may have a different number of elements,
	// but the memory area must be the same number of bytes.
	template <typename U>
	Span<U> reinterpret_cast_to() const {
		const size_t size_in_bytes = _size * sizeof(T);
#ifdef DEBUG_ENABLED
		ZN_ASSERT(size_in_bytes % sizeof(U) == 0);
#endif
		return Span<U>(reinterpret_cast<U *>(_ptr), 0, size_in_bytes / sizeof(U));
	}

	inline void set(size_t i, T v) {
#ifdef DEBUG_ENABLED
		ZN_ASSERT(i < _size);
#endif
		_ptr[i] = v;
	}

	inline T &operator[](size_t i) {
#ifdef DEBUG_ENABLED
		ZN_ASSERT(i < _size);
#endif
		return _ptr[i];
	}

	inline const T &operator[](size_t i) const {
#ifdef DEBUG_ENABLED
		ZN_ASSERT(i < _size);
#endif
		return _ptr[i];
	}

	inline size_t size() const {
		return _size;
	}

	inline T *data() {
		return _ptr;
	}

	inline const T *data() const {
		return _ptr;
	}

	inline void fill(const T v) {
		const T *end = _ptr + _size;
		for (T *p = _ptr; p != end; ++p) {
			*p = v;
		}
	}

	// Template because T could be const and TDst should not be
	template <typename TDst>
	inline void copy_to(Span<TDst> other) const {
		ZN_ASSERT(other.size() == _size);
		if (_size == 0) {
			return;
		}
		ZN_ASSERT(other.data() != nullptr);
		// for (size_t i = 0; i < _size; ++i) {
		// 	other._ptr[i] = _ptr[i];
		// }
		// Should compile to memcpy if T is simple enough
		std::copy(_ptr, _ptr + _size, other.data());
	}

	inline bool overlaps(const Span<T> other) const {
		return _ptr + _size > other._ptr && _ptr < other._ptr + other._size;
	}

	class Iterator {
	public:
		Iterator(T *p_ptr) : _elem_ptr(p_ptr) {}
		Iterator(const Iterator &p_it) : _elem_ptr(p_it._elem_ptr) {}

		inline T &operator*() const {
			return *_elem_ptr;
		}

		inline T *operator->() const {
			return _elem_ptr;
		}

		inline Iterator &operator++() {
			_elem_ptr++;
			return *this;
		}

		inline Iterator &operator--() {
			_elem_ptr--;
			return *this;
		}

		inline bool operator==(const Iterator &b) const {
			return _elem_ptr == b._elem_ptr;
		}

		inline bool operator!=(const Iterator &b) const {
			return _elem_ptr != b._elem_ptr;
		}

	private:
		T *_elem_ptr = nullptr;
	};

	class ConstIterator {
	public:
		ConstIterator(const T *p_ptr) : _elem_ptr(p_ptr) {}
		ConstIterator(const ConstIterator &p_it) : _elem_ptr(p_it._elem_ptr) {}

		inline const T &operator*() const {
			return *_elem_ptr;
		}

		inline const T *operator->() const {
			return _elem_ptr;
		}

		inline ConstIterator &operator++() {
			_elem_ptr++;
			return *this;
		}

		inline ConstIterator &operator--() {
			_elem_ptr--;
			return *this;
		}

		inline bool operator==(const ConstIterator &b) const {
			return _elem_ptr == b._elem_ptr;
		}

		inline bool operator!=(const ConstIterator &b) const {
			return _elem_ptr != b._elem_ptr;
		}

	private:
		const T *_elem_ptr = nullptr;
	};

	inline Iterator begin() {
		return Iterator(_ptr);
	}

	inline Iterator end() {
		return Iterator(_ptr + _size);
	}

	inline ConstIterator begin() const {
		return ConstIterator(_ptr);
	}

	inline ConstIterator end() const {
		return ConstIterator(_ptr + _size);
	}

private:
	T *_ptr;
	size_t _size;
};

template <typename T, size_t N>
Span<T> to_span(std::array<T, N> &a, unsigned int count) {
	ZN_ASSERT(count <= a.size());
	return Span<T>(a.data(), count);
}

template <typename T, size_t N>
Span<T> to_span(std::array<T, N> &a) {
	return Span<T>(a.data(), a.size());
}

template <typename T, size_t N>
Span<const T> to_span(const std::array<T, N> &a) {
	return Span<const T>(a.data(), a.size());
}

// TODO Deprecate, now Span has a conversion constructor that can allow doing that
template <typename T>
Span<const T> to_span_const(const Span<T> &a) {
	return Span<const T>(a.data(), 0, a.size());
}

template <typename T>
inline Span<T> to_single_element_span(T &a) {
	return Span<T>(&a, 1);
}

} // namespace zylann

#endif // ZN_SPAN_H
