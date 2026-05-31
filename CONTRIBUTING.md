# Contributing

## Commit Messages

Format:

```
<scope>: <description> [#issue]
```

### Scopes

**Kernel subsystems**

* `sys` — system calls
* `process` / `proc` — process management, scheduler
* `vmm` / `mm` — virtual memory, paging, allocator
* `vfs` / `fs` — filesystem, ISO9660, etc.
* `tty` — terminal subsystem
* `input` / `hid` — input devices, keyboard
* `graphic` — rendering, console, fonts
* `serial` — serial port
* `int` — interrupt handling
* `cpu` — CPU specific code
* `elf` — ELF loader
* `net` — networking stack
* `virtio` — VirtIO drivers
* `hal` — hardware abstraction layer
* `initrd` — initramfs

**Code & build**

* `libk` — kernel library
* `refactor` — restructuring without new behavior
* `fmt` / `formating` — code formatting
* `build` — Makefile, kconfig, build system
* `docs` / `doc` — documentation
* `sbin` — userspace binaries

### Rules

* Lowercase, imperative mood: `add`, `fix`, `implement`, `refactor`, `remove`
* One line only, no trailing period
* Append `#<issue>` when relevant

### Examples

```
sys: implement mmap and mprotect #11
elf: fix segment mapping offsets and kernel pointer resolution
input: implement scancode parser for keyboard
build: refactor makefile #29
refactor: decrease complexity on syscall_mmap
docs: add design spec for ELF load mapping
tty: implement tty #7
```

### Exceptions

Simple cleanups can omit scope:

```
remove unnecessary comments
formating
update readme
```
