#include <vcomp.h>
#include <vxair.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>


uint32_t* load_bmp32_raw(const char* filepath, uint32_t* out_w,
                         uint32_t* out_h) {
	int fd = open(filepath, 0);
	if (fd < 0)
		return NULL;

	uint8_t header[54];
	if (read(fd, header, 54) != 54 || header[0] != 'B' ||
	    header[1] != 'M') {
		close(fd);
		return NULL;
	}

	uint32_t data_offset = *(uint32_t*)&header[10];
	int32_t width = *(int32_t*)&header[18];
	int32_t height = *(int32_t*)&header[22];
	uint16_t bpp = *(uint16_t*)&header[28];

	if (bpp != 32 && bpp != 24) {
		close(fd);
		return NULL;
	}

	int is_bottom_up = 1;
	if (height < 0) {
		is_bottom_up = 0;
		height = -height;
	}

	lseek(fd, data_offset, 0);

	uint32_t* pixels = malloc(width * height * 4);
	if (!pixels) {
		close(fd);
		return NULL;
	}

	if (bpp == 32) {
		int total_bytes = width * height * 4;
		uint8_t* file_buf = malloc(total_bytes);
		int bytes_read = 0;
		while (bytes_read < total_bytes) {
			int chunk = total_bytes - bytes_read;
			if (chunk > 65536)
				chunk = 65536; // MAX 64KB per read
			int r = read(fd, file_buf + bytes_read, chunk);
			if (r <= 0)
				break;
			bytes_read += r;
		}

		if (is_bottom_up) {
			for (int y = height - 1; y >= 0; y--) {
				memcpy(&pixels[y * width],
				       file_buf + (height - 1 - y) * width * 4,
				       width * 4);
			}
		} else {
			memcpy(pixels, file_buf, total_bytes);
		}
		free(file_buf);

		// Notice: We NO LONGER swap BGRA to RGBA here. QEMU Virgl
		// natively expects BGRA memory! This saves ~8.2 million loop
		// iterations and 33MB of extra allocations.
	} else if (bpp == 24) {
		int row_stride = (width * 3 + 3) & ~3;
		int total_bytes = row_stride * height;

		uint8_t* file_buf = malloc(total_bytes);
		int bytes_read = 0;
		while (bytes_read < total_bytes) {
			int chunk = total_bytes - bytes_read;
			if (chunk > 65536)
				chunk = 65536; // MAX 64KB per read
			int r = read(fd, file_buf + bytes_read, chunk);
			if (r <= 0)
				break;
			bytes_read += r;
		}

		for (int row = 0; row < height; row++) {
			int y = is_bottom_up ? (height - 1 - row) : row;
			uint8_t* row_ptr = &file_buf[row * row_stride];
			uint32_t* dst_ptr = &pixels[y * width];

			for (int x = 0; x < width; x++) {
				uint32_t src_idx = x * 3;
				uint32_t b = row_ptr[src_idx];
				uint32_t g = row_ptr[src_idx + 1];
				uint32_t r = row_ptr[src_idx + 2];
				dst_ptr[x] =
				    b | (g << 8) | (r << 16) | (0xFF000000);
			}
		}
		free(file_buf);
	}

	close(fd);

	printf("Sukses memuat BMP mentah %dx%d (%d bpp)\n", width, height, bpp);
	if (out_w)
		*out_w = width;
	if (out_h)
		*out_h = height;
	return pixels;
}


void sleep_ns(uint64_t ns) {
	struct timespec ts;

	ts.tv_sec = ns / 1000000000ULL;
	ts.tv_nsec = ns % 1000000000ULL;

	nanosleep(&ts, NULL);
}


uint64_t time_ns() {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	uint64_t ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
	return ns;
}
