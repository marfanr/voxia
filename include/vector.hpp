#ifndef __LIBK__VECTOR_HPP_
#define __LIBK__VECTOR_HPP_

#include <memory/kalloc.h>
#include <type.h>
#include <vector.h>
#include <str.h>

// vector untuk c++
template <typename T> class Vector {
      public:
	Vector() : data_(nullptr), size_(0), capacity_(0) {
		reserve(VECTOR_MINIMUM_ITEM);
	}

	~Vector() {
		destroy();
	}

	// Nonaktifkan copy untuk menghindari double-free
	Vector(const Vector&) = delete;
	Vector& operator=(const Vector&) = delete;

	/**
     * @brief Move constructor.
     */
	Vector(Vector&& other) noexcept
	    : data_(other.data_), size_(other.size_),
	      capacity_(other.capacity_) {
		other.data_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
	}

	/**
     * @brief Move assignment.
     */
	Vector& operator=(Vector&& other) noexcept {
		if (this != &other) {
			destroy();
			data_ = other.data_;
			size_ = other.size_;
			capacity_ = other.capacity_;
			other.data_ = nullptr;
			other.size_ = 0;
			other.capacity_ = 0;
		}
		return *this;
	}

	/** @brief Akses elemen by index (tanpa bounds check). */
	T& operator[](size_t index) {
		return data_[index];
	}

	/** @brief Akses elemen by index — const overload. */
	const T& operator[](size_t index) const {
		return data_[index];
	}

	/** @brief Elemen pertama. */
	T& front() {
		return data_[0];
	}

	/** @brief Elemen terakhir. */
	T& back() {
		return data_[size_ - 1];
	}

	/** @brief Pointer raw ke data internal. */
	T* data() {
		return data_;
	}

	/** @brief Pointer raw ke data internal — const. */
	const T* data() const {
		return data_;
	}

	/** @brief Jumlah elemen aktif. */
	size_t size() const {
		return size_;
	}

	/** @brief Kapasitas buffer saat ini. */
	size_t capacity() const {
		return capacity_;
	}

	/** @brief True jika tidak ada elemen. */
	bool empty() const {
		return size_ == 0;
	}

	/**
     * @brief Tambah elemen ke akhir (copy).
     *        Menggantikan macro vector_push_back().
     */
	void push_back(const T& val) {
		if (size_ >= capacity_)
			expand_capacity();
		data_[size_++] = val;
	}

	/**
     * @brief Tambah elemen ke akhir (move).
     */
	void push_back(T&& val) {
		if (size_ >= capacity_)
			expand_capacity();
		data_[size_++] = static_cast<T&&>(val);
	}

	/**
     * @brief Hapus elemen terakhir dan kembalikan nilainya.
     *        Menggantikan macro vector_pop_back() — versi ini TIDAK
     *        menggunakan `return` di dalam macro (yang berbahaya).
     *
     * @return Pointer ke elemen yang dihapus, atau nullptr jika kosong.
     *         Perhatian: pointer ini menunjuk ke elemen di buffer internal;
     *         nilainya valid sampai push_back / destroy berikutnya.
     */
	T* pop_back() {
		if (size_ == 0)
			return nullptr;
		return &data_[--size_];
	}

	/**
     * @brief Reset ukuran ke 0 tanpa membebaskan memori.
     *        Menggantikan macro vector_clear().
     */
	void clear() {
		size_ = 0;
	}

	/**
     * @brief Pastikan kapasitas minimal `new_cap` elemen.
     *        Menggantikan vector_expand_capacity() dengan kontrol eksplisit.
     */
	void reserve(size_t new_cap) {
		if (new_cap <= capacity_)
			return;

		T* new_data = static_cast<T*>(kalloc(new_cap * sizeof(T)));

		if (data_) {
			memcopy(new_data, data_, capacity_ * sizeof(T));
			kfree(data_, capacity_ * sizeof(T));
		}

		data_ = new_data;
		capacity_ = new_cap;
	}

	T* begin() {
		return data_;
	}
	T* end() {
		return data_ + size_;
	}

	const T* begin() const {
		return data_;
	}
	const T* end() const {
		return data_ + size_;
	}

      private:
	T* data_;
	size_t size_;
	size_t capacity_;

	/**
     * @brief Dobel kapasitas — menggantikan macro vector_expand_capacity().
     */
	void expand_capacity() {
		size_t new_cap = capacity_ ? capacity_ * 2 : 4;
		reserve(new_cap);
	}

	/**
     * @brief Bebaskan memori — menggantikan macro vector_destroy().
     */
	void destroy() {
		if (data_) {
			kfree(data_, capacity_ * sizeof(T));
			data_ = nullptr;
			size_ = 0;
			capacity_ = 0;
		}
	}
};

#endif // __LIBK__VECTOR_HPP_