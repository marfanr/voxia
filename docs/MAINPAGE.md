# Voxia OS Documentation

Welcome to the official developer documentation for **Voxia OS**.

## 🚀 Overview

Voxia OS is a simple, modular x86_64 kernel designed with elegance and performance in mind. This documentation is automatically generated from the source code and providing a detailed view of the kernel's internal architecture.

## 🏗️ Core Architecture

The kernel is divided into several key subsystems:

- **Hardware Abstraction Layer (HAL)**: Low-level architecture-specific code (APIC, ACPI, GDT, IDT).
- **Memory Management**: Physical and virtual memory managers, including a slab allocator.
- **Process & Thread Management**: Multi-core scheduler and thread primitives.
- **Virtual File System (VFS)**: A flexible layer for filesystem abstraction.
- **Networking**: Integrated network stack with support for modern NICs.

## 📂 Navigation

- [Files](files.html): Browse the source tree.
- [Modules](modules.html): High-level grouping of kernel logic.
- [Data Structures](annotated.html): Overview of types and structures.

## 🛠️ Contributing

To update this documentation:
1.  Edit the source code comments (using Doxygen style `/** ... */`).
2.  Run `make doc` from the project root.
3.  Refresh your browser.

---
Developed with ❤️ by the Voxia Team.
