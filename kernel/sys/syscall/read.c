#include "libk/serial.h"
#include "type.h"
#include "vfs/dentry.h"
#include "vfs/enum.h"
#include "vfs/filesystem.h"
#include "vfs/vnode.h"
#include <sys/syscall.h>
#include <dev/event.h>

int syscall_read(int fd, void* buf, long count) {
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);

    // TODO: sementara hardcode
    
    // serial2_printf("syscall read %d %x %d\n", fd, buf, count);


    // dentry_ptr out;
    // if (resolve_dentry("/dev/event/event0", 0, &out, 0) != VFS_OK) {
    //     serial2_printf("failed resolve dentry\n");
    //     return -1;
    // }
    // serial2_printf("dentry found: %s \n", out->name->c_str);
    // auto vnode = out->vnode;
    // auto priv = (struct dev_event_data *)vnode->vnode_private;

    // // for (size_t i = 0; i < 20; i++) {
    // //     serial2_printf("%x ", priv->data[i]);
    // // }
    // serial2_printf("readed %d \n", priv->len);


    return 0;
}