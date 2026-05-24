# Voxia2 Process and Thread Specification

## 1. Introduction
Process and thread management in Voxia2 provides the infrastructure for multitasking. The system uses a strict separation between a **Process** (the resource container) and a **Thread** (the unit of execution).

## 2. Process Management (`proccess_t`)
A Process in Voxia2 is an abstraction representing a running program instance. It manages resources such as memory address space, file descriptors, and identity.

### 2.1 Process Control Block (PCB)
Defined in `kernel/procc/process.h` as `proccess_t`.

| Field | Type | Description |
| :--- | :--- | :--- |
| `pid` | `pid_t` | Unique Process Identifier (up to 4,194,304). |
| `parent_pid` | `pid_t` | PID of the creator process. |
| `name` | `char[64]` | Human-readable process name. |
| `main_thread`| `struct thread*` | The primary thread created with the process. |
| `fdtable` | `struct fdtable*`| Table of open file descriptors (VFS handles). |
| `exit_code` | `int` | Return value after termination. |
| `exited` | `bool` | Status flag for process completion. |

### 2.2 Process Lifecycle
1. **Creation**: Handled via `create_process`. PIDs are managed through `alloc_pid` and `free_pid`.
2. **Execution**: The `execve` system call replaces the current process image with a new executable (ELF loading).
3. **Termination**: Processes exit with a code, and resources (like the PID) are reclaimed.

---

## 3. Thread Management (`thread_t`)
Threads are the basic units of CPU utilization. Voxia2 supports multiple threads within a single process sharing the same address space.

### 3.1 Thread Control Block (TCB)
Defined in `include/procc/thread.h` as `thread_t`. It is optimized for cache alignment (64 bytes).

| Field | Type | Description |
| :--- | :--- | :--- |
| `id` | `thread_id` | Unique thread ID (encoded with generation). |
| `core_affinity`| `uint16_t` | CPU core assigned to run this thread. |
| `state` | `uint8_t` | Current state (READY, RUNNING, etc.). |
| `priority` | `uint8_t` | Priority level for the scheduler. |
| `flags` | `uint16_t` | Control flags (e.g., USER/KERNEL, Preemption). |
| `reg` | `cpu_register_t`| Saved CPU state (context) for switching. |
| `stack` | `uint64_t` | Pointer to the thread's stack. |

### 3.2 Thread States
- `THREAD_STATE_CREATE`: Initial state.
- `THREAD_STATE_READY`: Waiting in the run queue.
- `THREAD_STATE_RUNNING`: Currently executing on a CPU core.
- `THREAD_STATE_TERMINATED`: Finished execution, awaiting cleanup.

### 3.3 Thread ID Encoding
Thread IDs are 64-bit values combining an index and a generation count to prevent ID reuse issues:
- `THREAD_MAKE_ID(id, gen)`: `((gen << 32) | (id + 1))`

---

## 4. Scheduling
The Voxia2 scheduler (`kernel/procc/scheduler.c`) is responsible for selecting the next thread to run.

### 4.1 Architecture
- **Per-Core Scheduler**: Each CPU core has its own `scheduler_core_t` with a local `run_queue`.
- **Run Queue**: A doubly linked list of `scheduler_queue_t` containing threads.
- **Preemption**: Controlled via flags (`THREAD_PREEMPT_ENABLE/DISABLE`).

### 4.2 Key Operations
- `vxStartScheduler()`: Initializes and enters the scheduling loop.
- `attach_to_scheduler(thread_t*)`: Adds a new thread to the appropriate core's run queue.
- `vxGetSchedulerCore(core)`: Retrieves the scheduler state for a specific core.

---

## 5. Summary of Relationships
- A **Process** owns resources (Memory, FDs).
- A **Process** must have at least one **Thread** (the `main_thread`).
- **Threads** are scheduled independently across cores based on **Core Affinity**.
- Context switching involves saving/restoring the `cpu_register_t` state within the `thread_t`.
