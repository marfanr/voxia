#include "syscall.h"
#include "hal/acpi/hpet.h"
#include "hal/cpu/core.h"
#include "hal/cpu/interrupt.h"
#include "hal/cpu/msr.h"
#include "hal/cpu/paging.h"
#include "init/init.h"
#include "libk/serial.h"
#include "memory/memory_utils.h"
#include "memory/vm_manager.h"
#include "procc/scheduler.h"
#include "procc/thread.h"
#include "sys/err_no.h"
#include "sys/fd.h"
#include "tty/tty.h"

// prototype
extern void syscall_dispatch(interrupt_stack_frame_t* rsp);
extern void syscall_entry(void);

static const char* get_syscall_name(int rax);
void syscall_init(void) {
	uint64_t efer = vxRDMSR(MSR_EFER);
	vxWRSR(MSR_EFER, efer | 1 | (1ULL << 11)); // SCE and NXE bits

	vxWRSR(MSR_LSTAR, (uint64_t)syscall_entry);

	uint64_t star = 0;
	star |= (uint64_t)0x0028 << 32; // kernel CS (0x28), kernel SS (0x30)
	star |= (uint64_t)0x003B << 48; // user base for SYSRET (CS32 = 0x38, SS
	                                // = +8 = 0x40, CS64 = +16 = 0x48)
	vxWRSR(MSR_STAR, star);

	vxWRSR(MSR_FMASK, (1 << 9)); // IF bit
}

extern void syscall_dispatch(interrupt_stack_frame_t* rsp) {
	auto thr = get_current_core_data()->active_thread;
	if (thr) {
		vxSaveRegister(rsp, &thr->reg);
	}

// DEBUG
#ifdef VOXIA_SYSCALL_DEBUG
	auto curr_thr = get_current_core_data()->active_thread;
	/* Log all syscalls except high-frequency noise */
	if (rsp->rax != SYSCALL_NANOSLEEP && rsp->rax != SYSCALL_EXIT)
		serial2_printf("[SYSCALL] thr=%d pid=%d nr=%d (%s) rdi=0x%x rsi=0x%x rdx=0x%x\n", curr_thr ? curr_thr->id : 0,
		               (curr_thr && curr_thr->process) ? curr_thr->process->pid : 0, (int)rsp->rax, get_syscall_name((int)rsp->rax), rsp->rdi, rsp->rsi,
		               rsp->rdx);
#endif

	auto int_no = rsp->rax;

	if (thr && thr->signal && int_no != SYSCALL_EXIT && int_no != SYSCALL_EXIT_GROUP && int_no != SYSCALL_RT_SIGACTION && int_no != SYSCALL_SIGPROCMASK &&
	    int_no != 15) {
		// serial2_printf("active signal on thread %d\n", thr->id);

		uint64_t pending = __atomic_load_n(&thr->signal->pending.__bits[0], __ATOMIC_ACQUIRE);
		if (pending) {
			serial2_printf("active signal available\n");
			for (int sig = 1; sig <= 64; sig++) {
				if (pending & SIGBIT(sig)) {
					serial2_printf("found pending sig %d on syscall\n", sig);
					sig_handle_ptr_t handler = thr->signal->handler[sig - 1];
					if (handler == 0) { // SIG_DFL
						if (sig == SIGINT || sig == SIGQUIT || sig == SIGKILL || sig == SIGTERM) {
							__atomic_fetch_and(&thr->signal->pending.__bits[0], ~SIGBIT(sig), __ATOMIC_RELEASE);
							serial2_printf("Thread %d "
							               "terminated by "
							               "signal %d in "
							               "syscall\n",
							               thr->id, sig);
							syscall_exit_group(128 + sig);
						}
					} else if ((uintptr_t)handler == 1) { // SIG_IGN
						__atomic_fetch_and(&thr->signal->pending.__bits[0], ~SIGBIT(sig), __ATOMIC_RELEASE);
					} else { // Custom handler
						serial2_printf("SYSCALL START: Custom "
						               "signal %d handler %p "
						               "restorer %p for thread "
						               "%d\n",
						               sig, handler, thr->signal->restorer[sig - 1], thr->id);
						__atomic_fetch_and(&thr->signal->pending.__bits[0], ~SIGBIT(sig), __ATOMIC_RELEASE);
						vxSaveRegister(rsp, &thr->saved_reg);
						if (thr->signal->flags[sig - 1] & SA_RESTART) {
							thr->saved_reg.rip -= 2;
							thr->saved_reg.rax = (uint64_t)int_no;
						} else {
							thr->saved_reg.rax = (uint64_t)-EINTR;
						}
						thr->has_saved_reg = true;

						uint64_t* user_sp = (uint64_t*)((rsp->rsp & ~15ULL) - 8);
						*user_sp = (uint64_t)thr->signal->restorer[sig - 1];

						rsp->rsp = (uint64_t)user_sp;
						rsp->rdi = (uint64_t)sig;
						rsp->rsi = 0;
						rsp->rdx = 0;
						rsp->rip = (uintptr_t)handler;
						return;
					}
				}
			}
		}
	}

	// TODO: refactor this using array of linker section
	switch (int_no) {
	case SYSCALL_READ:
		rsp->rax = (uint64_t)syscall_read((int)rsp->rdi, (void*)rsp->rsi, (int)rsp->rdx);
		break;
	case SYSCALL_WRITE: {
		rsp->rax = (uint64_t)syscall_write((int)rsp->rdi, (void*)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_OPEN: {
		rsp->rax = (uint64_t)syscall_open((const char*)rsp->rdi, (int)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_CLOSE: {
		rsp->rax = (uint64_t)syscall_close((int)rsp->rdi);
		break;
	}
	case SYSCALL_FSYNC: {
		rsp->rax = (uint64_t)syscall_fsync((int)rsp->rdi);
		break;
	}
	case 32: { // SYSCALL_DUP
		extern long syscall_dup(int oldfd);
		rsp->rax = (uint64_t)syscall_dup((int)rsp->rdi);
		break;
	}
	case 33: { // SYSCALL_DUP2
		extern long syscall_dup2(int oldfd, int newfd);
		rsp->rax = (uint64_t)syscall_dup2((int)rsp->rdi, (int)rsp->rsi);
		break;
	}
	case 72: { // SYSCALL_FCNTL
		extern long syscall_fcntl(int fd, int cmd, unsigned long arg);
		rsp->rax = (uint64_t)syscall_fcntl((int)rsp->rdi, (int)rsp->rsi, rsp->rdx);
		break;
	}
	case SYSCALL_ARCH_PRCTL: {
		rsp->rax = (uint64_t)syscall_arch_prctl((int)rsp->rdi, (unsigned long)rsp->rsi);
		break;
	}
	case SYSCALL_SET_TID: {
		rsp->rax = (uint64_t)syscall_set_tid((uintptr_t)rsp->rdi);
		break;
	}
	case SYSCALL_EXIT: {
		thr->state = THREAD_STATE_TERMINATED;
		if (thr->clear_child_tid) {
			*thr->clear_child_tid = 0;
			syscall_futex((int*)thr->clear_child_tid, 1 /* FUTEX_WAKE */, 1, NULL, NULL, 0);
		}
		// auto procc = thr->process;
		// A thread exiting should not mark the whole process as exited!
		// That is the job of SYSCALL_EXIT_GROUP.
		// We just terminate the thread and yield.
		// TODO: clear allocated memory on heap and mmap
		break;
	}
	case SYSCALL_IOCTL: {
		rsp->rax = (uint64_t)syscall_ioctl((int)rsp->rdi, (uint32_t)rsp->rsi, (void*)rsp->rdx);
		break;
	}
	case SYSCALL_GETCWD: {
		rsp->rax = (uint64_t)syscall_getcwd((char*)rsp->rdi, (size_t)rsp->rsi);
		break;
	}
	case SYSCALL_CHDIR: {
		rsp->rax = (uint64_t)syscall_chdir((const char*)rsp->rdi);
		break;
	}
	case 81: {
		rsp->rax = (uint64_t)syscall_fchdir((int)rsp->rdi);
		break;
	}
	case 89: { // SYSCALL_READLINK
		const char* path = (const char*)rsp->rdi;
		char* buf = (char*)rsp->rsi;
		size_t bufsiz = (size_t)rsp->rdx;
		
		auto proc = get_current_core_data()->active_thread->process;
		dentry_ptr base_dir = (proc && path[0] != '/') ? proc->cwd : 0;

		dentry_ptr out;
		if (resolve_dentry(path, base_dir, &out, 0) != 0) { // VFS_OK is 0
			rsp->rax = (uint64_t)-ENOENT;
			break;
		}
		if (out->vnode->type != VNODE_TYPE_LNK) {
			dentry_put(out);
			rsp->rax = (uint64_t)-EINVAL;
			break;
		}
		auto lnk_ops = (vops_lnk_t*)out->vnode->ops;
		if (lnk_ops && lnk_ops->readlink) {
			rsp->rax = (uint64_t)lnk_ops->readlink(out->vnode, buf, bufsiz);
		} else {
			rsp->rax = (uint64_t)-EINVAL;
		}
		dentry_put(out);
		break;
	}
	case SYSCALL_READV: {
		rsp->rax = (uint64_t)syscall_readv((int)rsp->rdi, (const struct iovec*)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_WRITEV: {
		rsp->rax = (uint64_t)syscall_writev((int)rsp->rdi, (const struct iovec*)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_BRK: {
		rsp->rax = (uint64_t)syscall_brk((void*)rsp->rdi);
		break;
	}
	case SYSCALL_MMAP: {
		rsp->rax = (uint64_t)syscall_mmap((void*)rsp->rdi, (size_t)rsp->rsi, (int)rsp->rdx, (int)rsp->r10, (int)rsp->r8, (int)rsp->r9);
		break;
	}
	case SYSCALL_MPORTECT: {
		rsp->rax = (uint64_t)syscall_mprotect((void*)rsp->rdi, (size_t)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_EXIT_GROUP: {
		syscall_exit_group((int)rsp->rdi);
		break;
	}
	case SYSCALL_FORK: {
		rsp->rax = (uint64_t)syscall_fork(rsp);
		break;
	}
	case 186: { // SYS_gettid
		rsp->rax = (uint64_t)thr->id;
		break;
	}
	case SYSCALL_EXECVE: {
		rsp->rax = (uint64_t)syscall_execve((const char*)rsp->rdi, (char* const*)rsp->rsi, (char* const*)rsp->rdx, rsp);
		break;
	}
	case SYSCALL_WAIT4: {
		rsp->rax = (uint64_t)syscall_wait4((pid_t)rsp->rdi, (int*)rsp->rsi, (int)rsp->rdx, (void*)rsp->r10);
		break;
	}
	case SYSCALL_PAUSE: { // pause
		auto thr_ = get_current_core_data()->active_thread;
		serial2_printf("pause from thread id %d (procc %d) \n", thr_->id, thr_->process->pid);

		thread_block();
		rsp->rax = (uint64_t)-EINTR;
		break;
	}
	case SYSCALL_RT_SIGACTION: {
		rsp->rax = (uint64_t)syscall_rt_sigaction((int)rsp->rdi, (const void*)rsp->rsi, (void*)rsp->rdx, rsp->r10);
		break;
	}
	case SYSCALL_SIGPROCMASK: {
		rsp->rax = (uint64_t)syscall_rt_sigprocmask((int)rsp->rdi, (void*)rsp->rsi, (void*)rsp->rdx, rsp->r10);
		break;
	}
	case SYSCALL_MOUNT: {
		rsp->rax =
		    (uint64_t)syscall_mount((const char*)rsp->rdi, (const char*)rsp->rsi, (const char*)rsp->rdx, (unsigned long)rsp->r10, (const void*)rsp->r8);
		break;
	}
	case SYSCALL_UNMOUNT: {
		rsp->rax = (uint64_t)syscall_unmount((const char*)rsp->rdi, (int)rsp->rsi);
		break;
	}
	case SYSCALL_GETDENTS:
	case SYSCALL_GETDENTS64: {
		rsp->rax = (uint64_t)syscall_getdents64((int)rsp->rdi, (struct linux_dirent64*)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_SOCKET: {
		rsp->rax = (uint64_t)syscall_socket((int)rsp->rdi, (int)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_BIND: {
		rsp->rax = (uint64_t)syscall_bind((int)rsp->rdi, (const struct sockaddr*)rsp->rsi, (uint32_t)rsp->rdx);
		break;
	}
	case SYSCALL_LISTEN: { // SYSCALL_LISTEN
		rsp->rax = (uint64_t)syscall_listen((int)rsp->rdi, (int)rsp->rsi);
		break;
	}
	case SYSCALL_CONNECT: {
		rsp->rax = (uint64_t)syscall_connect((int)rsp->rdi, (const struct sockaddr*)rsp->rsi, (uint32_t)rsp->rdx);
		break;
	}
	case SYSCALL_ACCEPT: {
		rsp->rax = (uint64_t)syscall_accept((int)rsp->rdi, (struct sockaddr*)rsp->rsi, (uint32_t*)rsp->rdx);
		break;
	}
	case SYSCALL_SENDTO: {
		rsp->rax =
		    (uint64_t)syscall_sendto((int)rsp->rdi, (const void*)rsp->rsi, (uint32_t)rsp->rdx, (int)rsp->r10, (const void*)rsp->r8, (uint32_t)rsp->r9);
		break;
	}
	case SYSCALL_RECVFROM: {
		rsp->rax = (uint64_t)syscall_recvfrom((int)rsp->rdi, (void*)rsp->rsi, (uint32_t)rsp->rdx, (int)rsp->r10, (void*)rsp->r8, (uint32_t*)rsp->r9);
		break;
	}
	case SYSCALL_RT_SIGRETURN: { // SYSCALL_RT_SIGRETURN
		if (thr && thr->has_saved_reg) {
			vxRestoreRegister(rsp, &thr->saved_reg);
			thr->has_saved_reg = false;
		}
		break;
	}
	case 62: { // SYS_kill(pid, sig)
		pid_t pid = (pid_t)rsp->rdi;
		int sig = (int)rsp->rsi;

		if (sig < 0 || sig > 64) {
			rsp->rax = (uint64_t)-EINVAL;
			break;
		}

		if ((int)pid > 0) {
			process_t* target_proc = find_process_by_pid(pid);
			if (target_proc) {
				if (target_proc->main_thread) {
					if (target_proc->main_thread->signal) {
						sig_send(target_proc->main_thread->signal, sig);
					}
					vxThreadWake(target_proc->main_thread);
				}
				rsp->rax = 0;
			} else {
				rsp->rax = (uint64_t)-ESRCH;
			}
		} else if ((int)pid == 0) {
			pid_t sender_pgid = thr->process ? thr->process->pgid : 0;
			pgid_send_signal(sender_pgid, sig);
			rsp->rax = 0;
		} else if ((int)pid == -1) {
			signal_all_processes(sig);
			rsp->rax = 0;
		} else { // pid < -1
			pid_t target_pgid = (pid_t)(-(int)pid);
			pgid_send_signal(target_pgid, sig);
			rsp->rax = 0;
		}
		break;
	}
	case SYSCALL_STAT: {
		rsp->rax = (uint64_t)syscall_stat((const char*)rsp->rdi, (struct stat*)rsp->rsi);
		break;
	}
	case 262: { // SYSCALL_NEWFSTATAT
		int dirfd = (int)rsp->rdi;
		const char* pathname = (const char*)rsp->rsi;
		struct stat* statbuf = (struct stat*)rsp->rdx;
		int flags = (int)rsp->r10;

		rsp->rax = (uint64_t)syscall_newfstatat(dirfd, pathname, statbuf, flags);
		break;
	}
	case SYSCALL_FSTAT: {
		rsp->rax = (uint64_t)syscall_fstat((int)rsp->rdi, (struct stat*)rsp->rsi);
		break;
	}
	case SYSCALL_GETPID: {
		auto proc = get_current_core_data()->active_thread->process;
		rsp->rax = (uint64_t)proc->pid;
		break;
	}
	case SYSCALL_NANOSLEEP: {
		rsp->rax = (uint64_t)syscall_nanosleep((const struct timespec*)rsp->rdi, (struct timespec*)rsp->rsi);
		break;
	}
	case SYSCALL_LSEEK: {
		rsp->rax = (uint64_t)syscall_lseek((int)rsp->rdi, (long)rsp->rsi, (int)rsp->rdx);
		break;
	}
	case SYSCALL_CLONE: {
		rsp->rax = (uint64_t)syscall_clone(rsp);
		break;
	}
	case SYSCALL_FUTEX: {
		rsp->rax = (uint64_t)syscall_futex((int*)rsp->rdi, (int)rsp->rsi, (int)rsp->rdx, (const void*)rsp->r10, (int*)rsp->r8, (int)rsp->r9);
		break;
	}
	case 109: { // setpgid(pid, pgid)
		pid_t target_pid = (pid_t)rsp->rdi;
		pid_t target_pgid = (pid_t)rsp->rsi;

		process_t* target_proc = nullptr;
		if (target_pid == 0) {
			target_proc = get_current_core_data()->active_thread->process;
		} else {
			target_proc = find_process_by_pid(target_pid);
		}

		if (target_proc) {
			if (target_pgid == 0) {
				target_proc->pgid = target_proc->pid;
			} else {
				target_proc->pgid = target_pgid;
			}
			rsp->rax = 0;
		} else {
			rsp->rax = (uint64_t)-ESRCH;
		}
		break;
	}
	// TODO: handle later, need implement user, group, and permission first
	case 21: { // SYSCALL_ACCESS
		dentry_ptr out;
		if (resolve_dentry((const char*)rsp->rdi, 0, &out, 0) != 0 /* VFS_OK */) {
			rsp->rax = (uint64_t)-ENOENT;
		} else {
			rsp->rax = 0;
		}
		break;
	}
	case 23: { // SYSCALL_SELECT
		int nfds = (int)rsp->rdi;
		uint64_t* readfds = (uint64_t*)rsp->rsi;
		auto curr_procc = get_current_core_data()->active_thread->process;
		auto fdt = (struct fdtable*)curr_procc->fdtable;
		int ready_count = 0;

		while (true) {
			for (int i = 0; i < nfds; i++) {
				if (!readfds)
					break;
				if ((readfds[i / 64] & (1ULL << (i % 64))) == 0)
					continue;
				if ((uint32_t)i >= fdt->max_fds)
					continue;
				auto curr_fd = fdt->fds[i];
				if (!curr_fd || !curr_fd->vnode)
					continue;

				auto ops = (vops_file_t*)curr_fd->ops;
				int is_ready = 1;
				if (ops && ops->poll) {
					is_ready = ops->poll(curr_fd->vnode, get_current_core_data()->active_thread);
				}
				if (is_ready)
					ready_count++;
			}
			if (ready_count > 0) {
				break;
			}
			if (rsp->r8) {
				struct __timeval {
					long tv_sec;
					long tv_usec;
				};
				struct __timeval* tv = (struct __timeval*)rsp->r8;
				if (tv->tv_sec == 0 && tv->tv_usec == 0) {
					break;
				}
				// we don't have a timed wait yet, so we just
				// block forever
			}

			auto curr = get_current_core_data()->active_thread;
			if (curr && curr->signal) {
				uint64_t pending = __atomic_load_n(&curr->signal->pending.__bits[0], __ATOMIC_ACQUIRE);
				uint64_t mask = __atomic_load_n(&curr->signal->mask.__bits[0], __ATOMIC_ACQUIRE);
				if (pending & ~mask) {
					rsp->rax = (uint64_t)-EINTR;
					return;
				}
			}

			thread_block();
		}

		if (readfds) {
			for (int i = 0; i < nfds; i++) {
				if ((readfds[i / 64] & (1ULL << (i % 64))) == 0)
					continue;
				boolean_t ready = false;
				if ((uint32_t)i < fdt->max_fds) {
					auto curr_fd = fdt->fds[i];
					if (curr_fd && curr_fd->vnode) {
						auto ops = (vops_file_t*)curr_fd->ops;
						if (ops && ops->poll) {
							if (ops->poll(curr_fd->vnode, NULL))
								ready = true;
						} else {
							ready = true;
						}
					}
				}
				if (!ready) {
					readfds[i / 64] &= ~(1ULL << (i % 64));
				}
			}
		}

		rsp->rax = (uint64_t)ready_count;
		break;
	}
	// TODO: using RTC
	case 96: { // SYSCALL_GETTIMEOFDAY
		struct __timeval {
			long tv_sec;
			long tv_usec;
		};
		struct __timeval* tv = (struct __timeval*)rsp->rdi;
		if (tv) {
			uint64_t ns = vxHPETGetMainCount() * vxHPETMinTickNs();
			tv->tv_sec = ns / 1000000000;
			tv->tv_usec = (ns % 1000000000) / 1000;
		}
		rsp->rax = 0;
		break;
	}
	case 228: { // SYSCALL_CLOCK_GETTIME
		struct __timespec {
			long tv_sec;
			long tv_nsec;
		};
		struct __timespec* tp = (struct __timespec*)rsp->rsi;
		if (tp) {
			uint64_t ns = vxHPETGetMainCount() * vxHPETMinTickNs();
			tp->tv_sec = ns / 1000000000;
			tp->tv_nsec = ns % 1000000000;
		}
		rsp->rax = 0;
		break;
	}
	// TODO: impl later
	case 11: { // SYSCALL_MUNMAP
		void*  mu_addr = (void*)rsp->rdi;
		size_t mu_len  = (size_t)rsp->rsi;

		auto mu_thr  = get_current_core_data()->active_thread;
		auto mu_proc = mu_thr ? mu_thr->process : nullptr;
		if (!mu_proc || !mu_addr || mu_len == 0) {
			rsp->rax = 0; // tolerate bad args silently
			break;
		}

		/*
		 * Unmap every 4 KB page in the requested range and remove the
		 * corresponding VMA records.  Physical memory is not freed yet
		 * (no phys_base_free) but the virtual<->physical mapping is torn
		 * down so that future mmap() calls can reuse these virtual pages.
		 */
		size_t mu_pages = ALIGN_UP(mu_len, 0x1000) / 0x1000;
		paging_multiple_unmap(mu_proc->page,
		                      (uint64_t)(uintptr_t)mu_addr, mu_pages);
		paging_reload(mu_proc->page);

		/* Remove VMA entries that start within this range. */
		for (size_t pg = 0; pg < mu_pages; pg++) {
			uintptr_t v = (uintptr_t)mu_addr + pg * 0x1000;
			vma_unregister(mu_proc->vm_page, v);
		}

		rsp->rax = 0;
		break;
	}

	case 63: { // SYSCALL_UNAME
		struct utsname {
			char sysname[65];
			char nodename[65];
			char release[65];
			char version[65];
			char machine[65];
			char domainname[65];
		};
		struct utsname* name = (struct utsname*)rsp->rdi;
		extern char* strncpy(char* dest, const char* src, size_t n);
		if (name) {
			strncpy(name->sysname, "voxia", 65);
			strncpy(name->nodename, "voxia", 65);
			strncpy(name->release, "1.0", 65);
			strncpy(name->version, "1.0", 65);
			strncpy(name->machine, "x86_64", 65);
			strncpy(name->domainname, "", 65);
			rsp->rax = 0;
		} else {
			rsp->rax = (uint64_t)-EFAULT;
		}
		break;
	}
	case 97: { // SYSCALL_GETRLIMIT
		struct rlimit {
			unsigned long rlim_cur;
			unsigned long rlim_max;
		};
		struct rlimit* rlim = (struct rlimit*)rsp->rsi;
		if (rlim) {
			rlim->rlim_cur = -1UL;
			rlim->rlim_max = -1UL;
			rsp->rax = 0;
		} else {
			rsp->rax = (uint64_t)-EFAULT;
		}
		break;
	}
	case 110: { // SYSCALL_GETPPID
		auto proc = get_current_core_data()->active_thread->process;
		rsp->rax = (uint64_t)proc->parent_pid;
		break;
	}
	case 118: { // SYSCALL_GETRESUID
		uint32_t* ruid = (uint32_t*)rsp->rdi;
		uint32_t* euid = (uint32_t*)rsp->rsi;
		uint32_t* suid = (uint32_t*)rsp->rdx;
		if (ruid)
			*ruid = 0;
		if (euid)
			*euid = 0;
		if (suid)
			*suid = 0;
		rsp->rax = 0;
		break;
	}
	case 120: { // SYSCALL_GETRESGID
		uint32_t* rgid = (uint32_t*)rsp->rdi;
		uint32_t* egid = (uint32_t*)rsp->rsi;
		uint32_t* sgid = (uint32_t*)rsp->rdx;
		if (rgid)
			*rgid = 0;
		if (egid)
			*egid = 0;
		if (sgid)
			*sgid = 0;
		rsp->rax = 0;
		break;
	}
	case 121: { // SYSCALL_GETPGID
		pid_t pid = (pid_t)rsp->rdi;
		process_t* target_proc = nullptr;
		if (pid == 0) {
			target_proc = get_current_core_data()->active_thread->process;
		} else {
			target_proc = find_process_by_pid(pid);
		}

		if (target_proc) {
			rsp->rax = (uint64_t)target_proc->pgid;
		} else {
			rsp->rax = (uint64_t)-ESRCH;
		}
		break;
	}
	case 302: { // SYSCALL_PRLIMIT64
		struct rlimit64 {
			uint64_t rlim_cur;
			uint64_t rlim_max;
		};
		struct rlimit64* old_limit = (struct rlimit64*)rsp->r10;
		if (old_limit) {
			old_limit->rlim_cur = -1ULL;
			old_limit->rlim_max = -1ULL;
		}
		rsp->rax = 0;
		break;
	}

	case 112: { // SYSCALL_SETSID
		/* Create a new session. The calling process becomes the
		 * session leader and loses its controlling terminal.
		 * Returns the new session ID (= the process's own PID). */
		auto proc = get_current_core_data()->active_thread->process;
		if (!proc) {
			rsp->rax = (uint64_t)-EPERM;
			break;
		}
		/* Make the process its own process group and session leader */
		proc->pgid = proc->pid;
		rsp->rax = (uint64_t)proc->pid;
		break;
	}
	case 270: { // SYSCALL_PSELECT6
		/*
		 * pselect6(nfds, readfds, writefds, exceptfds, timeout, sigmask_ptr)
		 *
		 * Same semantics as select(2) for our purposes — reuse the
		 * existing poll / thread_block logic that SYSCALL_SELECT uses.
		 *
		 * rdi = nfds
		 * rsi = readfds  (fd_set* — bit array, 8-byte words)
		 * rdx = writefds  (ignored for now)
		 * r10 = exceptfds (ignored)
		 * r8  = struct timespec* timeout  (NULL = block forever)
		 * r9  = struct { sigset_t*; size_t }* sigmask  (ignored)
		 */
		int nfds = (int)rsp->rdi;
		uint64_t* readfds = (uint64_t*)rsp->rsi;
		auto curr_procc2 = get_current_core_data()->active_thread->process;
		auto fdt2 = (struct fdtable*)curr_procc2->fdtable;
		int ready_count2 = 0;

		while (true) {
			for (int i = 0; i < nfds; i++) {
				if (!readfds)
					break;
				if ((readfds[i / 64] & (1ULL << (i % 64))) == 0)
					continue;
				if ((uint32_t)i >= fdt2->max_fds)
					continue;
				auto curr_fd2 = fdt2->fds[i];
				if (!curr_fd2 || !curr_fd2->vnode)
					continue;
				auto ops2 = (vops_file_t*)curr_fd2->vnode->ops;
				int is_ready2 = 1;
				if (ops2 && ops2->poll) {
					is_ready2 = ops2->poll(curr_fd2->vnode, get_current_core_data()->active_thread);
				}
				if (is_ready2)
					ready_count2++;
			}
			if (ready_count2 > 0)
				break;

			/* Check timeout — timespec* is in r8 */
			if (rsp->r8) {
				struct __timespec_ps {
					long tv_sec;
					long tv_nsec;
				};
				struct __timespec_ps* ts = (struct __timespec_ps*)rsp->r8;
				if (ts->tv_sec == 0 && ts->tv_nsec == 0)
					break; /* poll mode, don't block */
			}

			auto curr = get_current_core_data()->active_thread;
			if (curr && curr->signal) {
				uint64_t pending = __atomic_load_n(&curr->signal->pending.__bits[0], __ATOMIC_ACQUIRE);
				uint64_t mask = __atomic_load_n(&curr->signal->mask.__bits[0], __ATOMIC_ACQUIRE);
				if (pending & ~mask) {
					rsp->rax = (uint64_t)-EINTR;
					return;
				}
			}

			thread_block();
		}

		/* Update readfds to reflect which fds are actually ready */
		if (readfds) {
			for (int i = 0; i < nfds; i++) {
				if ((readfds[i / 64] & (1ULL << (i % 64))) == 0)
					continue;
				boolean_t rdy = false;
				if ((uint32_t)i < fdt2->max_fds) {
					auto fd2 = fdt2->fds[i];
					if (fd2 && fd2->vnode) {
						auto ops2 = (vops_file_t*)fd2->ops;
						if (ops2 && ops2->poll) {
							if (ops2->poll(fd2->vnode, NULL))
								rdy = true;
						} else {
							rdy = true;
						}
					}
				}
				if (!rdy)
					readfds[i / 64] &= ~(1ULL << (i % 64));
			}
		}

		rsp->rax = (uint64_t)ready_count2;
		break;
	}
	case 324: { // SYSCALL_MEMBARRIER
		/* membarrier(cmd, flags, cpu_id)
		 * We just return 0 (success) for all commands — on a
		 * uniprocessor-style kernel where we issue barriers at
		 * every context switch, this is always a valid stub. */
		rsp->rax = 0;
		break;
	}

	case 22: { // SYSCALL_PIPE
		extern int syscall_pipe(int pipefd[2]);
		rsp->rax = (uint64_t)syscall_pipe((int*)rsp->rdi);
		break;
	}
	case 293: { // SYSCALL_PIPE2
		extern int syscall_pipe2(int pipefd[2], int flags);
		rsp->rax = (uint64_t)syscall_pipe2((int*)rsp->rdi, (int)rsp->rsi);
		break;
	}
	case 318: { // SYSCALL_GETRANDOM
		extern uint32_t vxRand();
		char* buf = (char*)rsp->rdi;
		size_t count = (size_t)rsp->rsi;
		// size_t flags = (size_t)rsp->rdx;
		size_t i = 0;
		while (i < count) {
			uint32_t r = vxRand();
			size_t copy_size = (count - i) < 4 ? (count - i) : 4;
			memcopy(buf + i, &r, copy_size);
			i += copy_size;
		}
		rsp->rax = count;
		break;
	}
	default:
		LOG2_DEBUG("syscall", "called %d (%s) 0x%x 0x%x %d", rsp->rax, get_syscall_name((int)rsp->rax), rsp->rdi, rsp->rsi, rsp->rdx);
		serial2_printf("unknown syscall %d\n", int_no);
		rsp->rax = (uint64_t)-ENOSYS;
		break;
	}

	if (thr && thr->signal && int_no != SYSCALL_EXIT && int_no != SYSCALL_EXIT_GROUP && int_no != SYSCALL_RT_SIGACTION && int_no != SYSCALL_SIGPROCMASK &&
	    int_no != 15) {
		uint64_t pending = __atomic_load_n(&thr->signal->pending.__bits[0], __ATOMIC_ACQUIRE);
		uint64_t mask = __atomic_load_n(&thr->signal->mask.__bits[0], __ATOMIC_ACQUIRE);
		uint64_t active_signals = pending & ~mask;
		if (active_signals) {
			for (int sig = 1; sig <= 64; sig++) {
				if (active_signals & SIGBIT(sig)) {
					sig_handle_ptr_t handler = thr->signal->handler[sig - 1];
					if (handler == 0) { // SIG_DFL
						if (sig == SIGINT || sig == SIGQUIT || sig == SIGKILL || sig == SIGTERM) {
							__atomic_fetch_and(&thr->signal->pending.__bits[0], ~SIGBIT(sig), __ATOMIC_RELEASE);
							serial2_printf("Thread %d "
							               "terminated by "
							               "signal %d in "
							               "syscall exit\n",
							               thr->id, sig);
							syscall_exit_group(128 + sig);
						}
					} else if ((uintptr_t)handler == 1) { // SIG_IGN
						__atomic_fetch_and(&thr->signal->pending.__bits[0], ~SIGBIT(sig), __ATOMIC_RELEASE);
					} else { // Custom handler
						serial2_printf("SYSCALL EXIT: Custom "
						               "signal %d handler %p "
						               "restorer %p for thread "
						               "%d\n",
						               sig, handler, thr->signal->restorer[sig - 1], thr->id);
						__atomic_fetch_and(&thr->signal->pending.__bits[0], ~SIGBIT(sig), __ATOMIC_RELEASE);
						vxSaveRegister(rsp, &thr->saved_reg);

						if (thr->signal->flags[sig - 1] & SA_RESTART) {
							thr->saved_reg.rip -= 2;
							thr->saved_reg.rax = (uint64_t)int_no;
						} else {
							thr->saved_reg.rax = rsp->rax;
						}
						thr->has_saved_reg = true;

						uint64_t* user_sp = (uint64_t*)((rsp->rsp & ~15ULL) - 8);
						*user_sp = (uint64_t)thr->signal->restorer[sig - 1];

						rsp->rsp = (uint64_t)user_sp;
						rsp->rdi = (uint64_t)sig;
						rsp->rsi = 0;
						rsp->rdx = 0;
						rsp->rip = (uintptr_t)handler;
						return;
					}
				}
			}
		}
	}
}

INIT(Syscall) { syscall_init(); }

extern const char* syscall_names[335];
static const char* get_syscall_name(int rax) {
	int max_syscalls = sizeof(syscall_names) / sizeof(syscall_names[0]);

	if (rax >= 0 && rax < max_syscalls && syscall_names[rax] != NULL) {
		return syscall_names[rax];
	}
	return "UNKNOWN_SYSCALL / NOT_IMPLEMENTED";
}