#include "type.h"
#include "hal/cpu/core.h"
#include "procc/thread.h"
#include "str.h"
#include "sys/err_no.h"
#include "vfs/dentry.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include "memory/kalloc.h"
#include "string.h"
#include "cpu/irq_lock.h"
#include "hal/timer/timer.h"
#include <linux_input.h>

#define EVDEV_MAX_EVENTS 512 /* dinaikkan dari 256: buffer kecil bikin
                                 REL_X/REL_Y ke-drop duluan sebelum sempat
                                 dibaca userspace; 512 beri margin aman
                                 untuk burst mouse event */

extern void evdev_report_key(uint16_t code, int32_t value);
extern bool evdev_report_mouse(int16_t x, int16_t y, int8_t z, uint8_t buttons);
extern void evdev_init(void);

struct evdev_device {
    int id;
    int is_mouse;
    struct input_event events[EVDEV_MAX_EVENTS];
    int head;
    int tail;
    spinlock_t lock;
    struct thread* waiter;
    dentry_ptr dentry;
};

static struct evdev_device* evdev_keyboard = NULL;
static struct evdev_device* evdev_mouse = NULL;
static time_counter_t evdev_time;

static void evdev_push_event_nolock(struct evdev_device* dev, uint16_t type, uint16_t code, int32_t value) {
    int next_head = (dev->head + 1) % EVDEV_MAX_EVENTS;
    if (next_head == dev->tail) {
        // Buffer full, drop oldest event
        dev->tail = (dev->tail + 1) % EVDEV_MAX_EVENTS;
    }

    uint64_t ms = get_timer_counter_count_ms(&evdev_time);
    uint64_t sec = ms / 1000;
    uint64_t usec = (ms % 1000) * 1000;

    dev->events[dev->head].tv_sec = sec;
    dev->events[dev->head].tv_usec = usec;
    dev->events[dev->head].type = type;
    dev->events[dev->head].code = code;
    dev->events[dev->head].value = value;
    dev->head = next_head;

    if (dev->waiter) {
        vxThreadWake(dev->waiter);
        dev->waiter = NULL;
    }
}

static void evdev_push_event(struct evdev_device* dev, uint16_t type, uint16_t code, int32_t value) {
    if (!dev) return;
    uintptr_t flags = irq_save();
    spin_acquire(&dev->lock);
    evdev_push_event_nolock(dev, type, code, value);
    spin_release(&dev->lock);
    irq_restore(flags);
}

void evdev_report_key(uint16_t code, int32_t value) {
    if (evdev_keyboard) {
        evdev_push_event(evdev_keyboard, EV_KEY, code, value);
        evdev_push_event(evdev_keyboard, EV_SYN, SYN_REPORT, 0);
    }
}

static int evdev_free_slots(struct evdev_device* dev) {
    if (!dev) return 0;
    if (dev->head >= dev->tail) {
        return EVDEV_MAX_EVENTS - (dev->head - dev->tail) - 1;
    } else {
        return (dev->tail - dev->head) - 1;
    }
}

bool evdev_report_mouse(int16_t x, int16_t y, int8_t z, uint8_t buttons) {
    static uint8_t prev_buttons = 0;

    if (!evdev_mouse) return false;

    // Hitung berapa event yang akan dipush dalam paket ini
    int needed_slots = 1; // Minimal untuk SYN_REPORT
    if (x != 0) needed_slots++;
    if (y != 0) needed_slots++;
    if (z != 0) needed_slots++;
    
    uint8_t changed = buttons ^ prev_buttons;
    if (changed & 1) needed_slots++;
    if (changed & 2) needed_slots++;
    if (changed & 4) needed_slots++;

    /* Pegang lock SEKALI untuk seluruh check+push, hindari TOCTOU race
       dimana buffer bisa penuh antara check dan push */
    uintptr_t flags = irq_save();
    spin_acquire(&evdev_mouse->lock);

    int free_slots = evdev_free_slots(evdev_mouse);
    if (free_slots < needed_slots) {
        spin_release(&evdev_mouse->lock);
        irq_restore(flags);
        return false; // Drop packet — caller harus skip cursor update
    }

    // Push semua event dalam satu paket dengan lock tetap dipegang
    if (x != 0) evdev_push_event_nolock(evdev_mouse, EV_REL, REL_X, x);
    if (y != 0) evdev_push_event_nolock(evdev_mouse, EV_REL, REL_Y, y);
    if (z != 0) evdev_push_event_nolock(evdev_mouse, EV_REL, REL_Z, z);

    if (changed & 1)
        evdev_push_event_nolock(evdev_mouse, EV_KEY, BTN_LEFT, (buttons & 1) ? 1 : 0);
    if (changed & 2)
        evdev_push_event_nolock(evdev_mouse, EV_KEY, BTN_RIGHT, (buttons & 2) ? 1 : 0);
    if (changed & 4)
        evdev_push_event_nolock(evdev_mouse, EV_KEY, BTN_MIDDLE, (buttons & 4) ? 1 : 0);
        
    prev_buttons = buttons;

    evdev_push_event_nolock(evdev_mouse, EV_SYN, SYN_REPORT, 0);

    spin_release(&evdev_mouse->lock);
    irq_restore(flags);
    return true;
}

static int evdev_read(vnode_t* vnode, void* buf, size_t len, size_t offset) {
    (void)offset;
    struct evdev_device* dev = (struct evdev_device*)vnode->vnode_private;
    if (!dev) return -EINVAL;

    if (len < sizeof(struct input_event)) return -EINVAL;

    uintptr_t flags = irq_save();
    spin_acquire(&dev->lock);

    if (dev->head == dev->tail) {
        spin_release(&dev->lock);
        irq_restore(flags);
        return -EAGAIN;
    }

    int bytes_read = 0;
    while (dev->head != dev->tail && len >= sizeof(struct input_event)) {
        memcopy((uint8_t*)buf + bytes_read, &dev->events[dev->tail], sizeof(struct input_event));
        dev->tail = (dev->tail + 1) % EVDEV_MAX_EVENTS;
        bytes_read += (int)sizeof(struct input_event);
        len -= sizeof(struct input_event);
    }

    spin_release(&dev->lock);
    irq_restore(flags);
    return bytes_read;
}

static int evdev_ioctl(vnode_t* vnode, uint32_t request, void* argp) {
    struct evdev_device* dev = (struct evdev_device*)vnode->vnode_private;
    if (!dev) return -EINVAL;

    if ((request & ~(uint32_t)_IOC_SIZEMASK) == ((uint32_t)EVIOCGNAME(0) & ~(uint32_t)_IOC_SIZEMASK)) {
        size_t len = (request >> _IOC_SIZESHIFT) & _IOC_SIZEMASK;
        const char* name = dev->is_mouse ? "Mouse" : "Keyboard";
        size_t namelen = strlen(name) + 1;
        if (namelen > len) namelen = len;
        memcopy(argp, (void*)name, namelen);
        return (int)namelen;
    }

    if ((request & ~(uint32_t)_IOC_SIZEMASK) == ((uint32_t)EVIOCGBIT(0, 0) & ~(uint32_t)_IOC_SIZEMASK)) {
        int ev = (int)((request >> _IOC_NRSHIFT) & _IOC_NRMASK);
        if (ev >= 0x20) {
            ev -= 0x20;
        }
        size_t len = (request >> _IOC_SIZESHIFT) & _IOC_SIZEMASK;
        
        uint8_t* bits = (uint8_t*)argp;
        memset(bits, 0, len);
        
        if (ev == 0) { // EV bits
            if (len > 0) {
                bits[0] |= (1 << EV_SYN);
                if (dev->is_mouse) {
                    bits[0] |= (1 << EV_REL);
                    bits[0] |= (1 << EV_KEY);
                } else {
                    bits[0] |= (1 << EV_KEY);
                }
            }
            return (int)len;
        } else if (ev == EV_REL && dev->is_mouse) {
            if (len > 0) bits[0] |= (1 << REL_X) | (1 << REL_Y) | (1 << REL_Z);
            return (int)len;
        } else if (ev == EV_KEY) {
            if (dev->is_mouse) {
                if (len > (BTN_MOUSE / 8)) {
                    bits[BTN_MOUSE / 8] |= (1 << (BTN_MOUSE % 8));
                    bits[BTN_RIGHT / 8] |= (1 << (BTN_RIGHT % 8));
                    bits[BTN_MIDDLE / 8] |= (1 << (BTN_MIDDLE % 8));
                }
            } else {
                // Keyboard has all keys. We just fill it with 0xFF for simplicity, except BTN_MOUSE
                memset(bits, 0xFF, len);
                if (len > (BTN_MOUSE / 8)) {
                    bits[BTN_MOUSE / 8] &= ~(1 << (BTN_MOUSE % 8));
                    bits[BTN_RIGHT / 8] &= ~(1 << (BTN_RIGHT % 8));
                    bits[BTN_MIDDLE / 8] &= ~(1 << (BTN_MIDDLE % 8));
                }
            }
            return (int)len;
        }
        return -EINVAL;
    }

    return -EINVAL;
}

static vops_file_t evdev_ops = {
    .read = evdev_read,
    .ioctl = evdev_ioctl,
};

static struct evdev_device* create_evdev(int id, int is_mouse) {
    struct evdev_device* dev = kalloc(sizeof(struct evdev_device));
    memset(dev, 0, sizeof(struct evdev_device));
    dev->id = id;
    dev->is_mouse = is_mouse;
    dev->head = 0;
    dev->tail = 0;
    
    kstring path = str_concat(str("/dev/input/event"), itoa(id, 10, (char[32]){0}));
    vxnamei(path->c_str, &dev->dentry);
    str_release(path);

    vnode_ptr_t vnode = create_and_attach_vnode();
    vnode->type = VNODE_TYPE_CHR;
    vnode->ops = &evdev_ops;
    vnode->permission = 660;
    vnode->vnode_private = dev;
    
    dev->dentry->vnode = vnode;
    return dev;
}

void evdev_init(void) {
    evdev_keyboard = create_evdev(0, 0); // /dev/input/event0
    evdev_mouse = create_evdev(1, 1);    // /dev/input/event1
    init_timer_counter(&evdev_time);
}