// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Mohammad Arfan

#include "hal/cpu/paging.h"
#include "memory/kalloc.h"
#include "memory/memory_utils.h"
#include "str.h"
#include <sys/syscall.h>

#define MAX_ARG_COUNT 32
#define MAX_ARG_STRLEN 4096

bool is_valid_user_pointer(page_t pml4, const void* ptr, size_t size) {
	if (!ptr)
		return false;

	uintptr_t addr = (uintptr_t)ptr;
	if (addr < PAGE_SIZE_4KB || addr >= g_hhdm_offset)
		return false;
	if (addr + size < addr)
		return false;

	uintptr_t start_page = ALIGN_DOWN(addr, PAGE_SIZE_4KB);
	uintptr_t end_page = ALIGN_DOWN(addr + size - 1, PAGE_SIZE_4KB);

	for (uintptr_t pg = start_page; pg <= end_page; pg += PAGE_SIZE_4KB) {
		if (vaddr_to_paddr(pml4, pg) == 0) {
			return false;
		}
	}
	return true;
}

char** copy_string_array(char* const* arr) {
	page_t pml4 = paging_get_highest_page_map();

	if (!arr || (uintptr_t)arr < PAGE_SIZE_4KB ||
	    (uintptr_t)arr >= g_hhdm_offset)
		return NULL;

	if (!is_valid_user_pointer(pml4, arr, sizeof(char*)))
		return NULL;

	int count = 0;
	while (count < MAX_ARG_COUNT) {
		if (!is_valid_user_pointer(pml4, &arr[count], sizeof(char*))) {
			return NULL;
		}
		if (!arr[count])
			break;
		if ((uintptr_t)arr[count] < PAGE_SIZE_4KB ||
		    (uintptr_t)arr[count] >= g_hhdm_offset) {
			return NULL;
		}
		count++;
	}

	if (count == MAX_ARG_COUNT && arr[count] != NULL) {
		return NULL;
	}

	char** new_arr = (char**)kalloc(((size_t)count + 1) * sizeof(char*));
	if (!new_arr)
		return NULL;

	for (int i = 0; i < count; i++) {
		size_t len = 0;
		bool valid = true;
		while (len < MAX_ARG_STRLEN) {
			char* char_ptr = &arr[i][len];

			if (!len || ((uintptr_t)char_ptr & 0xFFFUL) == 0) {
				if (!is_valid_user_pointer(pml4, char_ptr, 1)) {
					valid = false;
					break;
				}
			}

			if (*char_ptr == '\0') {
				break;
			}
			len++;
		}

		if (!valid || len >= MAX_ARG_STRLEN) {
			for (int j = 0; j < i; j++) {
				kfree2(new_arr[j]);
			}
			kfree2(new_arr);
			return NULL;
		}

		new_arr[i] = (char*)kalloc(len + 1);
		if (!new_arr[i]) {
			for (int j = 0; j < i; j++) {
				kfree2(new_arr[j]);
			}
			kfree2(new_arr);
			return NULL;
		}
		memcopy(new_arr[i], (void*)arr[i], len);
		new_arr[i][len] = '\0';
	}
	new_arr[count] = NULL;
	return new_arr;
}

kstring safe_str_from_user(page_t pml4, const char* ustr) {
	if (!ustr || (uintptr_t)ustr < 4096 || (uintptr_t)ustr >= g_hhdm_offset)
		return NULL;

	size_t len = 0;
	while (len < MAX_ARG_STRLEN) {
		const char* char_ptr = &ustr[len];
		if (!len || ((uintptr_t)char_ptr & 0xFFFUL) == 0) {
			if (!is_valid_user_pointer(pml4, char_ptr, 1)) {
				return NULL;
			}
		}
		if (*char_ptr == '\0') {
			break;
		}
		len++;
	}
	if (len >= MAX_ARG_STRLEN) {
		return NULL;
	}

	kstring s = str(ustr);
	if (!s)
		return NULL;
	
	return s;
}

void free_string_array(char** arr) {
	if (!arr)
		return;
	for (int i = 0; arr[i]; i++) {
		kfree2(arr[i]);
	}
	kfree2(arr);
}