#include <memory/kalloc.h>
#include <str.h>
#include <string.h>
#include <type.h>

KERNEL_API kstring str(const char* src) {
	if (!src)
		return NULL;

	size_t len = strlen(src);

	/* Sanity check: prevent huge allocations */
	if (len > 4096)
		return NULL;

	kstring s = (kstring)kalloc(sizeof(*s));
	if (!s)
		return NULL;

	char* buf = (char*)kalloc(len + 1);
	if (!buf) {
		kfree(s, sizeof(*s));
		return NULL;
	}

	s->c_str = buf;
	s->len = len;
	s->cap = len + 1;

	memcopy(buf, (void*)src, len); /* Use memcopy instead of strcpy */
	buf[len] = '\0';               /* Manually null-terminate */
	return s;
}

KERNEL_API void str_release(kstring str) {
	if (!str)
		return;
	if (str->c_str)
		kfree(str->c_str, str->cap);
	kfree(str, sizeof(*str));
}

KERNEL_API boolean_t stringcmp(kstring s1, kstring s2) {
	if (!s1 || !s2)
		return false;
	if (s1->len != s2->len)
		return false;
	return strncmp(s1->c_str, s2->c_str, s1->len) == 0;
}

KERNEL_API kstring str_concat(kstring s, const char* suffix) {
	if (!s || !suffix)
		return NULL;

	size_t suffix_len = strlen(suffix);
	size_t new_len = s->len + suffix_len;

	kstring s_new = (kstring)kalloc(sizeof(*s_new));
	if (!s_new)
		return NULL;

	char* buf = (char*)kalloc(new_len + 1);
	if (!buf) {
		kfree(s_new, sizeof(*s_new));
		return NULL;
	}

	s_new->c_str = buf;
	s_new->len = new_len;
	s_new->cap = new_len + 1;

	strcpy(s_new->c_str, s->c_str);
	strcpy(s_new->c_str + s->len, suffix);

	return s_new;
}

KERNEL_API kstring str_concat_prefix(kstring s, const char* prefix) {
	if (!s || !prefix)
		return NULL;

	size_t prefix_len = strlen(prefix);
	size_t new_len = s->len + prefix_len;

	kstring s_new = (kstring)kalloc(sizeof(*s_new));
	if (!s_new)
		return NULL;

	char* buf = (char*)kalloc(new_len + 1);
	if (!buf) {
		kfree(s_new, sizeof(*s_new));
		return NULL;
	}

	s_new->c_str = buf;
	s_new->len = new_len;
	s_new->cap = new_len + 1;

	strcpy(buf, prefix);
	strcpy(buf + prefix_len, s->c_str);

	return s_new;
}

KERNEL_API void str_trim(kstring str) {
	if (!str || !str->c_str || str->len == 0)
		return;

	/* pakai int bukan size_t supaya i >= 0 bisa dievaluasi dengan benar */
	int i = (int)str->len - 1;
	while (i >= 0) {
		char c = str->c_str[i];
		if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
			str->c_str[i] = '\0';
			str->len--;
			i--;
		} else {
			break;
		}
	}
}