#ifndef __DEV__EVENT_H__
#define __DEV__EVENT_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dev_event_data {
	const uint8_t* data;
	size_t len;
	uint8_t available;
};

#ifdef __cplusplus
}
#endif

#endif // __DEV__EVENT_H__